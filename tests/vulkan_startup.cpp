#include "check.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#ifdef _WIN32
#include "desktop_host.hpp"
#else
#include "desktop_linux_test.hpp"
#include <dlfcn.h>
#endif
#include <iostream>

using namespace isoduo;
using namespace isoduo::detail;
using test::check;
namespace {
auto page() { return layout::column({}); }

void defaultFrame(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
#ifdef _WIN32
    DesktopHost host(ui, webAppearance(), page, scroll, config);
    host.paint();
    check(host.frames() && host.capabilities().renderBackend == RenderBackendKind::openGL, "Default OpenGL frame");
    check(!GetModuleHandleW(L"vulkan-1.dll"), "VK01 Vulkan module loaded");
#else
    auto result = Desktop::create(ui, webAppearance(), page, scroll, config);
    check(bool(result.desktop), "Create default desktop");
    auto& desktop = *result.desktop;
    Frame frame;
    check(LinuxDesktopTest::compareFrame(desktop, 64, 64, 1, frame).passed &&
          desktop.capabilities().renderBackend == RenderBackendKind::openGL, "Default OpenGL frame");
    auto* loaded = dlopen("libvulkan.so.1", RTLD_NOLOAD | RTLD_NOW);
    if (loaded) dlclose(loaded);
    check(!loaded, "VK01 Vulkan module loaded");
#endif
}
int failures(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    config.renderBackend = RenderBackendKind::vulkan;
    auto create = [&](RendererTestHooks hooks) {
#ifdef _WIN32
        auto result = DesktopHost::createForTest(ui, webAppearance(), page, scroll, config, &hooks);
#else
        auto result = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
#endif
        check(!result.desktop && result.failure && result.status == Status::failed, "Expected a startup failure");
        return *result.failure;
    };
    RendererTestHooks loader;
    loader.hideVulkanLoader = true;
    auto absent = create(loader);
    check(absent.kind == StartupFailureKind::renderBackendUnavailable && !absent.message.empty(), "VK03 missing loader");
    auto base = create({});
    std::cout << utf8FromUtf16(base.message) << '\n';
    if (base.kind == StartupFailureKind::contextVersion || (base.kind == StartupFailureKind::renderBackendUnavailable &&
        base.message.find(u"not connected") == base.message.npos)) return 77;
    check(base.kind == StartupFailureKind::renderBackendUnavailable && base.message.find(u"not connected") != base.message.npos,
          "VK03 qualified device reaches native backend handoff");
    auto requirement = [&](RendererTestHooks hooks, std::u16string_view required, std::u16string_view actual = {}) {
        auto failure = create(hooks);
        check(failure.kind == StartupFailureKind::contextVersion && failure.required == required &&
              failure.actual == actual && !failure.message.empty(), "VK03 capability failure");
    };
    RendererTestHooks devices, instanceExtension, dynamicRendering, synchronization;
    devices.hidePhysicalDevices = true;
    check(create(devices).kind == StartupFailureKind::renderBackendUnavailable, "VK03 no physical devices");
    instanceExtension.hideInstanceExtension = true;
    requirement(instanceExtension, u"VK_KHR_surface");
    dynamicRendering.hideDynamicRendering = true;
    requirement(dynamicRendering, u"dynamicRendering");
    synchronization.hideSynchronization2 = true;
    requirement(synchronization, u"synchronization2");
    RendererTestHooks version;
    version.apiVersionOverride = (1u << 22) | (1u << 12);
    requirement(version, u"Vulkan 1.3", u"1.1");
    RendererTestHooks extension, queue, format;
    extension.hideDeviceExtension = queue.hidePresentQueue = format.hideSurfaceFormats = true;
    requirement(extension, u"VK_KHR_swapchain");
    requirement(queue, u"present queue family");
    requirement(format, u"B8G8R8A8_UNORM or R8G8B8A8_UNORM with SRGB_NONLINEAR");
    auto inject = [&](const char* call, int result, StartupFailureKind kind, const char* symbol) {
        RendererTestHooks hooks;
        hooks.startupResultOnce = {call, result};
        auto failure = create(hooks);
        check(failure.kind == kind && failure.message == utf16FromUtf8(std::string(call) + ": " + symbol), "VK05 startup result");
    };
    inject("vkCreateInstance", -9, StartupFailureKind::renderBackendUnavailable, "VK_ERROR_INCOMPATIBLE_DRIVER");
    inject("vkCreateInstance", -3, StartupFailureKind::contextCreation, "VK_ERROR_INITIALIZATION_FAILED");
#ifdef _WIN32
    const char* surfaceCall = "vkCreateWin32SurfaceKHR";
#else
    const char* surfaceCall = base.backend == DesktopBackendKind::wayland ? "vkCreateWaylandSurfaceKHR" : "vkCreateXcbSurfaceKHR";
#endif
    inject(surfaceCall, -1000000000, StartupFailureKind::contextCreation, "VK_ERROR_SURFACE_LOST_KHR");
    inject("vkCreateDevice", -4, StartupFailureKind::contextCreation, "VK_ERROR_DEVICE_LOST");
    inject("vkCreateDevice", -123456, StartupFailureKind::contextCreation, "VkResult(-123456)");
    return 0;
}
}
int main(int argc, char** argv) {
    try {
        DesktopConfig config;
        if (argc == 3) config.backend = std::string_view(argv[2]) == "wayland" ? DesktopBackendKind::wayland : DesktopBackendKind::x11;
        if (argc >= 2 && std::string_view(argv[1]) == "--default-frame") {
            defaultFrame(config);
            return 0;
        }
        return failures(config);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
