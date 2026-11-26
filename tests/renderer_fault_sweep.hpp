#pragma once
#include "check.hpp"
#ifdef _WIN32
#include "desktop_host.hpp"
#else
#include "desktop_linux_test.hpp"
#endif
#include "utf.hpp"
#include <cstdlib>
#include <initializer_list>
#include <tuple>

namespace test::rendererFaultSweep {
using namespace isoduo;
using namespace isoduo::detail;
using StartupKind = StartupFailureKind;
using FrameKind = RendererFailureKind;
using Inject = std::function<void(RendererTestHooks&)>;

inline RenderBackendKind renderBackend(std::string_view name) {
    check(name == "opengl" || name == "vulkan", "Fault sweep renderer must be opengl or vulkan");
    return name == "vulkan" ? RenderBackendKind::vulkan : RenderBackendKind::openGL;
}

inline void observePaint(Desktop& desktop, std::function<void()> observe) {
#ifdef _WIN32
    DesktopHost::fromDesktop(desktop).onPresented = std::move(observe);
#else
    LinuxDesktopTest::observeFrames(desktop, [observe = std::move(observe)](const Frame&, const LinuxClientGeometry&) {
        observe();
    });
#endif
}

// Failure context is checked outside application callbacks so the Win32 window
// procedure cannot turn an assertion exception into an unrelated run failure.
template<class Action>
auto boundary(const char* entryPoint, Action action) {
    try {
        return action();
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(entryPoint) + " threw: " + error.what());
    } catch (...) {
        throw std::runtime_error(std::string(entryPoint) + " threw a non-standard exception");
    }
}

template<class Field, class Value>
Inject set(Field RendererTestHooks::* field, Value value) {
    return [field, value](RendererTestHooks& hooks) { hooks.*field = value; };
}

inline Inject vkResult(const char* function, int result) {
    return [=](RendererTestHooks& hooks) { hooks.startupResultOnce = {function, result}; };
}

struct Fixture {
    Ui ui;
    ScrollState scroll;
    RendererTestHooks hooks;
    LabelHandle label = ui.addLabel({.id = "glyphs", .value = u"Renderer fault sweep"}).value.value();

    CreateResult open(const DesktopConfig& config, bool inject = true) {
        auto page = [label = label] { return layout::column({layout::component(label)}); };
        return boundary("Desktop::create", [&] {
            if (!inject) return Desktop::create(ui, webAppearance(), page, scroll, config);
#ifdef _WIN32
            return DesktopHost::createForTest(ui, webAppearance(), page, scroll, config, &hooks);
#else
            return LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
#endif
        });
    }
};

inline void checkOpened(const CreateResult& result) {
    if (result.failure) std::cerr << utf8FromUtf16(result.failure->message) << '\n';
    check(result.status == Status::ok && result.desktop && !result.failure, "Desktop opens successfully");
}

inline RunResult runDesktop(Desktop& desktop) {
    auto result = boundary("Desktop::run / paint", [&] { return desktop.run(); });
    check(!desktop.nativeWindow(), "run releases the native window");
    check(boundary("Desktop::post after run", [&] { return desktop.post([] {}); }).status == Status::notFound,
          "post returns notFound after run");
    check(boundary("Desktop::rebuildRenderer after run", [&] { return desktop.rebuildRenderer(); }).status == Status::notFound,
          "rebuild returns notFound after run");
    return result;
}

inline void closeAfterPostedWork(Desktop& desktop, Result& posted, Result& closed) {
    posted = boundary("Desktop::post", [&] {
        return desktop.post([&] { closed = boundary("Desktop::close", [&] { return desktop.close(); }); });
    });
    // Keep the event loop bounded when a regression rejects the post.
    if (!posted) desktop.close();
}

struct StartupFault {
    const char* name;
    Inject inject;
    StartupKind kind;
    bool rebuild = false;
};

struct FrameFault {
    const char* name;
    Inject inject;
    FrameKind kind;
    std::u16string_view message{};
    bool armAtCreate = false;
};

inline void startup(const DesktopConfig& config, const StartupFault& fault) {
    Fixture fixture;
    fault.inject(fixture.hooks);
    auto result = fixture.open(config);
    check(result.status == Status::failed && !result.desktop && result.failure &&
          result.failure->kind == fault.kind && result.failure->backend == config.backend && !result.failure->message.empty(),
          "Startup fault returns the expected StartupFailure");
    // Failed creation must release the Ui claim so the public API can retry.
    checkOpened(fixture.open(config, false));
}

