#pragma once
#include "check.hpp"
#ifdef _WIN32
#include "desktop_host.hpp"
#else
#include "desktop_linux_test.hpp"
#endif
#include "utf.hpp"
#include <glcorearb.h>
#include <vulkan/vulkan_core.h>
#include <cstdlib>
#include <initializer_list>
#include <tuple>

namespace test::rendererFaultSweep {
using namespace isoduo;
using namespace isoduo::detail;
using StartupKind = StartupFailureKind;
using FrameKind = RendererFailureKind;
using Inject = std::function<void(RendererTestHooks&)>;
using VerifyPaint = std::function<void(const RendererCapabilities&, const std::vector<RenderDiagnostic>&,
                                       const RendererTestHooks&)>;
using VerifyRebuild = std::function<void(const RendererCapabilities&, const RendererTestHooks&)>;
constexpr int unknownVkResult = -1234567;

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
    std::u16string_view required{};
    std::u16string_view actual{};
    std::u16string message{};
};

inline StartupFault vkStartupFault(const char* call, int result, StartupKind kind, bool rebuild,
                                   std::u16string_view resultName, const char* name = nullptr) {
    auto message = utf16FromUtf8(call) + u": ";
    message.append(resultName);
    return {name ? name : call, vkResult(call, result), kind, rebuild, {}, {}, std::move(message)};
}

struct FrameFault {
    const char* name;
    Inject inject;
    FrameKind kind;
    std::u16string_view message{};
    bool armAtCreate = false;
    std::optional<unsigned> nativeCode{};
};

inline void startup(const DesktopConfig& config, const StartupFault& fault) {
    Fixture fixture;
    fault.inject(fixture.hooks);
    auto result = fixture.open(config);
    check(result.status == Status::failed && !result.desktop && result.failure &&
          result.failure->kind == fault.kind && result.failure->backend == config.backend &&
          result.failure->required == fault.required && result.failure->actual == fault.actual &&
          !result.failure->message.empty() && result.failure->message.find(fault.message) != std::u16string::npos,
          "Startup fault returns the expected StartupFailure");
    // Failed creation must release the Ui claim so the public API can retry.
    checkOpened(fixture.open(config, false));
}

inline void rebuild(DesktopConfig config, const Inject& inject, bool expectFailure, bool replaceSurface = false,
                    std::u16string_view message = {}, const VerifyRebuild& verify = {}) {
    Fixture fixture;
    const bool vulkan = config.renderBackend == RenderBackendKind::vulkan;
    const auto kind = replaceSurface ? FrameKind::presentFailed : FrameKind::deviceReset;
    if (replaceSurface) fixture.hooks.presentResultAtFrame = {1, VK_ERROR_SURFACE_LOST_KHR};
    else if (vulkan) fixture.hooks.submitResultAtFrame = {1, VK_ERROR_DEVICE_LOST};
    else fixture.hooks.resetAtFrame = 1;

    int callbacks = 0, painted = 0;
    bool stillFailed = false, recovered = false;
    RendererCapabilities firstCapabilities;
    RendererTestHooks firstHooks;
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
        firstCapabilities = desktop.capabilities();
        firstHooks = fixture.hooks;
        stillFailed = desktop.rendererFailed();
        RendererTestHooks cleared;
        cleared.rebuildAttempts = fixture.hooks.rebuildAttempts;
        fixture.hooks = cleared;
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
          (!expectFailure || (!first.message.empty() && first.message.find(message) != std::u16string::npos)),
          "Rebuild returns the expected Result and failure state");
    check(fixture.hooks.rebuildAttempts == (expectFailure ? 2 : 1), "Rebuild attempts match explicit requests");
    check(recovered && painted > 0 && posted && closed && result.status == Status::ok && !result.failure,
          "Explicit recovery resumes painting and permits post and close");
    if (verify) verify(firstCapabilities, firstHooks);
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
               (fault.message.empty() || failure.message == fault.message) &&
               (!fault.nativeCode || failure.nativeCode == *fault.nativeCode);
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

