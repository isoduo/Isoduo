#include "check.hpp"
#include "desktop_linux_test.hpp"
#include "linux_window.hpp"
#include <thread>
#include <wayland-client.h>

using namespace isoduo;
using namespace isoduo::detail;

int main() {
    Ui ui;
    ScrollState scroll;
    auto field = ui.addTextField({.id = "field"}).value.value();
    DesktopConfig config;
    config.backend = DesktopBackendKind::wayland;
    config.platformAccessibilityEnabled = false;
    auto opened = Desktop::create(ui, webAppearance(), [&] { return layout::component(field); }, scroll, config);
    test::check(bool(opened.desktop), "Open Wayland desktop");
    auto& desktop = *opened.desktop;
    auto* display = static_cast<wl_display*>(desktop.nativeWindow()->display);
    int frames = 0;
    auto previous = std::chrono::steady_clock::now();
    LinuxDesktopTest::observeFrames(desktop, [&](const Frame&, const LinuxClientGeometry&) {
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
    if (run.failure) std::cerr << std::string(run.failure->message.begin(), run.failure->message.end()) << "\n";
    test::check(run.status == Status::ok && frames == 8, "Present each invalidation");
}
