#include "isoduo/desktop.hpp"
#include "desktop_linux_test.hpp"
#include "renderer.hpp"
#include "check.hpp"
#include "core_internal.hpp"
#include "perf.hpp"
#include <ctime>
#include "utf.hpp"
#include <atomic>
#include <barrier>
#include <clocale>
#include <cstdio>
#include <thread>

using namespace isoduo;
using namespace isoduo::detail;

#include "renderer_fault_sweep.hpp"

namespace {
void check(bool condition, const char* expectation) { test::check(condition, expectation); }
void printCapabilities(const RendererCapabilities& caps) {
    std::cout << "Version " << utf8FromUtf16(caps.version)
              << "\nVendor " << utf8FromUtf16(caps.vendor)
              << "\nRenderer " << utf8FromUtf16(caps.renderer)
              << "\nDebug context " << caps.debugContext << ", messages " << caps.debugMessages
              << ", reset detection " << caps.resetDetection << '\n';
    int major = 0, minor = 0;
    check(std::sscanf(utf8FromUtf16(caps.version).c_str(), "%d.%d", &major, &minor) == 2 &&
        (major > 3 || (major == 3 && minor >= 3)), "Context version is at least 3.3");
    check(!caps.vendor.empty() && !caps.renderer.empty(), "Context vendor and renderer are reported");
}
void checkStartup(const CreateResult& result) {
    if (result.failure) {
        std::cerr << "Startup failed " << static_cast<int>(result.failure->kind) << ' '
                  << utf8FromUtf16(result.failure->message) << " actual " << utf8FromUtf16(result.failure->actual) << '\n';
    }
    check(result.status == Status::ok && !!result.desktop, "Native desktop opens");
}
void applicationFrames(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    auto area = *ui.addDrawingArea({.id = "indicator", .label = u"Indicator"});
    auto spacer = *ui.addDrawingArea({.id = "spacer", .label = u"Spacer"});
    auto page = [&] { return layout::scroll(layout::column({
        layout::drawingArea(area, 20), layout::drawingArea(spacer, 800)}, 0, 0)); };
    RendererTestHooks hooks;
    if (config.renderBackend == RenderBackendKind::vulkan) hooks.submitResultAtFrame = {1, -2};
    else hooks.errorAtFrame = 1;
    int calls = 0, presents = 0, recovery = 0;
    config.onRendererFailed = [&](Desktop& host, const RendererFailure&) {
        check(calls == 1 && !*ui.animating(), "Failed presentation pauses the continuation");
        check(area.cancelFrame() && area.requestFrame(), "Accept cancellation and requests during failure");
        check(bool(host.rebuildRenderer()), "Rebuild after the callback already ran");
        ++recovery;
    };
    auto opened = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
    checkStartup(opened);
    auto& host = *opened.desktop;
    check(ui.simulateFrame({}).status == Status::notSupported, "Desktop owns frame time");
    using namespace std::chrono_literals;
    std::chrono::steady_clock::time_point now{};
    access(ui).animationTime([&] { return now; });
    int quietStart = 0, lastPresentCount = -1, scrollFrame = 0, compositorWaits = 0;
    bool shortWait = false, resumed = false, idle = false;
    std::jthread idleProbe;
    LinuxDesktopTest::observeWait(host, [&](int timeout) {
        if (calls >= 4 && !scrollFrame && host.nativeWindow()->backend == DesktopBackendKind::wayland) {
            if (timeout < 0) ++compositorWaits;
            if (timeout == 16) shortWait = true;
        }
    });
    std::chrono::steady_clock::time_point previous;
    bool ordered = true, clipped = false;
    area.onFrame([&](const FrameInfo& frame) {
        ++calls;
        ordered = ordered && frame.time >= previous;
        previous = frame.time;
        // Native map/expose traffic can submit during startup. Require four consecutive
        // callback-only intervals after the last such submission.
        if (presents != lastPresentCount) {
            quietStart = calls;
            lastPresentCount = presents;
        }
        if (!scrollFrame && recovery && calls - quietStart == 4) {
            scrollFrame = calls;
            scroll.offset = 100;
            area.requestRedraw();
        }
        if (!scrollFrame && calls > 64) return host.close();
        if (scrollFrame && calls == scrollFrame + 1) {
            LinuxDesktopTest::setVisible(host, false);
            area.requestFrame();
            const bool paused = !*ui.animating();
            host.post([&, paused] {
                now += 3s;
                resumed = paused && calls == scrollFrame + 1;
                LinuxDesktopTest::setVisible(host, true);
            });
            return Result{};
        }
        if (scrollFrame && calls == scrollFrame + 2) {
            resumed = resumed && frame.time == now && now.time_since_epoch() == 3s;
            return area.requestFrame();
        }
        if (scrollFrame && calls == scrollFrame + 3) {
            const int start = presents;
            const auto cpu = std::clock();
            idleProbe = std::jthread([&, start, cpu] {
                std::this_thread::sleep_for(10s);
                host.post([&, start, cpu] {
                    idle = idlePass(presents - start, 0, 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC);
                    host.close();
                });
            });
            return Result{};
        }
        return area.requestFrame();
    });
    area.onDraw([](const DrawingEnvironment&) { return Result{}; });
    area.requestFrame();
    LinuxDesktopTest::observeFrames(host, [&](const Frame& frame, const LinuxClientGeometry&) {
        ++presents;
        if (scrollFrame && calls == scrollFrame && !clipped) {
            clipped = !*ui.animating();
            check(frame.scrollTo(ui, scroll, 0) && spacer.enable(false), "Scroll the pending area back into view");
        }
    });
    const bool wayland = host.nativeWindow()->backend == DesktopBackendKind::wayland;
    auto run = host.run();
    check(idle, "Ten seconds idle after the last frame request");
    check(!wayland || (compositorWaits > 0 && !shortWait), "Wayland waits for compositor release without a 16ms timer");
    check(run.status == Status::ok && scrollFrame && calls == scrollFrame + 3 && recovery == 1, "Resume the continuation after explicit rebuild");
    check(ordered, "Ordered times and no submissions for callback-only frames");
    check(clipped, "Scrolling out pauses the request");
    check(resumed, "Visibility restoration runs the callback at the advanced time");
}
void checkFaults(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    auto page = [] { return layout::column({}); };
    auto create = [&](RendererTestHooks& hooks) {
        auto result = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
        if (!result.desktop)
            check(result.status == Status::failed && result.failure && result.failure->backend == config.backend,
                "Structured startup failure names the selected backend");
        return result;
    };
    RendererTestHooks hooks;
    hooks.failContext33 = true;
    auto result = create(hooks);
    check(result.failure && result.failure->kind == StartupFailureKind::contextVersion &&
        result.failure->required == u"OpenGL 3.3 core" && result.failure->actual == u"2.1", "Version failure includes both versions");
    hooks = {};
    hooks.fragmentShaderOverride = "invalid shader";
    result = create(hooks);
    check(result.failure && result.failure->kind == StartupFailureKind::shader &&
        !result.failure->message.empty(), "Shader compile failure includes driver message");
    hooks = {};
    hooks.startupResourceError = "resource 中文";
    result = create(hooks);
    check(result.failure && result.failure->kind == StartupFailureKind::resource &&
        result.failure->message == u"resource 中文", "Resource error keeps its Unicode text");

    hooks = {};
    hooks.hideKhrDebug = hooks.hideRobustness = true;
    config.debugContext = DebugContext::on;
    result = create(hooks);
    checkStartup(result);
    auto caps = result.desktop->capabilities();
    check(caps.debugContext && !caps.debugMessages && !caps.resetDetection,
        "Debug context remains available without optional messages or reset detection");
    auto* desktop = result.desktop.get();
    Status status = Status::ok;
    std::thread worker([&] { status = desktop->close().status; });
    worker.join();
    check(status == Status::wrongThread, "Wrong thread cannot close");
    check(desktop->nativeWindow() != nullptr, "Wrong-thread close preserves the window");
    check(bool(desktop->close()), "Owner can close before running");
    check(desktop->run().status == Status::ok && !desktop->nativeWindow(), "Run returns and releases closed window");
    result.desktop.reset();

    hooks = {};
    hooks.resetAtFrame = 1;
    config.debugContext = DebugContext::off;
    int recovered = 0;
    const NativeWindow* reference = nullptr;
    config.onRendererFailed = [&](Desktop& host, const RendererFailure& failure) {
        check(failure.kind == RendererFailureKind::deviceReset, "Run reports a simulated graphics reset");
        check(host.rendererFailed(), "Host enters renderer failure");
        check(bool(host.rebuildRenderer()), "Owner rebuilds renderer explicitly");
        check(!host.rendererFailed() && host.nativeWindow() == reference, "Rebuild preserves the native window reference");
        ++recovered;
        host.close();
    };
    result = create(hooks);
    checkStartup(result);
    reference = result.desktop->nativeWindow();
    check(result.desktop->run().status == Status::ok && recovered == 1, "Run returns after the recovery callback closes");
    hooks = {};
    hooks.errorAtFrame = 1;
    config.onRendererFailed = [](Desktop& host, const RendererFailure&) { host.close(); };
    result = create(hooks);
    checkStartup(result);
    check(result.desktop->run().status == Status::rendererFailed, "Close a failed host");
    auto button = ui.addButton({.id = "recovered"}).value.value();
    auto next = Desktop::create(ui, webAppearance(), [&] { return layout::component(button); }, scroll, config);
    checkStartup(next);
    check(!next.desktop->rendererFailed() && bool(button.focus()), "Replacement host accepts focus");
    bool painted = false;
    LinuxDesktopTest::observeFrames(*next.desktop, [&](const Frame&, const LinuxClientGeometry&) {
        painted = true;
        next.desktop->close();
    });
    check(next.desktop->run().status == Status::ok && painted, "Replacement host paints a frame");
    std::cout << "PASS startup fixtures, close thread, renderer rebuild\n";
}
void concurrentX11Locale(DesktopConfig config) {
    check(config.backend == DesktopBackendKind::x11, "Locale verification requires X11");
    check(std::setlocale(LC_CTYPE, "C") != nullptr, "Select the C locale");
    for (int round = 0; round < 50; ++round) {
        std::barrier start(2);
        auto open = [&](bool& opened) {
            Ui ui;
            ScrollState scroll;
            start.arrive_and_wait();
            auto host = Desktop::create(ui, webAppearance(), [] { return layout::column({}); }, scroll, config);
            opened = host.desktop && LinuxDesktopTest::x11InputMethodOpen(*host.desktop);
            start.arrive_and_wait();
        };
        bool first = false, second = false;
        std::jthread worker([&] { open(first); });
        open(second);
        worker.join();
        check(first && second, "Both concurrent desktops open their X11 input method");
        check(std::string_view(std::setlocale(LC_CTYPE, nullptr)) == "C", "Concurrent opens restore LC_CTYPE");
    }
}
void configuredX11Locale(DesktopConfig config) {
    check(config.backend == DesktopBackendKind::x11, "Locale verification requires X11");
    const char* locale = std::setlocale(LC_CTYPE, "C.UTF-8");
    check(locale != nullptr, "Select the application UTF-8 locale");
    const std::string expected(locale);
    std::atomic<bool> changed = false;
    std::barrier ready(2);
    std::jthread observer([&](std::stop_token stop) {
        ready.arrive_and_wait();
        do {
            if (std::string_view(std::setlocale(LC_CTYPE, nullptr)) != expected) changed = true;
        } while (!stop.stop_requested());
    });
    ready.arrive_and_wait();
    Ui ui;
    ScrollState scroll;
    auto host = Desktop::create(ui, webAppearance(), [] { return layout::column({}); }, scroll, config);
    observer.request_stop();
    observer.join();
    checkStartup(host);
    check(!changed && std::string_view(std::setlocale(LC_CTYPE, nullptr)) == expected,
        "Application LC_CTYPE is preserved during and after opening");
}
void lowVersion(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    auto result = Desktop::create(ui, webAppearance(), [] { return layout::column({}); }, scroll, config);
    check(!result.desktop && result.failure && result.failure->backend == config.backend &&
        result.failure->kind == StartupFailureKind::contextVersion, "A driver below minimum GL version fails startup");
    check(result.failure->required == u"OpenGL 3.3 core" && result.failure->actual.starts_with(u"2.1"),
        "Version failure reports the driver version");
    std::cout << "Required " << utf8FromUtf16(result.failure->required) << ", actual "
              << utf8FromUtf16(result.failure->actual) << '\n';
}
void interactive(DesktopConfig config, std::string_view mode) {
    Ui ui;
    ScrollState scroll;
    ComponentConfig component;
    component.id = "action";
    component.label = u"Activate after first close";
    auto button = ui.addButton(component).value.value();
    int activations = 0;
    button.bind([&] { std::cout << "Activated " << ++activations << std::endl; return Result{}; });
    auto page = [&] { return layout::column({layout::component(button)}); };
    if (mode == "wrapper") {
        check(runDesktop(ui, materialAppearance(), page, scroll, config).status == Status::ok,
            "runDesktop returns success on native close");
        check(activations > 0, "runDesktop window receives input");
        return;
    }

    int requests = 0;
    const NativeWindow* reference = nullptr;
    config.onCloseRequested = [&](Desktop& host) {
        check(host.nativeWindow() == reference, "Close callback sees the same native window");
        std::cout << "Close request " << ++requests << std::endl;
        if (requests == 1) return CloseDecision::keepOpen;
        check(activations > 0, "Window receives input after close veto");
        if (mode == "program") {
            check(bool(host.close()), "Programmatic close bypasses veto");
            return CloseDecision::keepOpen;
        }
        return CloseDecision::close;
    };
    auto created = Desktop::create(ui, webAppearance(), page, scroll, config);
    checkStartup(created);
    auto& desktop = *created.desktop;
    reference = desktop.nativeWindow();
    check(reference && reference->display && (reference->backend == DesktopBackendKind::x11 ?
        reference->windowId != 0 && !reference->surface : reference->surface && !reference->windowId),
        "Tagged native reference identifies the display and window");
    if (config.backend)
        check(reference->backend == config.backend, "Native reference uses the selected backend");
    check(bool(desktop.setTitle(u"Isoduo native verification 中文")), "Title update accepted");
    std::cout << "Backend " << (reference->backend == DesktopBackendKind::wayland ? "wayland" : "x11") << '\n';
    printCapabilities(desktop.capabilities());
    check(desktop.capabilities().debugContext == (config.debugContext == DebugContext::on),
        "Context matches its requested debug mode");
    std::cout << "Window " << reference->windowId << std::endl;
    check(desktop.run().status == Status::ok && requests == 2 && !desktop.nativeWindow(), "Run returns and releases window");
}
}

