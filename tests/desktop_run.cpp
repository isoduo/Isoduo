#include "isoduo/desktop.hpp"
#include "check.hpp"
#include <windows.h>
#include <thread>

using namespace isoduo;
using test::check;

namespace {
void runChecks() {
    Ui ui;
    ScrollState scroll;
    auto created = Desktop::create(ui, webAppearance(), [] { return layout::column({}); }, scroll, {});
    check(created.desktop != nullptr, "Create the desktop");
    auto& desktop = *created.desktop;
    auto window = static_cast<HWND>(desktop.nativeWindow());
    RunResult other;
    std::thread worker([&] { other = desktop.run(); });
    worker.join();
    check(other.status == Status::wrongThread, "Only the owner thread may run the desktop");
    check(!IsWindowVisible(window), "A rejected run leaves the window hidden");
    bool visible = false;
    RunResult nested;
    check(bool(desktop.post([&] {
        visible = IsWindowVisible(window);
        nested = desktop.run();
    })), "Queue a nested run");
    check(bool(desktop.post([&] {
        desktop.close();
    })), "Queue work for the owner thread");
    check(desktop.run().status == Status::ok, "The owner can run and close the desktop");
    check(visible, "The owner's run shows the window and dispatches posted work");
    check(nested.status == Status::busy, "A nested run is refused and the outer loop continues");
    check(desktop.run().status == Status::notFound, "A completed desktop cannot run again");
}
}

int main() {
    const test::Case cases[] = {{"desktop run checks", runChecks}};
    return test::run("desktop run", cases);
}