inline void rebuild(DesktopConfig config, const Inject& inject, bool expectFailure, bool replaceSurface = false) {
    Fixture fixture;
    const bool vulkan = config.renderBackend == RenderBackendKind::vulkan;
    const auto kind = replaceSurface ? FrameKind::presentFailed : FrameKind::deviceReset;
    if (replaceSurface) fixture.hooks.presentResultAtFrame = {1, -1000000000}; // VK_ERROR_SURFACE_LOST_KHR
    else if (vulkan) fixture.hooks.submitResultAtFrame = {1, -4}; // VK_ERROR_DEVICE_LOST
    else fixture.hooks.resetAtFrame = 1;

    int callbacks = 0, painted = 0;
    bool stillFailed = false, recovered = false;
    Result first{Status::invalidValue, {}}, posted{Status::failed, {}}, closed{Status::failed, {}};
    std::optional<RendererFailure> seen;
    config.onRendererFailed = [&](Desktop& desktop, const RendererFailure& failure) {
        seen = failure;
        if (++callbacks != 1) {
            desktop.close();
            return;
        }
        inject(fixture.hooks);
        first = boundary("Desktop::rebuildRenderer", [&] { return desktop.rebuildRenderer(); });
        stillFailed = desktop.rendererFailed();
        fixture.hooks = {};
        recovered = !stillFailed || bool(boundary("Desktop::rebuildRenderer retry", [&] { return desktop.rebuildRenderer(); }));
        if (!recovered) desktop.close();
        // The swapchain may have no extent yet. Once it has one, a frame must present.
    };
    auto created = fixture.open(config);
    checkOpened(created);
    Desktop& desktop = *created.desktop;
    observePaint(desktop, [&] {
        ++painted;
        if (painted == 1) closeAfterPostedWork(desktop, posted, closed);
    });
    const auto result = runDesktop(desktop);
    check(callbacks == 1 && seen && seen->kind == kind, "The initial fault reaches onRendererFailed exactly once");
    check(first.status == (expectFailure ? Status::failed : Status::ok) && stillFailed == expectFailure &&
          (!expectFailure || !first.message.empty()), "Rebuild returns the expected Result and failure state");
    check(recovered && painted > 0 && posted && closed && result.status == Status::ok && !result.failure,
          "Explicit recovery resumes painting and permits post and close");
}

inline void frame(DesktopConfig config, const FrameFault& fault, bool handled) {
    Fixture fixture;
    if (fault.armAtCreate) fault.inject(fixture.hooks);
    std::optional<RendererFailure> seen;
    int callbacks = 0, painted = 0;
    bool frozen = false;
    Result posted{Status::failed, {}}, closed{Status::failed, {}};
    if (handled) config.onRendererFailed = [&](Desktop& desktop, const RendererFailure& failure) {
        ++callbacks;
        seen = failure;
        frozen = desktop.rendererFailed();
        closeAfterPostedWork(desktop, posted, closed);
    };
    auto created = fixture.open(config);
    checkOpened(created);
    if (!fault.armAtCreate) fault.inject(fixture.hooks);
    Desktop& desktop = *created.desktop;
    observePaint(desktop, [&] {
        ++painted;
        desktop.close(); // If the injection is missed, fail the verdict instead of hanging.
    });
    check(boundary("Desktop::rebuildRenderer while healthy", [&] { return desktop.rebuildRenderer(); }).status ==
          Status::invalidValue, "Healthy renderer rejects rebuild");
    const auto result = runDesktop(desktop);
    auto expected = [&](const RendererFailure& failure) {
        return failure.kind == fault.kind && !failure.message.empty() &&
               (fault.message.empty() || failure.message == fault.message);
    };
    const bool failed = painted == 0 && result.status == Status::rendererFailed && result.failure && expected(*result.failure);
    if (!failed) {
        std::cerr << "run status " << static_cast<int>(result.status) << ", frames " << painted;
        if (result.failure) std::cerr << ", failure " << utf8FromUtf16(result.failure->message);
        std::cerr << '\n';
    }
    check(failed,
          "Runtime fault returns the expected RendererFailure from run");
    if (handled)
        check(callbacks == 1 && frozen && seen && expected(*seen) && posted && closed,
              "Fault reaches onRendererFailed exactly once and permits post and close while failed");
}