enum class PaintInjection { atCreate, beforeRun, afterFirstPaint };

inline void paints(DesktopConfig config, const Inject& inject, const VerifyPaint& verify,
                   PaintInjection when = PaintInjection::atCreate) {
    Fixture fixture;
    if (when == PaintInjection::atCreate) inject(fixture.hooks);
    int callbacks = 0, painted = 0;
    std::vector<RenderDiagnostic> events;
    config.onDiagnostic = [&](const RenderDiagnostic& event) { events.push_back(event); };
    config.onRendererFailed = [&](Desktop& desktop, const RendererFailure&) { ++callbacks; desktop.close(); };
    auto created = fixture.open(config);
    checkOpened(created);
    Desktop& desktop = *created.desktop;
    const auto capabilities = desktop.capabilities();
    if (when == PaintInjection::atCreate)
        check(!fixture.hooks.zeroSurfaceExtentOnce, "Creation consumes the zero extent hook");
    if (when == PaintInjection::beforeRun)
        inject(fixture.hooks);
    Result posted{Status::failed, {}}, closed{Status::failed, {}};
    Result warmup{Status::ok, {}};
    observePaint(desktop, [&] {
        ++painted;
        if (when == PaintInjection::afterFirstPaint && painted == 1) {
            inject(fixture.hooks);
            warmup = fixture.label.setWritingMode(WritingMode::vertical);
            if (!warmup) desktop.close();
            return;
        }
        const int finalPaint = when == PaintInjection::afterFirstPaint ? 2 : 1;
        if (painted == finalPaint) closeAfterPostedWork(desktop, posted, closed);
    });
    const auto result = runDesktop(desktop);
    check(painted > 0 && warmup && callbacks == 0 && posted && closed && result.status == Status::ok && !result.failure,
          "Renderer presents without entering failure and accepts post and close");
    verify(capabilities, events, fixture.hooks);
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
                row(fault.name, "rebuildRenderer", [&] { rebuild(config, fault.inject, true, false, fault.message); });
        }
    }
    void frameRows(const DesktopConfig& config, std::initializer_list<FrameFault> faults) {
        for (const auto& fault : faults)
            for (bool handled : {false, true})
                row(fault.name, handled ? "run / paint / post / close" : "run / paint without a handler",
                    [&] { frame(config, fault, handled); });
    }
};

// Entry points for the matrices below:
// - Failed create has no Desktop to run, rebuild, post to, or close. The same Ui
//   must be reusable. Rebuild on a healthy or closed Desktop never reaches a hook.
// - Run drives native paint. Runtime faults cover handled and unhandled run;
//   handled rows post and close while failed. Recovery rows post and close after
//   presentation. Post queues work and close requests shutdown; neither calls
//   renderer startup or draw. Close can expose the Vulkan destruction diagnostic
//   when run releases the retained instance.
// - OpenGL context, shader, startup resource and extension flags are read at
//   both context creations. Its frame, reset and misuse hooks require a draw.
// - Vulkan rebuild retains the loader, instance, debug messenger and normally
//   the surface. Loader/proc lookup, instance creation/extensions, validation
//   layer and debug-utils filtering apply only at create. Surface creation
//   applies at create and at rebuild after surface loss.
// - Vulkan reselects a device and recreates device resources and the swapchain
//   on rebuild. Atlas/frame resources are lazy: image, buffer, allocation,
//   command pool, and fence faults apply on first paint and atlas restore,
//   descriptor pool on paint alone. Query pools require the performance test
//   seam to enable timing and have no ordinary desktop lifecycle entry point.
//   The acquire/submit/present/fence-at-frame and diagnostic injection hooks
//   require a draw. The runtime swapchain one-shot is installed after initial
//   swapchain creation; the function-table fault covers all swapchain creations.
// - failRebuildOnce is specific to rebuild. A startupResourceError at the
//   Vulkan host boundary is Win32-only; Linux has no corresponding interception.
//   Extension suppression requires the original capability, and Vulkan misuse
//   needs active Khronos validation and debug utils.

