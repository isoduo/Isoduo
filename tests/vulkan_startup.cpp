#include "check.hpp"
#include "renderer_vulkan.hpp"
#include "draw_list_fixture.hpp"
#include "glyph_atlas.hpp"
#include "utf.hpp"
#ifdef _WIN32
#include "desktop_host.hpp"
#else
#include "desktop_linux_test.hpp"
#include <dlfcn.h>
#include "linux_window.hpp"
#include <X11/Xlib.h>
#undef Status
#endif
#include <iostream>

using namespace isoduo;
using namespace isoduo::detail;
using test::check;
namespace {
auto page() { return layout::column({}); }

bool unavailable(const CreateResult& result) {
    return result.status == Status::failed && !result.desktop && result.failure &&
           result.failure->kind == StartupFailureKind::renderBackendUnavailable;
}

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
#ifdef _WIN32
void missingLoader(DesktopConfig config) {
    auto loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (loader) {
        FreeLibrary(loader);
        check(false, "VK02 requires vulkan-1.dll to be absent from System32, but it loaded");
    }
    check(GetLastError() == ERROR_MOD_NOT_FOUND, "VK02 expected ERROR_MOD_NOT_FOUND");
    Ui ui;
    ScrollState scroll;
    config.title = u"VK02 missing Vulkan loader";
    config.renderBackend = RenderBackendKind::vulkan;
    auto result = Desktop::create(ui, webAppearance(), page, scroll, config);
    check(unavailable(result),
          "VK02 expected renderBackendUnavailable");
    check(result.failure->message.find(u"126") != result.failure->message.npos, "VK02 native load error");
    check(!FindWindowW(nullptr, L"VK02 missing Vulkan loader"), "VK02 left a native window");
}

void presentation(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    RendererTestHooks hooks;
    DesktopHost host(ui,webAppearance(),page,scroll,config,false,true,&hooks);
    ShowWindow(host.window(),SW_SHOWNOACTIVATE);
    host.pump(); host.paint();
    auto report=[&](const char* phase) {
        std::cout << phase << " frames=" << host.frames() << " acquire=" << hooks.lastAcquire
            << " present=" << hooks.lastPresent << " FIFO present (ms)=" << hooks.presentMs
            << " compositeAlpha mask=" << hooks.compositeAlphaMask << '\n';
    };
    check(host.frames()>0 && !host.rendererFailed(),"Vulkan first frame");
    report("visible");
    ShowWindow(host.window(),SW_MINIMIZE);
    int frames=host.frames();
    for (int i=0;i<4;++i) { host.pump(); host.paint(); Sleep(20); }
    report("minimized");
    check(host.frames()==frames && !host.rendererFailed() && !hooks.retryTimers,"VK09 minimized desktop stays idle");
    ShowWindow(host.window(),SW_RESTORE);
    host.pump(); host.paint();
    check(host.frames()>frames && host.compareGpu(64,64,1).passed,"VK09 restore draws a matching frame");
    report("restored");
    RECT bounds{}; GetWindowRect(host.window(),&bounds);
    HWND cover=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP|WS_VISIBLE,
        bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(cover!=nullptr,"Create covering window");
    host.paint(); report("covered");
    DestroyWindow(cover);
    host.clientSize(473,321); host.pump(); host.paint(); report("resized");
    check(!host.rendererFailed() && host.compareGpu(473,321,1).passed,"Resize and occlusion keep renderer usable");
}
#else
void x11Presentation(DesktopConfig config) {
    Ui ui;
    ScrollState scroll;
    RendererTestHooks hooks;
    hooks.startupResultOnce = {"vkAcquireNextImageKHR", 2}; // VK_TIMEOUT
    unsigned frames = 0, recoveries = 0;
    config.width = 320;
    config.height = 240;
    config.onRendererFailed = [&](Desktop& desktop, const RendererFailure& failure) {
        check(failure.kind == RendererFailureKind::deviceReset && desktop.rendererFailed(), "Resize reports device loss");
        check(desktop.rebuildRenderer() && desktop.capabilities().renderBackend == RenderBackendKind::vulkan,
              "Rebuild the same Vulkan desktop");
        ++recoveries;
    };
    auto result = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, config, &hooks);
    check(bool(result.desktop), "Create X11 presentation fixture");
    auto& desktop = *result.desktop;
    LinuxDesktopTest::observeFrames(desktop, [&](const Frame&, const LinuxClientGeometry& geometry) {
        if (++frames == 1) {
            check(!hooks.startupResultOnce, "The timed-out acquire retries and presents");
            hooks.startupResultOnce = {"vkCreateSwapchainKHR", -4}; // VK_ERROR_DEVICE_LOST
            auto& window = *desktop.nativeWindow();
            auto display = static_cast<Display*>(window.display);
            XResizeWindow(display, window.windowId, 451, 319);
            XFlush(display);
        } else if (geometry.width == 451 && geometry.height == 319) {
            Frame frame;
            check(recoveries == 1 && LinuxDesktopTest::compareFrame(desktop, 451, 319, geometry.scale, frame).passed,
                  "Resize recreates the chain and the recovered frame matches the CPU image");
            desktop.close();
        }
    });
    check(desktop.run().status == Status::ok && recoveries == 1, "Finish the X11 resize and rebuild trial");
}
#endif
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
        if (result.failure->kind == StartupFailureKind::renderBackendUnavailable)
            check(unavailable(result), "Unavailable backend fails startup");
        return *result.failure;
    };
    RendererTestHooks loader;
    loader.hideVulkanLoader = true;
    auto absent = create(loader);
    check(absent.kind == StartupFailureKind::renderBackendUnavailable && absent.message.find(u"Vulkan loader unavailable: ") == 0, "VK03 missing loader");
    {
        auto retry = config;
        retry.renderBackend = RenderBackendKind::openGL;
        auto replacement = Desktop::create(ui, webAppearance(), page, scroll, retry);
        check(replacement.desktop && replacement.desktop->capabilities().renderBackend == RenderBackendKind::openGL,
              "HO07 OpenGL can host the same Ui after Vulkan startup fails");
    }