inline void paints(DesktopConfig config, const Inject& inject,
                   std::function<void(const RendererCapabilities&, const std::vector<RenderDiagnostic>&)> verify = {},
                   bool clearZeroExtent = false) {
    Fixture fixture;
    inject(fixture.hooks);
    int callbacks = 0, painted = 0;
    std::vector<RenderDiagnostic> events;
    config.onDiagnostic = [&](const RenderDiagnostic& event) { events.push_back(event); };
    config.onRendererFailed = [&](Desktop& desktop, const RendererFailure&) { ++callbacks; desktop.close(); };
    auto created = fixture.open(config);
    checkOpened(created);
    Desktop& desktop = *created.desktop;
    const auto capabilities = desktop.capabilities();
    if (clearZeroExtent) fixture.hooks.zeroSurfaceExtent = false;
    Result posted{Status::failed, {}}, closed{Status::failed, {}};
    observePaint(desktop, [&] {
        if (++painted == 1) closeAfterPostedWork(desktop, posted, closed);
    });
    const auto result = runDesktop(desktop);
    check(painted > 0 && callbacks == 0 && posted && closed && result.status == Status::ok && !result.failure,
          "Renderer presents without entering failure and accepts post and close");
    if (verify) verify(capabilities, events);
}

struct Sweep {
    int passed = 0;
    int failed = 0;
    template<class Action>
    void row(const char* name, const char* entryPoint, Action action) {
        try {
            action();
            ++passed;
            std::cout << "PASS " << name << " / " << entryPoint << std::endl;
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << " / " << entryPoint << ": " << error.what() << std::endl;
        }
    }
    void startupRows(const DesktopConfig& config, std::initializer_list<StartupFault> faults) {
        for (const auto& fault : faults) {
            row(fault.name, "create", [&] { startup(config, fault); });
            if (fault.rebuild)
                row(fault.name, "rebuildRenderer", [&] { rebuild(config, fault.inject, true); });
        }
    }
    void frameRows(const DesktopConfig& config, std::initializer_list<FrameFault> faults) {
        for (const auto& fault : faults)
            for (bool handled : {false, true})
                row(fault.name, handled ? "run / paint / post / close" : "run / paint without a handler",
                    [&] { frame(config, fault, handled); });
    }
};

inline void openGL(Sweep& sweep, DesktopConfig config) {
    sweep.startupRows(config, {
        {"failContext33", set(&RendererTestHooks::failContext33, true), StartupKind::contextVersion, true},
        {"fragmentShaderOverride", set(&RendererTestHooks::fragmentShaderOverride, "invalid shader"), StartupKind::shader, true},
        {"startupResourceError", set(&RendererTestHooks::startupResourceError, "Injected resource failure"), StartupKind::resource, true},
    });
    sweep.row("failRebuildOnce", "rebuildRenderer", [&] { rebuild(config, set(&RendererTestHooks::failRebuildOnce, true), true); });
    sweep.frameRows(config, {
        {"errorAtFrame (out of memory)", set(&RendererTestHooks::errorAtFrame, 1), FrameKind::frameError},
        {"injectedError (invalid operation)", [](auto& hooks) { hooks.errorAtFrame = 1; hooks.injectedError = 0x0502; }, FrameKind::frameError},
        {"resetAtFrame", set(&RendererTestHooks::resetAtFrame, 1), FrameKind::deviceReset},
        {"contextLostAtFrame", set(&RendererTestHooks::contextLostAtFrame, 1), FrameKind::deviceReset},
    });
    config.debugContext = DebugContext::on;
    sweep.row("hideRobustness", "create / run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::hideRobustness, true), [](const auto& caps, const auto&) {
            check(!caps.resetDetection, "Hidden OpenGL robustness disables reset detection");
        });
    });
    sweep.row("hideKhrDebug", "create / run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::hideKhrDebug, true), [](const auto& caps, const auto&) {
            check(!caps.debugMessages, "Hidden OpenGL debug extension disables debug messages");
        });
    });
    sweep.row("misuseOnce", "run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::misuseOnce, true));
    });
}

inline const char* surfaceFunction(DesktopBackendKind backend) {
    switch (backend) {
        case DesktopBackendKind::windows: return "vkCreateWin32SurfaceKHR";
        case DesktopBackendKind::wayland: return "vkCreateWaylandSurfaceKHR";
        case DesktopBackendKind::x11: return "vkCreateXcbSurfaceKHR";
    }
    throw std::runtime_error("Missing native backend");
}