inline RendererCapabilities capabilitiesFor(const DesktopConfig& config) {
    Fixture fixture;
    auto created = fixture.open(config, false);
    checkOpened(created);
    return created.desktop->capabilities();
}

inline void rebuildOnce(Sweep& sweep, const DesktopConfig& config, std::u16string_view failureMessage = {}) {
    const auto inject = set(&RendererTestHooks::failRebuildOnce, true);
    sweep.row("failRebuildOnce", "create / run / paint / post / close", [&] {
        paints(config, inject, [](const auto&, const auto&, const auto& hooks) {
            check(hooks.failRebuildOnce, "Initial creation leaves the rebuild-only hook armed");
        });
    });
    sweep.row("failRebuildOnce", "rebuildRenderer", [&] {
        rebuild(config, inject, true, false, failureMessage, [](const auto&, const auto& hooks) {
            check(!hooks.failRebuildOnce, "The first rebuild consumes the one-shot fault");
        });
    });
}

inline void openGL(Sweep& sweep, DesktopConfig config) {
    rebuildOnce(sweep, config);
    sweep.startupRows(config, {
        {"failContext33", set(&RendererTestHooks::failContext33, true), StartupKind::contextVersion, true,
         u"OpenGL 3.3 core", u"2.1", u"OpenGL 3.3 core is required"},
        {"fragmentShaderOverride", set(&RendererTestHooks::fragmentShaderOverride, "invalid shader"), StartupKind::shader, true},
        {"startupResourceError", set(&RendererTestHooks::startupResourceError, "Injected resource failure"),
         StartupKind::resource, true, {}, {}, u"Injected resource failure"},
    });
    sweep.frameRows(config, {
        {"errorAtFrame (out of memory)", set(&RendererTestHooks::errorAtFrame, 1), FrameKind::frameError,
         {}, false, GL_OUT_OF_MEMORY},
        {"injectedError (invalid operation)", [](auto& hooks) { hooks.errorAtFrame = 1; hooks.injectedError = GL_INVALID_OPERATION; },
         FrameKind::frameError, {}, false, GL_INVALID_OPERATION},
        {"resetAtFrame", set(&RendererTestHooks::resetAtFrame, 1), FrameKind::deviceReset, {}, false, GL_GUILTY_CONTEXT_RESET},
        {"contextLostAtFrame", set(&RendererTestHooks::contextLostAtFrame, 1), FrameKind::deviceReset, {}, false, GL_CONTEXT_LOST},
    });
    config.debugContext = DebugContext::on;
    const auto baseline = capabilitiesFor(config);
    for (const auto& [name, hook, capability] : {
        std::tuple{"hideRobustness", &RendererTestHooks::hideRobustness, &RendererCapabilities::resetDetection},
        std::tuple{"hideKhrDebug", &RendererTestHooks::hideKhrDebug, &RendererCapabilities::debugMessages},
    }) {
        if (!(baseline.*capability)) {
            std::cout << "Unavailable: " << name << " requires the corresponding OpenGL capability\n";
            continue;
        }
        const auto inject = set(hook, true);
        auto verifyHidden = [&](const RendererCapabilities& caps) {
            check(!(caps.*capability), "The injected extension filter disables the OpenGL capability");
        };
        sweep.row(name, "create / run / paint / post / close", [&] {
            paints(config, inject, [&](const auto& caps, const auto&, const auto&) { verifyHidden(caps); });
        });
        sweep.row(name, "rebuildRenderer", [&] {
            rebuild(config, inject, false, false, {}, [&](const auto& caps, const auto&) { verifyHidden(caps); });
        });
    }
    sweep.row("misuseOnce", "run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::misuseOnce, true), [](const auto&, const auto&, const auto& hooks) {
            check(!hooks.misuseOnce, "OpenGL consumes the API misuse hook");
        }, PaintInjection::beforeRun);
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
        {"hideVulkanLoader", set(&RendererTestHooks::hideVulkanLoader, true), StartupKind::renderBackendUnavailable,
         false, {}, {}, u"Vulkan loader unavailable"},
        {"hideInstanceProcAddr", set(&RendererTestHooks::hideInstanceProcAddr, true), StartupKind::contextCreation,
         false, {}, {}, u"Missing vkGetInstanceProcAddr"},
        {"hidePhysicalDevices", set(&RendererTestHooks::hidePhysicalDevices, true), StartupKind::renderBackendUnavailable,
         true, {}, {}, u"No Vulkan physical devices"},
        {"hideInstanceExtension", set(&RendererTestHooks::hideInstanceExtension, true), StartupKind::contextVersion,
         false, u"VK_KHR_surface", {}, u"VK_KHR_surface"},
        {"apiVersionOverride", set(&RendererTestHooks::apiVersionOverride, VK_API_VERSION_1_1), StartupKind::contextVersion,
         true, u"Vulkan 1.3", u"1.1", u"Vulkan 1.3"},
        {"hideDynamicRendering", set(&RendererTestHooks::hideDynamicRendering, true), StartupKind::contextVersion,
         true, u"dynamicRendering", {}, u"dynamicRendering"},
        {"hideSynchronization2", set(&RendererTestHooks::hideSynchronization2, true), StartupKind::contextVersion,
         true, u"synchronization2", {}, u"synchronization2"},
        {"hideDeviceExtension", set(&RendererTestHooks::hideDeviceExtension, true), StartupKind::contextVersion,
         true, u"VK_KHR_swapchain", {}, u"VK_KHR_swapchain"},
        {"hideSurfaceFormats", set(&RendererTestHooks::hideSurfaceFormats, true), StartupKind::contextVersion,
         true, u"B8G8R8A8_UNORM or R8G8B8A8_UNORM with SRGB_NONLINEAR", {}, u"SRGB_NONLINEAR"},
        {"hidePresentQueue", set(&RendererTestHooks::hidePresentQueue, true), StartupKind::contextVersion,
         true, u"present queue family", {}, u"present queue family"},
        vkStartupFault("vkCreateInstance", VK_ERROR_INCOMPATIBLE_DRIVER, StartupKind::renderBackendUnavailable,
                       false, u"VK_ERROR_INCOMPATIBLE_DRIVER", "vkCreateInstance (incompatible driver)"),
        vkStartupFault("vkCreateInstance", VK_ERROR_INITIALIZATION_FAILED, StartupKind::contextCreation,
                       false, u"VK_ERROR_INITIALIZATION_FAILED"),
        vkStartupFault(surface, VK_ERROR_SURFACE_LOST_KHR, StartupKind::contextCreation, false, u"VK_ERROR_SURFACE_LOST_KHR"),
    });
    for (const auto& [call, kind] : {
        std::pair{"vkGetPhysicalDeviceSurfaceSupportKHR", StartupKind::contextCreation},
        std::pair{"vkCreateDevice", StartupKind::contextCreation},
        std::pair{"vkCreateSwapchainKHR", StartupKind::contextCreation},
        std::pair{"vkCreateShaderModule", StartupKind::shader},
        std::pair{"vkCreateGraphicsPipelines", StartupKind::shader},
    }) sweep.startupRows(config, {vkStartupFault(call, VK_ERROR_INITIALIZATION_FAILED, kind, true,
                                                 u"VK_ERROR_INITIALIZATION_FAILED")});
    for (const char* call : {"vkCreateSampler", "vkCreateDescriptorSetLayout", "vkCreatePipelineLayout",
                             "vkCreateImageView", "vkCreateSemaphore"}) {
        sweep.startupRows(config, {vkStartupFault(call, VK_ERROR_OUT_OF_DEVICE_MEMORY, StartupKind::resource,
                                                  true, u"VK_ERROR_OUT_OF_DEVICE_MEMORY")});
    }
    sweep.row(surface, "rebuildRenderer after surface loss", [&] {
        rebuild(config, vkResult(surface, VK_ERROR_SURFACE_LOST_KHR), true, true, u"VK_ERROR_SURFACE_LOST_KHR");
    });
    rebuildOnce(sweep, config, u"vkCreateDevice: VK_ERROR_DEVICE_LOST");
    sweep.row("failImageCreationOnce", "rebuildRenderer / restore atlas", [&] {
        rebuild(config, set(&RendererTestHooks::failImageCreationOnce, true), true, false, u"vkCreateImage: VK_ERROR_OUT_OF_DEVICE_MEMORY");
    });
    sweep.row("hideBufferMemoryTypes", "rebuildRenderer / restore atlas", [&] {
        rebuild(config, set(&RendererTestHooks::hideBufferMemoryTypes, true), true, false, u"No compatible Vulkan memory type");
    });
    sweep.row("zeroSurfaceExtentOnce", "rebuildRenderer", [&] {
        rebuild(config, set(&RendererTestHooks::zeroSurfaceExtentOnce, true), false, false, {}, [](const auto&, const auto& hooks) {
            check(!hooks.zeroSurfaceExtentOnce, "Rebuild consumes the zero extent hook");
        });
    });
    for (const char* call : {"vkCreateBuffer", "vkAllocateMemory", "vkCreateCommandPool", "vkCreateFence", "vkCreateImage"}) {
        sweep.row(call, "rebuildRenderer / restore atlas", [&] {
            const auto fault = vkStartupFault(call, VK_ERROR_OUT_OF_DEVICE_MEMORY, StartupKind::resource,
                                              true, u"VK_ERROR_OUT_OF_DEVICE_MEMORY");
            rebuild(config, fault.inject, true, false, fault.message);
        });
    }
    sweep.frameRows(config, {
        {"submitResultAtFrame (out of memory)", set(&RendererTestHooks::submitResultAtFrame, RendererTestHooks::FrameResult{1, VK_ERROR_OUT_OF_DEVICE_MEMORY}),
         FrameKind::frameError, u"vkQueueSubmit: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
        {"submitResultAtFrame (device lost)", set(&RendererTestHooks::submitResultAtFrame, RendererTestHooks::FrameResult{1, VK_ERROR_DEVICE_LOST}),
         FrameKind::deviceReset, u"vkQueueSubmit: VK_ERROR_DEVICE_LOST", false, static_cast<unsigned>(VK_ERROR_DEVICE_LOST)},
        {"submitResultAtFrame (unknown)", set(&RendererTestHooks::submitResultAtFrame, RendererTestHooks::FrameResult{1, unknownVkResult}),
         FrameKind::frameError, u"vkQueueSubmit: VkResult(-1234567)"},
        {"fenceResultAtFrame", set(&RendererTestHooks::fenceResultAtFrame, 1), FrameKind::deviceReset,
         u"vkWaitForFences: VK_ERROR_DEVICE_LOST"},
        {"presentResultAtFrame (surface lost)", set(&RendererTestHooks::presentResultAtFrame, RendererTestHooks::FrameResult{1, VK_ERROR_SURFACE_LOST_KHR}),
         FrameKind::presentFailed, u"vkQueuePresentKHR: VK_ERROR_SURFACE_LOST_KHR"},
        {"acquireResultAtFrame (surface lost)", set(&RendererTestHooks::acquireResultAtFrame, RendererTestHooks::FrameResult{1, VK_ERROR_SURFACE_LOST_KHR}),
         FrameKind::presentFailed, u"vkAcquireNextImageKHR: VK_ERROR_SURFACE_LOST_KHR"},
        {"swapchainCreateResultOnce", [](auto& hooks) {
            hooks.acquireResultAtFrame = {1, VK_ERROR_OUT_OF_DATE_KHR};
            hooks.swapchainCreateResultOnce = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }, FrameKind::resourceFailed, u"vkCreateSwapchainKHR: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
        {"hideBufferMemoryTypes", set(&RendererTestHooks::hideBufferMemoryTypes, true), FrameKind::resourceFailed,
         u"No compatible Vulkan memory type", true},
        {"failImageCreationOnce", set(&RendererTestHooks::failImageCreationOnce, true), FrameKind::resourceFailed,
         u"vkCreateImage: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
        {"vkAcquireNextImageKHR", vkResult("vkAcquireNextImageKHR", VK_ERROR_SURFACE_LOST_KHR), FrameKind::presentFailed,
         u"vkAcquireNextImageKHR: VK_ERROR_SURFACE_LOST_KHR"},
        {"vkCreateSwapchainKHR", [](auto& hooks) {
            hooks.acquireResultAtFrame = {1, VK_ERROR_OUT_OF_DATE_KHR};
            vkResult("vkCreateSwapchainKHR", VK_ERROR_OUT_OF_DEVICE_MEMORY)(hooks);
        }, FrameKind::resourceFailed, u"vkCreateSwapchainKHR: VK_ERROR_OUT_OF_DEVICE_MEMORY"},
    });
    for (const char* call : {"vkCreateBuffer", "vkAllocateMemory", "vkCreateCommandPool", "vkCreateFence",
                             "vkCreateImage", "vkCreateDescriptorPool", "vkCreateImageView", "vkCreateSemaphore"}) {
        const auto fault = vkStartupFault(call, VK_ERROR_OUT_OF_DEVICE_MEMORY, StartupKind::resource,
                                          false, u"VK_ERROR_OUT_OF_DEVICE_MEMORY");
        sweep.frameRows(config, {{call, fault.inject, FrameKind::resourceFailed, fault.message}});
    }
    for (const auto& [name, hook, result] : {
        std::tuple{"acquireResultAtFrame (timeout)", &RendererTestHooks::acquireResultAtFrame, VK_TIMEOUT},
        std::tuple{"acquireResultAtFrame (not ready)", &RendererTestHooks::acquireResultAtFrame, VK_NOT_READY},
        std::tuple{"presentResultAtFrame (out of date)", &RendererTestHooks::presentResultAtFrame, VK_ERROR_OUT_OF_DATE_KHR},
        std::tuple{"presentResultAtFrame (suboptimal)", &RendererTestHooks::presentResultAtFrame, VK_SUBOPTIMAL_KHR},
    }) sweep.row(name, "run / paint / post / close", [&] {
        paints(config, set(hook, RendererTestHooks::FrameResult{1, result}), [&](const auto&, const auto&, const auto& hooks) {
            check((hooks.*hook).frame == 0, "Vulkan consumes the presentation hook before recovery");
        }, PaintInjection::beforeRun);
    });
    sweep.row("out of date twice", "run / paint / post / close", [&] {
        int faultDraw = 0;
        paints(config, [&](auto& hooks) {
            // Warm up first so the fault draw starts with a current swapchain.
            faultDraw = hooks.vulkanDrawAttempts + 1;
            hooks.acquireResultAtFrame = {faultDraw, VK_ERROR_OUT_OF_DATE_KHR};
            hooks.presentResultAtFrame = {faultDraw, VK_ERROR_OUT_OF_DATE_KHR};
        }, [&](const auto&, const auto&, const auto& hooks) {
            check(hooks.acquireResultAtFrame.frame == 0 && hooks.presentResultAtFrame.frame == 0,
                  "The same draw consumes both out-of-date results");
            check(hooks.vulkanDrawAttempts > faultDraw, "A later draw presents after both out-of-date results");
        }, PaintInjection::afterFirstPaint);
    });
    sweep.row("zeroSurfaceExtentOnce", "create / run / paint / post / close", [&] {
        paints(config, set(&RendererTestHooks::zeroSurfaceExtentOnce, true), [](const auto&, const auto&, const auto& hooks) {
            check(hooks.compositeAlphaMask != 0, "An allocated swapchain replaces the deferred one");
        });
    });
    sweep.row("zeroSurfaceExtentOnce", "run / paint / post / close", [&] {
        paints(config, [](auto& hooks) {
            hooks.acquireResultAtFrame = {hooks.vulkanDrawAttempts + 1, VK_ERROR_OUT_OF_DATE_KHR};
            hooks.zeroSurfaceExtentOnce = true;
        }, [](const auto&, const auto&, const auto& hooks) {
            check(hooks.acquireResultAtFrame.frame == 0 && !hooks.zeroSurfaceExtentOnce,
                  "The out-of-date swapchain encounters zero extent on the paint path");
        }, PaintInjection::afterFirstPaint);
    });
    auto hasDiagnostic = [](const auto& events, std::string_view code) {
        return std::any_of(events.begin(), events.end(), [&](const auto& event) { return event.code == code; });
    };
    sweep.row("hideDebugUtils", "create / run / paint / post / close", [&] {
        paints(config, [](auto& hooks) {
            hooks.hideDebugUtils = true;
            hooks.diagnoseDestruction = true;
        }, [&](const auto& caps, const auto& events, const auto&) {
            check(!caps.debugMessages && !hasDiagnostic(events, "Isoduo.DestroyInstance"),
                  "Hidden debug utils suppresses the destruction diagnostic");
        });
    });
    auto debug = config;
    debug.debugContext = DebugContext::on;
    const auto debugBaseline = baseline.debugContext ? baseline : capabilitiesFor(debug);
    if (debugBaseline.debugContext) sweep.row("hideValidationLayer", "create / run / paint / post / close", [&] {
        paints(debug, set(&RendererTestHooks::hideValidationLayer, true), [](const auto& caps, const auto& events, const auto&) {
            check(!caps.debugContext && !caps.debugMessages && std::any_of(events.begin(), events.end(), [](const auto& event) {
                return event.nativeMessage == "VK_LAYER_KHRONOS_validation is unavailable";
            }), "Hidden validation layer emits the startup diagnostic");
        });
    });
    else std::cout << "Unavailable: hideValidationLayer requires installed Khronos validation\n";
    if (debugBaseline.debugMessages) {
        sweep.row("misuseOnce", "run / paint / post / close", [&] {
            paints(debug, set(&RendererTestHooks::misuseOnce, true), [&](const auto&, const auto& events, const auto&) {
                check(hasDiagnostic(events, "VUID-vkCmdSetScissor-scissorCount-arraylength"),
                      "Application receives the injected API misuse diagnostic");
            }, PaintInjection::beforeRun);
        });
    } else {
        std::cout << "Unavailable: misuseOnce requires debug context and validation messages\n";
    }
#ifdef _WIN32
    sweep.startupRows(config, {
        {"startupResourceError", set(&RendererTestHooks::startupResourceError, "Injected resource failure"),
         StartupKind::resource, false, {}, {}, u"Injected resource failure"},
    });
#endif
    for (const auto& [name, injection, code] : {
        std::tuple<const char*, Inject, const char*>{"injectDebugMessage", set(&RendererTestHooks::injectDebugMessage, 1u), "Isoduo.TestMessage"},
        {"diagnoseDestruction", set(&RendererTestHooks::diagnoseDestruction, true), "Isoduo.DestroyInstance"},
    }) sweep.row(name, "create / run / paint / post / close", [&] {
        paints(config, injection, [&](const auto&, const auto& events, const auto&) {
            check(hasDiagnostic(events, code), "Application receives the injected debug utils diagnostic");
        }, std::string_view(name) == "diagnoseDestruction" ? PaintInjection::atCreate : PaintInjection::beforeRun);
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