#ifndef _WIN32
    config.backend = absent.backend;
#endif
    {
        auto result = Desktop::create(ui, webAppearance(), page, scroll, config);
        if (result.failure && (result.failure->kind == StartupFailureKind::contextVersion ||
            (result.failure->kind == StartupFailureKind::renderBackendUnavailable &&
             result.failure->message.find(u"not connected") == result.failure->message.npos))) return 77;
        check(bool(result.desktop), "Vulkan creates a drawable desktop");
        auto caps = result.desktop->capabilities();
        check(caps.renderBackend == RenderBackendKind::vulkan && caps.version.starts_with(u"Vulkan ") &&
            !caps.renderer.empty() && !caps.vendor.empty() && caps.resetDetection, "VK04 Vulkan capabilities");
        std::cout << utf8FromUtf16(caps.renderer) << " / " << utf8FromUtf16(caps.vendor) << " / " << utf8FromUtf16(caps.version) << '\n';
    }
    {
        bool delivered = false;
        auto options = config;
        options.onDiagnostic = [&](const RenderDiagnostic& event) {
            if (event.code == "Isoduo.DestroyInstance") delivered = true;
        };
        RendererTestHooks hooks;
        hooks.diagnoseDestruction = true;
#ifdef _WIN32
        auto result = DesktopHost::createForTest(ui, webAppearance(), page, scroll, options, &hooks);
#else
        auto result = LinuxDesktopTest::create(ui, webAppearance(), page, scroll, options, &hooks);
#endif
        check(bool(result.desktop), "Create shutdown diagnostic fixture");
        result.desktop.reset();
        check(delivered, "Deliver the Vulkan instance destruction diagnostic");
    }
    RendererTestHooks broken;
    broken.hideInstanceProcAddr = true;
    auto missingSymbol = create(broken);
    check(missingSymbol.kind == StartupFailureKind::contextCreation &&
          missingSymbol.message.find(u"vkGetInstanceProcAddr") != missingSymbol.message.npos,
          "Broken loader identifies its missing entry point");
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
    presentation(config);
