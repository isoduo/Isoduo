#include "check.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#ifdef _WIN32
#include "desktop_host.hpp"
#else
#include "desktop_linux_test.hpp"
#endif
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <syncstream>
#include <thread>

using namespace isoduo;
using namespace isoduo::detail;

namespace {
void require(bool passed, const char* message) {
    if (!passed) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

struct Window {
    Ui ui;
    ScrollState scroll;
    TextFieldHandle field = ui.addTextField({.id = "draft"}).value.value();
    RendererTestHooks hooks;
    std::unique_ptr<Desktop> desktop;
    int frames = 0;

    Window() {
        field.connectSource({[] { return u"Initial"; }, test::ignoreNotifications});
        field.connectConfirmed([](const auto&) { return Result{}; });
    }

    void open(DesktopConfig config) {
        config.width = 240;
        config.height = 160;
        auto page = [&] { return layout::component(field); };
#ifdef _WIN32
        auto result = DesktopHost::createForTest(ui, webAppearance(), page, scroll, config, &hooks);
#else
        auto result = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
#endif
        if (result.failure) {
            std::cerr << utf8FromUtf16(result.failure->message) << std::endl;
            if (config.renderBackend == RenderBackendKind::vulkan &&
                (result.failure->kind == StartupFailureKind::renderBackendUnavailable ||
                 result.failure->kind == StartupFailureKind::contextVersion)) std::exit(77);
        }
        require(bool(result.desktop), "Create the requested desktop");
        desktop = std::move(result.desktop);
        auto caps = desktop->capabilities();
        require(caps.renderBackend == config.renderBackend, "Use the requested renderer");
        std::osyncstream(std::cout) << utf8FromUtf16(caps.renderer) << " / " << utf8FromUtf16(caps.vendor)
                                    << " / " << utf8FromUtf16(caps.version) << '\n';
    }

    void run(std::function<bool()> finished) {
        require(bool(field.focus()), "Focus the draft for pixel verification");
        frames = 0;
        auto presented = [&] {
            ++frames;
            // Change state outside paint, so the invalidation schedules a new native frame.
            desktop->post([&] {
                if (desktop->rendererFailed()) return;
                if (finished()) desktop->close();
                else {
                    auto settings = ui.presentation();
                    settings.textSize = settings.textSize == 18 ? 20 : 18;
                    require(bool(ui.setPresentation(settings)), "Request the next frame");
                }
            });
        };
#ifdef _WIN32
        auto& host = DesktopHost::fromDesktop(*desktop);
        host.onPresented = presented;
#else
        LinuxDesktopTest::observeFrames(*desktop, [&](const auto&, const auto&) { presented(); });
#endif
        auto start = std::chrono::steady_clock::now();
        require(desktop->run().status == Status::ok && frames > 0, "Run and present without failure");
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
        std::osyncstream(std::cout) << frames << " presented frames in " << ms.count() << " ms\n";
    }

    void compare() {
#ifdef _WIN32
        auto result = DesktopHost::fromDesktop(*desktop).compareGpu(240, 160, 1);
#else
        Frame frame;
        auto result = LinuxDesktopTest::compareFrame(*desktop, 240, 160, 1, frame);
#endif
        require(result.passed && !result.regions.empty() && !desktop->rendererFailed(), "K05 on the active desktop");
    }
};

void concurrent(std::string_view scenario, DesktopConfig config) {
    std::atomic<int> progress[2]{};
    std::atomic<Desktop*> failed{};
    std::atomic<bool> closed{};
    auto worker = [&](int side) {
        Window window;
        auto options = config;
        if (scenario == "md2" && side == 0) options.renderBackend = RenderBackendKind::openGL;
        if (scenario == "md3" && side == 0) {
            if (options.renderBackend == RenderBackendKind::vulkan) window.hooks.submitResultAtFrame = {1, -4};
            else window.hooks.resetAtFrame = 1;
            options.onRendererFailed = [&](Desktop& desktop, const RendererFailure& error) {
                require(error.kind == RendererFailureKind::deviceReset && desktop.rendererFailed(), "Observe device loss");
                failed.store(&desktop);
            };
        }
        window.open(options);
        require(bool(window.field.setValue(side ? u"Second" : u"First")), "Set distinct desktop content");
        bool sawClosedPeer = false;
        window.run([&] {
            if (scenario == "md3") {
                if (side == 0) {
                    require(failed.load() == window.desktop.get(), "Present again after explicit rebuild");
                    window.compare();
                    return true;
                }
                if (auto* peer = failed.load(); peer && progress[side] < 3) {
                    window.compare();
                    if (++progress[side] == 3) {
                        require(bool(peer->post([peer] {
                            require(peer->rendererFailed() && bool(peer->rebuildRenderer()), "Rebuild only the failed desktop");
                        })), "Request explicit rebuild on owner thread");
                    }
                }
                return closed.load();
            }
            if (scenario == "md4") {
                window.compare();
                if (side == 0) return progress[1] > 0;
                ++progress[1];
                if (sawClosedPeer) return true;
                sawClosedPeer = closed.load();
                return false;
            }
            if (progress[side] == 0 || (progress[1 - side] > 0 && progress[side] < 4)) {
                window.compare();
                ++progress[side];
                return false;
            }
            return progress[side] == 4 && progress[1 - side] == 4;
        });
        if (side == 0) closed = true;
    };
    std::jthread first(worker, 0);
    worker(1);
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 1;
#ifndef _WIN32
    if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("DISPLAY")) return 77;
#endif
    DesktopConfig config;
    config.renderBackend = std::string_view(argv[2]) == "opengl" ? RenderBackendKind::openGL : RenderBackendKind::vulkan;
    const std::string_view scenario = argv[1];
    if (scenario == "md5" || scenario == "sequential") {
        Window first;
        auto options = config;
        if (scenario == "md5") options.renderBackend = RenderBackendKind::openGL;
        first.open(options);
        require(bool(first.field.setValue(u"Retained draft")), "Edit the first host");
        Window other;
        if (scenario == "sequential") other.open(config);
        first.run([&] { first.compare(); return true; });
        if (scenario == "md5") first.open(config);
        auto& next = scenario == "md5" ? first : other;
        next.run([&] {
            require(first.field.draft().value == u"Retained draft", "Keep the component draft after changing host");
            next.compare();
            return true;
        });
    } else if (scenario == "md1" || scenario == "md2" || scenario == "md3" || scenario == "md4") {
        concurrent(scenario, config);
    } else return 1;
}