inline void vulkan(Sweep& sweep, DesktopConfig config, const RendererCapabilities& baseline) {
    const char* surface = surfaceFunction(*config.backend);
    sweep.startupRows(config, {
        {"hideVulkanLoader", set(&RendererTestHooks::hideVulkanLoader, true), StartupKind::renderBackendUnavailable},
        {"hideInstanceProcAddr", set(&RendererTestHooks::hideInstanceProcAddr, true), StartupKind::contextCreation},
        {"hidePhysicalDevices", set(&RendererTestHooks::hidePhysicalDevices, true), StartupKind::renderBackendUnavailable},
        {"hideInstanceExtension", set(&RendererTestHooks::hideInstanceExtension, true), StartupKind::contextVersion},
        {"apiVersionOverride", set(&RendererTestHooks::apiVersionOverride, (1u << 22) | (1u << 12)), StartupKind::contextVersion},
        {"hideDynamicRendering", set(&RendererTestHooks::hideDynamicRendering, true), StartupKind::contextVersion},
        {"hideSynchronization2", set(&RendererTestHooks::hideSynchronization2, true), StartupKind::contextVersion},
        {"hideDeviceExtension", set(&RendererTestHooks::hideDeviceExtension, true), StartupKind::contextVersion},
        {"hideSurfaceFormats", set(&RendererTestHooks::hideSurfaceFormats, true), StartupKind::contextVersion},
        {"hidePresentQueue", set(&RendererTestHooks::hidePresentQueue, true), StartupKind::contextVersion},
        {"vkCreateInstance (incompatible driver)", vkResult("vkCreateInstance", -9), StartupKind::renderBackendUnavailable},
        {"vkCreateInstance", vkResult("vkCreateInstance", -3), StartupKind::contextCreation},
        {surface, vkResult(surface, -1000000000), StartupKind::contextCreation},
        {"vkGetPhysicalDeviceSurfaceSupportKHR", vkResult("vkGetPhysicalDeviceSurfaceSupportKHR", -3), StartupKind::contextCreation, true},
        {"vkCreateDevice", vkResult("vkCreateDevice", -3), StartupKind::contextCreation, true},
        {"vkCreateSwapchainKHR", vkResult("vkCreateSwapchainKHR", -3), StartupKind::contextCreation, true},
        {"vkCreateShaderModule", vkResult("vkCreateShaderModule", -3), StartupKind::shader, true},
        {"vkCreateGraphicsPipelines", vkResult("vkCreateGraphicsPipelines", -3), StartupKind::shader, true},
        {"vkCreateSampler", vkResult("vkCreateSampler", -2), StartupKind::resource, true},
        {"vkCreateDescriptorSetLayout", vkResult("vkCreateDescriptorSetLayout", -2), StartupKind::resource, true},
        {"vkCreatePipelineLayout", vkResult("vkCreatePipelineLayout", -2), StartupKind::resource, true},
        {"vkCreateImageView", vkResult("vkCreateImageView", -2), StartupKind::resource, true},
        {"vkCreateSemaphore", vkResult("vkCreateSemaphore", -2), StartupKind::resource, true},
    });
    sweep.row(surface, "rebuildRenderer after surface loss", [&] { rebuild(config, vkResult(surface, -1000000000), true, true); });
    sweep.row("failRebuildOnce", "rebuildRenderer", [&] { rebuild(config, set(&RendererTestHooks::failRebuildOnce, true), true); });
    sweep.row("failImageCreationOnce", "rebuildRenderer / restore atlas", [&] {
        rebuild(config, set(&RendererTestHooks::failImageCreationOnce, true), true);
    });
    sweep.row("hideBufferMemoryTypes", "rebuildRenderer / restore atlas", [&] {
        rebuild(config, set(&RendererTestHooks::hideBufferMemoryTypes, true), true);
    });
    sweep.row("zeroSurfaceExtent", "rebuildRenderer", [&] {
        rebuild(config, set(&RendererTestHooks::zeroSurfaceExtent, true), false);
    });
    for (const char* call : {"vkCreateBuffer", "vkAllocateMemory", "vkCreateCommandPool", "vkCreateFence"}) {
        sweep.row(call, "rebuildRenderer / restore atlas", [&] { rebuild(config, vkResult(call, -2), true); });
    }
    sweep.frameRows(config, {
        {"submitResultAtFrame (out of memory)", set(&RendererTestHooks::submitResultAtFrame, RendererTestHooks::FrameResult{1, -2}),
         FrameKind::frameError, u"vkQueueSubmit: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
        {"submitResultAtFrame (device lost)", set(&RendererTestHooks::submitResultAtFrame, RendererTestHooks::FrameResult{1, -4}),
         FrameKind::deviceReset, u"vkQueueSubmit: VK_ERROR_DEVICE_LOST"},
        {"submitResultAtFrame (unknown)", set(&RendererTestHooks::submitResultAtFrame, RendererTestHooks::FrameResult{1, -1234567}),
         FrameKind::frameError, u"vkQueueSubmit: VkResult(-1234567)"},
        {"fenceResultAtFrame", set(&RendererTestHooks::fenceResultAtFrame, 1), FrameKind::deviceReset,
         u"vkWaitForFences: VK_ERROR_DEVICE_LOST"},
        {"presentResultAtFrame (surface lost)", set(&RendererTestHooks::presentResultAtFrame, RendererTestHooks::FrameResult{1, -1000000000}),
         FrameKind::presentFailed, u"vkQueuePresentKHR: VK_ERROR_SURFACE_LOST_KHR"},
        {"acquireResultAtFrame (surface lost)", set(&RendererTestHooks::acquireResultAtFrame, RendererTestHooks::FrameResult{1, -1000000000}),
         FrameKind::presentFailed, u"vkAcquireNextImageKHR: VK_ERROR_SURFACE_LOST_KHR"},
        {"swapchainCreateResultOnce", [](auto& hooks) {
            hooks.acquireResultAtFrame = {1, -1000001004}; // VK_ERROR_OUT_OF_DATE_KHR
            hooks.swapchainCreateResultOnce = -2; // VK_ERROR_OUT_OF_DEVICE_MEMORY
        }, FrameKind::resourceFailed, u"vkCreateSwapchainKHR: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
        {"hideBufferMemoryTypes", set(&RendererTestHooks::hideBufferMemoryTypes, true), FrameKind::resourceFailed,
         u"No compatible Vulkan memory type", true},
        {"failImageCreationOnce", set(&RendererTestHooks::failImageCreationOnce, true), FrameKind::resourceFailed,
         u"vkCreateImage: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
        {"vkCreateBuffer", vkResult("vkCreateBuffer", -2), FrameKind::resourceFailed},
        {"vkAllocateMemory", vkResult("vkAllocateMemory", -2), FrameKind::resourceFailed},
        {"vkCreateCommandPool", vkResult("vkCreateCommandPool", -2), FrameKind::resourceFailed},
        {"vkCreateFence", vkResult("vkCreateFence", -2), FrameKind::resourceFailed},
        {"vkCreateDescriptorPool", vkResult("vkCreateDescriptorPool", -2), FrameKind::resourceFailed},
        {"vkCreateImageView", vkResult("vkCreateImageView", -2), FrameKind::resourceFailed},
        {"vkCreateSemaphore", vkResult("vkCreateSemaphore", -2), FrameKind::resourceFailed},
        {"vkAcquireNextImageKHR", vkResult("vkAcquireNextImageKHR", -1000000000), FrameKind::presentFailed},
        {"vkCreateSwapchainKHR", [](auto& hooks) {
            hooks.acquireResultAtFrame = {1, -1000001004}; // VK_ERROR_OUT_OF_DATE_KHR
            vkResult("vkCreateSwapchainKHR", -2)(hooks);
        }, FrameKind::resourceFailed},
    });
    constexpr int outOfDate = -1000001004, suboptimal = 1000001003, timeout = 2, notReady = 1;
    for (const auto& [name, injection] : std::initializer_list<std::pair<const char*, Inject>>{
        {"acquireResultAtFrame (timeout)", set(&RendererTestHooks::acquireResultAtFrame, RendererTestHooks::FrameResult{1, timeout})},
        {"acquireResultAtFrame (not ready)", set(&RendererTestHooks::acquireResultAtFrame, RendererTestHooks::FrameResult{1, notReady})},
        {"presentResultAtFrame (out of date)", set(&RendererTestHooks::presentResultAtFrame, RendererTestHooks::FrameResult{1, outOfDate})},
        {"presentResultAtFrame (suboptimal)", set(&RendererTestHooks::presentResultAtFrame, RendererTestHooks::FrameResult{1, suboptimal})},
        {"out of date twice", [](auto& hooks) {
            hooks.acquireResultAtFrame = {1, outOfDate};
            hooks.presentResultAtFrame = {1, outOfDate};
        }},
    }) sweep.row(name, "run / paint / post / close", [&] { paints(config, injection); });
    sweep.row("zeroSurfaceExtent", "create / run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::zeroSurfaceExtent, true), {}, true);
    });
    sweep.row("hideDebugUtils", "create / run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::hideDebugUtils, true), [](const auto& caps, const auto&) {
            check(!caps.debugMessages, "Hidden debug utils disables debug messages");
        });
    });
    auto debug = config;
    debug.debugContext = DebugContext::on;
    sweep.row("hideValidationLayer", "create / run / paint / post / close", [&] {
        paints(debug, set(&RendererTestHooks::hideValidationLayer, true), [](const auto& caps, const auto& events) {
            check(!caps.debugContext && std::any_of(events.begin(), events.end(), [](const auto& event) {
                return event.nativeMessage == "VK_LAYER_KHRONOS_validation is unavailable";
            }), "Hidden validation layer emits the startup diagnostic");
        });
    });
    if (baseline.debugMessages) {
        sweep.row("misuseOnce", "run / paint / post / close", [&] {
            paints(config, set(&RendererTestHooks::misuseOnce, true), [](const auto&, const auto& events) {
                check(std::any_of(events.begin(), events.end(), [](const auto& event) {
                    return event.code == "VUID-vkCmdSetScissor-scissorCount-arraylength";
                }), "Application receives the injected API misuse diagnostic");
            });
        });
    } else {
        std::cout << "Unavailable: misuseOnce requires debug context and validation messages\n";
    }