#else
    if (config.backend == DesktopBackendKind::x11) x11Presentation(config);
#endif
    inject("vkCreateSwapchainKHR", -3, StartupFailureKind::contextCreation, "VK_ERROR_INITIALIZATION_FAILED");
    inject("vkCreateShaderModule", -3, StartupFailureKind::shader, "VK_ERROR_INITIALIZATION_FAILED");
    inject("vkCreateGraphicsPipelines", -3, StartupFailureKind::shader, "VK_ERROR_INITIALIZATION_FAILED");
    inject("vkCreateSampler", -2, StartupFailureKind::resource, "VK_ERROR_OUT_OF_DEVICE_MEMORY");
#ifdef _WIN32
    const char* surfaceCall = "vkCreateWin32SurfaceKHR";
#else
    const char* surfaceCall = config.backend == DesktopBackendKind::wayland ? "vkCreateWaylandSurfaceKHR" : "vkCreateXcbSurfaceKHR";
#endif
    inject(surfaceCall, -1000000000, StartupFailureKind::contextCreation, "VK_ERROR_SURFACE_LOST_KHR");
    inject("vkCreateDevice", -4, StartupFailureKind::contextCreation, "VK_ERROR_DEVICE_LOST");
    inject("vkCreateDevice", -123456, StartupFailureKind::contextCreation, "VkResult(-123456)");
    return 0;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--offscreen") {
            auto list = test::layeredDrawList();
            try {
                auto image = renderVulkanOffscreen(list, 8, 6);
                auto comparison = compareImages(renderCpu(list, 8, 6), image, {});
                check(comparison.regionsOk && comparison.differentPixels == 0, "VO01 layered draw list");
                GlyphAtlas atlas(3,1);
                RecordingCanvas canvas(8,1,{0,0,0});
                canvas.beginGroup("eviction", false, false);
                for (unsigned x=0;x<8;++x) {
                    canvas.glyphDeferred({255,255,255},[&,x]{
                        auto generation=atlas.batches();
                        auto glyph=atlas.get({1,x,1},[x]{return GlyphBitmap{1,1,0,0,{static_cast<unsigned char>(32+x*30)}};});
                        return ResolvedGlyph{{int(x),0,1,1},glyph.region,atlas.batches(),atlas.batches()!=generation};
                    });
                }
                canvas.endGroup();
                auto striped=renderVulkanOffscreen(canvas.list,8,1);
                auto stripes=compareImages(renderCpu(canvas.list,8,1),striped,{});
                check(atlas.batches()>0 && stripes.regionsOk && stripes.differentPixels==0, "VO01 replay across atlas eviction");
            } catch (const StartupError& error) {
                std::cerr << utf8FromUtf16(error.failure.message) << '\n';
                return error.failure.kind == StartupFailureKind::renderBackendUnavailable ||
                    error.failure.kind == StartupFailureKind::contextVersion ? 77 : 1;
            }
            return 0;
        }
        DesktopConfig config;
#ifdef _WIN32
        if (argc == 2 && std::string_view(argv[1]) == "--no-loader") {
            missingLoader(config);
            return 0;
        }
#endif
        if (argc == 3) config.backend = std::string_view(argv[2]) == "wayland" ? DesktopBackendKind::wayland : DesktopBackendKind::x11;
        if (argc >= 2 && std::string_view(argv[1]) == "--default-frame") {
            defaultFrame(config);
            return 0;
        }
        if (argc >= 2 && std::string_view(argv[1]) == "--no-driver") {
            Ui ui;
            ScrollState scroll;
            config.renderBackend = RenderBackendKind::vulkan;
            auto result = Desktop::create(ui, webAppearance(), page, scroll, config);
            check(unavailable(result),
                  "VK03 absent driver is unavailable");
            return 0;
        }
        return failures(config);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
