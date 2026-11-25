#include "check.hpp"
#include "desktop_linux_test.hpp"
#include "linux_window.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include <thread>
#include <wayland-client.h>

using namespace isoduo;
using namespace isoduo::detail;

int main(int argc, char** argv) {
    Ui ui;
    ScrollState scroll;
    auto field = ui.addTextField({.id = "field"}).value.value();
    DesktopConfig config;
    config.backend = DesktopBackendKind::wayland;
    const std::string_view mode = argc == 2 ? argv[1] : "opengl";
    const bool recovery = mode == "recovery";
    if (mode == "vulkan" || recovery) config.renderBackend = RenderBackendKind::vulkan;
    RendererTestHooks hooks;
    bool recovered = false;
    if (recovery) config.onRendererFailed = [&](Desktop& host, const RendererFailure& error) {
        test::check(error.kind == RendererFailureKind::presentFailed, "Report swapchain recreation failure");
        test::check(bool(host.rebuildRenderer()), "Rebuild on the same native window");
        test::check(host.capabilities().renderBackend == RenderBackendKind::vulkan, "Keep Vulkan on rebuild");
        recovered = true;
    };
    config.platformAccessibilityEnabled = false;
    auto opened = LinuxDesktopTest::create(ui, webAppearance(), [&] { return layout::component(field); }, scroll, config, &hooks);
    test::check(bool(opened.desktop), "Open Wayland desktop");
    auto& desktop = *opened.desktop;
    auto* display = static_cast<wl_display*>(desktop.nativeWindow()->display);
    int frames = 0;
    auto previous = std::chrono::steady_clock::now();
    LinuxDesktopTest::observeFrames(desktop, [&](const Frame&, const LinuxClientGeometry&) {
        if (recovery && frames == 0) {
            hooks.startupResultOnce = {"vkCreateSwapchainKHR", -3};
            LinuxDesktopTest::measureFrames(desktop, {}, [](const LinuxFrameMeasurement&) {}, {});
        }
        auto now = std::chrono::steady_clock::now();
        test::check(!frames || now - previous < std::chrono::seconds(2), "Pending redraw proceeds without input");
        previous = now;
        if (++frames == 8) {
            desktop.close();
            return;
        }
        auto settings = ui.presentation();
        settings.textSize = frames % 2 ? 16 : 22;
        test::check(bool(ui.setPresentation(settings)), "Invalidate the visible field");
        // A WSI reader may queue the compositor's callback without dispatching
        // the host's default queue. Reproduce that ordering after presentation.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto* queue = wl_display_create_queue(display);
        test::check(queue != nullptr, "Create separate Wayland event queue");
        test::check(wl_display_roundtrip_queue(display, queue) >= 0, "Read events on the separate queue");
        wl_event_queue_destroy(queue);
    });
    auto run = desktop.run();
    if (run.failure) std::cerr << utf8FromUtf16(run.failure->message) << "\n";
    test::check(!recovery || recovered, "Present again after resize and explicit rebuild");
    test::check(run.status == Status::ok && frames == 8, "Present each invalidation");
}