int main(int argc, char** argv) {
    const bool rendererCase = argc == 5 && (std::string_view(argv[2]) == "fault-sweep" ||
                           std::string_view(argv[2]) == "frames") &&
                           std::string_view(argv[3]) == "--render-backend";
    if (argc != 3 && !rendererCase) {
        std::cerr << "Usage: isoduo_linux_desktop_native <wayland|x11|auto> "
                     "<faults|close|program|wrapper|low-version|locale|configured-locale|fault-sweep --render-backend opengl|vulkan>\n";
        return 2;
    }
    try {
        DesktopConfig config;
        const std::string_view backend = argv[1], mode = argv[2];
        if (backend == "wayland") config.backend = DesktopBackendKind::wayland;
        else if (backend == "x11") config.backend = DesktopBackendKind::x11;
        else check(backend == "auto", "Backend must be wayland, x11, or auto");
        config.width = 480;
        config.height = 320;
        config.title = u"Isoduo native verification";
        config.debugContext = mode == "program" ? DebugContext::on : DebugContext::off;
        if (rendererCase) {
            config.debugContext = DebugContext::buildDefault;
            config.renderBackend = test::rendererFaultSweep::renderBackend(argv[4]);
            if (mode == "fault-sweep") return test::rendererFaultSweep::run(config);
            if (!config.backend && !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return 77;
            applicationFrames(config);
            return 0;
        }
        if (mode == "locale") concurrentX11Locale(config);
        else if (mode == "configured-locale") configuredX11Locale(config);
        else if (mode == "faults") checkFaults(config);
        else if (mode == "low-version") lowVersion(config);
        else {
            check(mode == "close" || mode == "program" || mode == "wrapper", "Unknown verification mode");
            interactive(config, mode);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
