#include "core_internal.hpp"
#include "check.hpp"
#include <barrier>

using namespace isoduo;
using detail::HostClaim;
using test::check;

static void exclusiveHost() {
    Ui ui;
    auto label = ui.addLabel({.id = "label"}).value.value();
    std::u16string value;
    std::function<void()> notify;
    label.connectSource(Source<std::u16string>{[&] { return value; }, [&](auto changed) {
        notify = std::move(changed);
        return [] {};
    }});
    HostClaim first(ui), second(ui);
    check(first.status() == Status::ok && second.status() == Status::busy, "Only the first host attaches");
    bool painted = false;
    first.state().onInvalidate([&] { painted = true; });
    first.state().bindAppearance(&webAppearance());
    second.release();
    value = u"attached";
    notify();
    check(painted, "The owner's invalidation survives a refused host");
    first.release();
    painted = false;
    notify();
    check(!painted, "The released host receives no invalidations");
    check(!ui.semantics().effectivePalette(), "The released appearance is unbound");
}

static void inputAndWindowStateEndWithHost() {
    Ui ui;
    auto button = ui.addButton({.id = "button"}).value.value();
    auto tip = ui.addTooltip("tip", button, u"Help", 0).value.value();
    int invoked = 0;
    button.bind([&] { ++invoked; return Result{}; });
    HostClaim first(ui);
    auto& state = first.state();
    state.windowActive(true);
    state.setSystemKeyboardCues(true);
    button.focus();
    check(*tip.visible(), "The active window shows the focused tooltip");
    state.pointerDown("button");
    state.keyDown({Key::space});
    state.frameVisibility({{"button", false}});
    state.setSystemAnimations(false);
    state.setWindowHidden(true);
    first.release();

    HostClaim next(ui);
    auto& input = next.state();
    input.pointerUp("button");
    input.keyUp(Key::space);
    check(invoked == 0, "Input from the released host cannot complete");
    check(!ui.focusIndicatorVisible(), "The old keyboard cue preference is cleared");
    input.tooltipHover("button");
    check(!*tip.visible(), "The replacement starts inactive");
    input.windowActive(true);
    check(*tip.visible(), "The replacement uses fresh visibility");
    check(!input.motionEnabled(), "Transitions finish immediately without a host clock");
    input.animationTime([] { return 10; });
    check(input.motionEnabled(), "The new clock animates with default visibility and system preferences");
}

static void rendererFailureEndsWithHost() {
    Ui ui;
    auto button = ui.addButton({.id = "button"}).value.value();
    HostClaim first(ui);
    first.state().rendererFailed(true);
    check(button.focus().status == Status::rendererFailed, "Failed host refuses focus");
    first.release();
    HostClaim next(ui);
    check(next.status() == Status::ok && bool(button.focus()), "New host accepts focus");
}

static void partialAttachmentRollsBack() {
    Ui ui;
    auto field = ui.addTextField({.id = "field"}).value.value();
    field.connectSource({[] { return u"original"; }, test::ignoreNotifications});
    field.connectConfirmed([](const auto&) { return Result{}; });
    struct ClipboardProbe : Clipboard {
        bool write(std::u16string_view) override { return true; }
        Query<std::u16string> read() override { return {Status::ok, {}, u"copied"}; }
    } clipboard;
    {
        HostClaim partial(ui);
        partial.state().setClipboard(&clipboard);
        field.select(0, 2);
        check(bool(field.copy()), "The partially attached clipboard works");
        partial.state().bindAppearance(&webAppearance());
        check(bool(field.focus()) && bool(partial.state().compositionUpdate(u"preedit", 2)), "Compose while hosted");
        check(field.setValue(u"busy").status == Status::busy, "Composition owns the edit");
    }
    check(!ui.semantics().effectivePalette() && field.draft().value == u"original" && bool(field.setValue(u"new")),
          "Rollback discards preedit and restores editing");
    field.select(0, 2);
    check(field.copy().status == Status::failed, "Core-only editing has no clipboard");
    HostClaim next(ui);
    check(next.status() == Status::ok, "Partial attachment releases ownership");
}

static void simultaneousClaims() {
    Ui ui;
    auto label = ui.addLabel({.id = "label"}).value.value();
    std::function<void()> notify;
    label.connectSource(Source<std::u16string>{[] { return u"value"; }, [&](auto changed) {
        notify = std::move(changed);
        return [] {};
    }});
    std::barrier phase(3);
    Status results[2]{};
    int painted = -1;
    auto attempt = [&](int index) {
        for (int round = 0; round < 1000; ++round) {
            phase.arrive_and_wait();
            HostClaim claim(ui);
            results[index] = claim.status();
            if (claim.status() == Status::ok)
                claim.state().onInvalidate([&, index] { painted = index; });
            phase.arrive_and_wait();
            phase.arrive_and_wait();
        }
    };
    std::thread left(attempt, 0), right(attempt, 1);
    bool exclusive = true;
    for (int round = 0; round < 1000; ++round) {
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        painted = -1;
        notify();
        exclusive &= (results[0] == Status::ok) != (results[1] == Status::ok);
        exclusive &= painted == (results[0] == Status::ok ? 0 : 1);
        phase.arrive_and_wait();
    }
    left.join();
    right.join();
    check(exclusive, "Every simultaneous claim leaves exactly one working host");
}

int main() {
    const test::Case cases[] = {{"exclusive host", exclusiveHost}, {"renderer recovery", rendererFailureEndsWithHost},
        {"input and window state", inputAndWindowStateEndWithHost}, {"partial attachment", partialAttachmentRollsBack}, {"simultaneous claims", simultaneousClaims}};
    return test::run("host claim", cases);
}