#ifdef _WIN32
    sweep.startupRows(config, {
        {"startupResourceError", set(&RendererTestHooks::startupResourceError, "Injected resource failure"), StartupKind::resource},
    });
#endif
    for (const auto& [name, injection, code] : {
        std::tuple<const char*, Inject, const char*>{"injectDebugMessage", set(&RendererTestHooks::injectDebugMessage, 1u), "Isoduo.TestMessage"},
        {"diagnoseDestruction", set(&RendererTestHooks::diagnoseDestruction, true), "Isoduo.DestroyInstance"},
    }) sweep.row(name, "create / run / paint / post / close", [&] {
        paints(config, injection, [&](const auto&, const auto& events) {
            check(std::any_of(events.begin(), events.end(), [&](const auto& event) {
                return event.code == code;
            }), "Application receives the injected debug utils diagnostic");
        });
    });
}

inline int run(DesktopConfig config) {
#ifndef _WIN32
    if (!config.backend && !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
        std::cout << "Renderer fault sweep requires a native desktop session\n";
        return 77;
    }
#endif
    config.width = 320;
    config.height = 240;
    config.platformAccessibilityEnabled = false;
    config.onDiagnostic = [](const RenderDiagnostic&) {};
    RendererCapabilities capabilities;
    {
        Fixture fixture;
        auto baseline = fixture.open(config, false);
        if (config.renderBackend == RenderBackendKind::vulkan && baseline.failure &&
            (baseline.failure->kind == StartupKind::renderBackendUnavailable || baseline.failure->kind == StartupKind::contextVersion)) {
            std::cerr << utf8FromUtf16(baseline.failure->message) << '\n';
            return 77;
        }
        checkOpened(baseline);
        capabilities = baseline.desktop->capabilities();
#ifdef _WIN32
        config.backend = DesktopBackendKind::windows;
#else
        config.backend = baseline.desktop->nativeWindow()->backend;
#endif
        check(capabilities.renderBackend == config.renderBackend, "Desktop uses the requested backend");
        std::cout << utf8FromUtf16(capabilities.renderer) << " / " << utf8FromUtf16(capabilities.vendor) << " / "
                  << utf8FromUtf16(capabilities.version) << " / debug context " << capabilities.debugContext << '\n';
    }
    Sweep sweep;
    sweep.row("public Desktop::create", "run / paint / post / close", [&] {
        Fixture fixture;
        auto result = fixture.open(config, false);
        checkOpened(result);
        Desktop& desktop = *result.desktop;
        Result posted{Status::failed, {}}, closed{Status::failed, {}};
        observePaint(desktop, [&] { if (!posted) closeAfterPostedWork(desktop, posted, closed); });
        check(runDesktop(desktop).status == Status::ok && posted && closed, "Public creation produces a usable desktop");
    });
    if (config.renderBackend == RenderBackendKind::vulkan) vulkan(sweep, config, capabilities);
    else openGL(sweep, config);
    std::cout << "Renderer fault sweep: " << sweep.passed << '/' << sweep.passed + sweep.failed << " rows passed\n";
    return sweep.failed ? 1 : 0;
}
}
