#include "isoduo/layout.hpp"
#include "core_internal.hpp"
#include "fake_measure.hpp"
#include "isoduo/semantics.hpp"
#include <limits>
#include <thread>
#include "check.hpp"

using namespace isoduo;

void retainedRectangle() {
    Ui ui;
    auto area = ui.addDrawingArea({.id = "chart", .label = u"Trend"}).value.value();
    int calls = 0;
    area.onDraw([&](const DrawingEnvironment& env) {
        ++calls;
        return env.fillRect({1.5f, 2.f, 12.f, 8.f}, {20, 40, 60}, 128);
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    auto node = layout::drawingArea(area, 20);
    auto frame = arrange(node, ui, webAppearance(), measure, 100, 100, 1.f, scroll);
    auto content = area.content().value.value();
    test::check(frame.find(area) && content.size == DipSize{100, 20} &&
                content.commands == std::vector<DrawingCommand>{{DrawingCommandKind::fillRect,
                    {1.5f, 2.f, 12.f, 8.f}, {20, 40, 60}, 128}}, "arrange retains the application rectangle");
    arrange(node, ui, webAppearance(), measure, 100, 100, 1.f, scroll);
    test::check(calls == 1, "unchanged arrangement replays retained content");
}

void nestedClips() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "chart"});
    DipRect outer{1.5f, 0, 10, 12}, empty{30, 2, 4, 4}, shape{0, 0, 20, 10};
    Color color{30, 40, 50};
    area.onDraw([&](const DrawingEnvironment& env) {
        auto clipped = env.clip(outer, [&](const DrawingEnvironment& child) {
            auto nested = child.clip(empty, [&](const DrawingEnvironment& inner) {
                return inner.roundedRect(shape, 20.5f, color, 128);
            });
            if (!nested) return nested;
            return child.fillRect(shape, color);
        });
        if (!clipped) return clipped;
        return env.roundedRect(shape, 0, color);
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    arrange(layout::drawingArea(area, 20), ui, webAppearance(), measure, 100, 40, 1.25f, scroll);
    DrawingCommand rounded{DrawingCommandKind::roundedRect, shape, color, 128, 20.5f};
    DrawingCommand inner{DrawingCommandKind::clip, empty, {}, 255, 0, {rounded}};
    DrawingCommand fill{DrawingCommandKind::fillRect, shape, color};
    DrawingCommand clip{DrawingCommandKind::clip, outer, {}, 255, 0, {inner, fill}};
    DrawingCommand after{DrawingCommandKind::roundedRect, shape, color, 255, 0};
    test::check(area.content()->commands == std::vector<DrawingCommand>{clip, after},
                "nested clip retains original DIP commands even at an empty intersection");
}

void redrawLifecycle() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "area"});
    auto node = layout::drawingArea(area, 20);
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&](int width, float scale, int visibleHeight = 40) {
        return arrange(node, ui, webAppearance(), measure, width, visibleHeight, scale, scroll);
    };
    int calls = 0;
    bool enabled = false;
    float lastScale = 0;
    area.onDraw([&](const DrawingEnvironment& env) {
        ++calls;
        enabled = env.enabled();
        lastScale = env.scale();
        return env.fillRect({-10, -5, 120, 40}, {20, 30, 40});
    });
    place(100, 1);
    area.requestRedraw();
    area.requestRedraw();
    test::check(area.content()->pending && calls == 1, "redraw requests wait for arrange");
    place(100, 1, 5);
    test::check(calls == 2 && !area.content()->pending && area.content()->size == DipSize{100,20},
                "coalesced redraw gets full size even when clipped");
    auto original = *area.content();
    place(125, 1.25f);
    test::check(calls == 3 && lastScale == 1.25f && *area.content() == original, "DIP content at 125 percent");
    place(150, 1.5f);
    test::check(calls == 4 && *area.content() == original, "DIP content at 150 percent");
    place(101, 1.25f);
    test::check(area.content()->size.w == 80.8f, "physical size divided by scale");
    place(100, 1.25f);
    test::check(calls == 6 && area.content()->size.w == 80, "width alone redraws");
    auto spacer = *ui.addLabel({.id = "spacer", .value = u"Header"});
    node = layout::column({layout::component(spacer), layout::drawingArea(area, 20)}, 0, 0);
    place(100, 1.25f, 200);
    test::check(calls == 6, "moving the area keeps its cached drawing");
    area.enable(false);
    place(100, 1.25f, 200);
    test::check(calls == 7 && !enabled && !area.describe()->enabled, "disable schedules a redraw");
    area.onDraw({});
    place(100, 1);
    test::check(calls == 7 && *area.content() == DrawingContent{}, "clearing the callback clears content");
    area.remove();
    test::check(area.content().status == Status::notFound && bool(ui.addDrawingArea({.id = "area"})), "removed id is reusable");
}

void invalidRectanglesAndHeight() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "plot"});
    int calls = 0;
    area.onDraw([&](const DrawingEnvironment& env) {
        ++calls;
        for (float radius : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
            test::check(env.roundedRect({0,0,4,4}, radius, {}).status == Status::invalidValue,
                        "invalid radius rejected");
        for (auto rect : {DipRect{std::numeric_limits<float>::quiet_NaN(),0,1,2},
                          DipRect{0,std::numeric_limits<float>::infinity(),1,2},
                          DipRect{0,0,-1,2}, DipRect{0,0,1,-2}})
            test::check(env.fillRect(rect, {}).status == Status::invalidValue, "invalid rectangle rejected");
        return env.fillRect({-20, -10, 5, 5}, {});
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    for (auto node : {layout::component(area), layout::drawingArea(area, 0), layout::drawingArea(area, -3)})
        arrange(node, ui, webAppearance(), measure, 100, 100, 1, scroll);
    test::check(calls == 0, "zero height never draws");
#ifndef NDEBUG
    test::check(std::count(ui.diagnostics().begin(), ui.diagnostics().end(),
                "plot needs layout::drawingArea with a positive height") == 1, "one layout diagnostic");
#endif
    arrange(layout::drawingArea(area, 20), ui, webAppearance(), measure, 100, 100, 1, scroll);
    test::check(calls == 1 && area.content()->commands.size() == 1 &&
                area.content()->commands.front().rect == DipRect{-20,-10,5,5}, "valid offscreen command retained");
}

void imageSemantics() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "plot", .label = u"Fallback"});
    test::check(area.describe()->name == u"Fallback", "component label names the area");
    auto label = *ui.addLabel({.id = "title", .value = u"Chart", .target = "plot", .labelKind = LabelKind::name});
    ui.addLabel({.id = "caption", .value = u"Totals", .target = "plot", .labelKind = LabelKind::description});
    test::FakeMeasure measure;
    ScrollState scroll;
    arrange(layout::drawingArea(area, 100), ui, webAppearance(), measure, 80, 40, 1, scroll);
    auto node = *ui.semantics().get({"plot"});
    test::check(node.role == Role::drawingArea && area.describe()->kind == ComponentKind::drawingArea &&
                node.visible && node.enabled && !node.focusable && node.verbs.empty(), "visible passive image");
    test::check(node.name == u"Chart" && node.description == u"Totals" && node.instance != 0 &&
                node.labelledBy == std::vector<NodeRef>{{"title"}} && node.describedBy == std::vector<NodeRef>{{"caption"}},
                "semantic label associations and instance");
    test::check(node.bounds && node.bounds->x == 0 && node.bounds->y == 0 && node.bounds->w == 80 &&
                node.bounds->h == 40, "semantic bounds clipped to viewport");
    for (auto verb : {Verb::focus, Verb::invoke, Verb::contextMenu})
        test::check(ui.semantics().perform({"plot"}, verb).status == Status::notSupported, "image rejects interaction");
    test::check(bool(ui.simulateHover(area)) && ui.addContextMenu(area, {}).status == Status::notSupported,
                "hover accepted and menu refused");
    bool renamed = false;
    auto listener = ui.semantics().subscribe([&](const SemanticEvent& event) {
        if (event.ref.id == "plot" && event.property == "name") renamed = true;
    });
    label.connectSource([] { return std::u16string(u"Revised"); });
    ui.semantics().dispatchEvents();
    test::check(renamed && area.describe()->name == u"Revised", "associated label changes the image name and emits an event");
    area.enable(false);
    test::check(!ui.semantics().get({"plot"})->enabled, "disabled state visible through semantics");
    auto unnamed = *ui.addDrawingArea({.id = "unnamed"});
    auto button = *ui.addButton({.id = "button"});
    arrange(layout::column({layout::drawingArea(unnamed, 10), layout::component(button)}),
            ui, webAppearance(), measure, 80, 100, 1, scroll);
    test::check(unnamed.describe()->name.empty(), "unnamed image has no fallback to id");
#ifndef NDEBUG
    for (auto id : {"unnamed", "button"})
        test::check(std::count(ui.diagnostics().begin(), ui.diagnostics().end(), std::string(id) + " has no name") == 1,
                    "one diagnostic for an unnamed component");
#endif
}
void pageAndDialog() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "area", .label = u"Background"});
    auto tabs = *ui.addTabs({.id = "tabs"}, {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {area}}});
    auto inside = *ui.addButton({.id = "inside", .label = u"Done"});
    auto modalImage = *ui.addDrawingArea({.id = "modalImage", .label = u"Preview"});
    auto opener = *ui.addButton({.id = "open", .label = u"Open"});
    auto dialog = *ui.addDialog("dialog", {inside, modalImage}, inside, opener, [](bool) {});
    int draws = 0;
    modalImage.onDraw([&](const DrawingEnvironment&) { ++draws; return Result{}; });
    auto root = layout::modal(layout::drawingArea(area, 20),
        layout::column({layout::component(inside), layout::drawingArea(modalImage, 10)}), dialog);
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&] { return arrange(root, ui, webAppearance(), measure, 400, 300, 1, scroll); };
    place();
    test::check(ui.semantics().get({"area"})->hidden && ui.semantics().get({"modalImage"})->hidden, "inactive tab and closed dialog images are hidden");
    tabs.activate({"two"});
    place();
    test::check(area.describe()->visible, "active tab image is visible");
    dialog.open();
    place();
    test::check(draws == 1 && modalImage.describe()->visible && ui.semantics().get({"area"})->blockedByModal,
                "modal image draws and background reports modal blocking");
}

void presentationRedraw() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "area", .label = u"Chart"});
    int calls = 0;
    ThemeVariant variant;
    Color border{};
    area.onDraw([&](const DrawingEnvironment& env) {
        ++calls;
        variant = env.variant();
        border = env.palette().border;
        return Result{};
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&] { arrange(layout::drawingArea(area, 20), ui, webAppearance(), measure, 100, 40, 1, scroll); };
    place();
    auto settings = ui.presentation();
    settings.variant = ThemeVariant::dark();
    ui.setPresentation(settings);
    place();
    test::check(calls == 2 && variant == settings.variant, "theme change draws with new variant");
    settings.density = Density::compact;
    ui.setPresentation(settings);
    place();
    test::check(calls == 3, "density redraws even at unchanged size");
    ++settings.textSize;
    ui.setPresentation(settings);
    place();
    test::check(calls == 4, "text size redraws");
    ui.setPaletteOverrides({{std::string(webAppearance().name()), variant, "border", Color{17,34,51}}});
    place();
    place();
    test::check(calls == 5 && border == Color{17,34,51}, "palette override draws once with new color");
}

void callbackAccess() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "area", .label = u"Chart"});
    auto field = *ui.addTextField({.id = "field", .value = u"before"});
    std::optional<DrawingEnvironment> saved;
    bool read = false, guarded = false;
    Status workerRedraw{}, workerFill{}, workerRounded{}, workerClip{}, workerMeasure{};
    area.onDraw([&](const DrawingEnvironment& env) {
        saved = env;
        read = field.draft().value == u"before";
        guarded = area.requestRedraw().status == Status::busy &&
            ui.setPresentation(ui.presentation()).status == Status::busy &&
            ui.onMotionPreferenceChange({}).status == Status::busy &&
            ui.simulateSystemMotion(MotionPreference::off).status == Status::busy &&
            ui.onChange({}).status == Status::busy && ui.bindAppearance(nullptr).status == Status::busy &&
            field.setValue(u"after").status == Status::busy &&
            ui.addButton({.id = "late"}).status == Status::busy;
        std::thread worker([&] {
            workerRedraw = area.requestRedraw().status;
            workerMeasure = env.measureText(u"worker", {}).status;
            workerRounded = env.roundedRect({}, 0, {}).status;
            workerClip = env.clip({}, {}).status;
            workerFill = env.fillRect({}, {}).status;
        });
        worker.join();
        return Result{};
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    arrange(layout::drawingArea(area, 20), ui, webAppearance(), measure, 100, 40, 1, scroll);
    test::check(read && guarded && field.draft().value == u"before", "draw permits queries and refuses mutation");
    test::check(workerRedraw == Status::wrongThread && workerFill == Status::wrongThread && workerMeasure == Status::wrongThread &&
                workerRounded == Status::wrongThread && workerClip == Status::wrongThread, "drawing is thread affine");
    test::check(saved && saved->fillRect({}, {}).status == Status::notFound &&
                saved->roundedRect({}, 0, {}).status == Status::notFound && saved->clip({}, {}).status == Status::notFound &&
                saved->measureText(u"expired", {}).status == Status::notFound, "saved environment expires");
}

void invisibleDrawings() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "area", .label = u"Chart"});
    auto spacer = *ui.addDrawingArea({.id = "spacer", .label = u"Spacer"});
    auto tabs = *ui.addTabs({.id = "tabs"}, {{{"one"}, u"One", true, {area}}, {{"two"}, u"Two", true, {}}});
    int calls = 0;
    area.onDraw([&](const DrawingEnvironment&) { ++calls; return Result{}; });
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&](int height = 20) {
        auto node = layout::scroll(layout::column({layout::drawingArea(area, height), layout::drawingArea(spacer, 100)}, 0, 0));
        arrange(node, ui, webAppearance(), measure, 100, 40, 1, scroll);
    };
    place();
    tabs.activate({"two"});
    area.requestRedraw();
    area.requestRedraw();
    place();
    test::check(calls == 1, "inactive page defers requests");
    tabs.activate({"one"});
    place(0);
    test::check(calls == 1, "zero height defers requests");
    place();
    test::check(calls == 2, "visible page consumes one pending redraw");
    scroll.offset = 50;
    place();
    area.requestRedraw();
    area.requestRedraw();
    place();
    test::check(calls == 2, "fully scrolled out area defers requests");
    scroll.offset = 0;
    place();
    scroll.offset = 50;
    place();
    scroll.offset = 0;
    place();
    test::check(calls == 3, "scrolling back consumes only a pending redraw");
    // Feed the same visibility signal as the desktop host into a core-only Ui.
    detail::access(ui).setWindowHidden(true);
    area.requestRedraw();
    area.requestRedraw();
    place();
    test::check(calls == 3 && area.content()->pending, "hidden window retains a single pending redraw");
    detail::access(ui).setWindowHidden(false);
    place();
    place();
    test::check(calls == 4 && !area.content()->pending, "restored window consumes pending redraw once");
}

void localizedFailure() {
    for (bool throws : {false, true}) {
        Ui ui;
        auto area = *ui.addDrawingArea({.id = "chart", .label = u"Chart"});
        auto neighbor = *ui.addDrawingArea({.id = "neighbor", .label = u"Neighbor"});
        auto field = *ui.addTextField({.id = "field", .label = u"Value"});
        int calls = 0;
        area.onDraw([&](const DrawingEnvironment& env) {
            ++calls;
            env.fillRect({0,0,10,10}, {});
            if (calls > 1) return Result{};
            if (throws) throw std::runtime_error("drawing failure");
            return Result{Status::failed, {}};
        });
        neighbor.onDraw([](const DrawingEnvironment& env) { return env.fillRect({1,2,3,4}, {}); });
        auto node = layout::column({layout::drawingArea(area, 20), layout::drawingArea(neighbor, 20)});
        test::FakeMeasure measure;
        ScrollState scroll;
        auto place = [&] { arrange(node, ui, webAppearance(), measure, 100, 100, 1, scroll); };
        auto before = ui.diagnostics().size();
        place();
        place();
        test::check(calls == 1 && area.content()->status == DrawingStatus::failed && area.content()->commands.empty(),
                    "failure discards partial drawing without retry");
        test::check(ui.diagnostics().size() == before + 1 && ui.diagnostics().back().find("chart") != std::string::npos,
                    "one diagnostic identifies the failed drawing");
        test::check(neighbor.content()->commands.size() == 1 && field.enable(false) && !field.describe()->enabled, "neighbor and UI remain usable");
        area.requestRedraw();
        place();
        test::check(calls == 2 && area.content()->status == DrawingStatus::drawn, "explicit redraw recovers");
    }
}

void motionChanges() {
    Ui ui;
    using M = MotionPreference;
    test::check(ui.motionPreference() == M::noPreference, "core starts without a preference");
    auto a = *ui.addDrawingArea({.id = "a"});
    auto b = *ui.addDrawingArea({.id = "b"});
    auto root = layout::column({layout::drawingArea(a, 10), layout::drawingArea(b, 10)});
    std::vector<M> draws, changes;
    for (auto area : {a, b}) area.onDraw([&](const DrawingEnvironment& env) {
        draws.push_back(env.motionPreference());
        return Result{};
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&] { arrange(root, ui, webAppearance(), measure, 200, 200, 1, scroll); };
    place();
    ui.onMotionPreferenceChange([&](M) { test::check(false, "replaced callback ran"); });
    ui.onMotionPreferenceChange([&, calls = 0u](M value) mutable {
        test::check(calls++ == changes.size(), "registered callback keeps its state");
        test::check(value == ui.motionPreference(), "callback sees the effective value");
        changes.push_back(value);
    });
    test::check(bool(ui.simulateSystemMotion(M::reduce)), "core accepts simulated preference");
    place();
    auto settings = ui.presentation();
    settings.motion = Motion::off;
    ui.setPresentation(settings);
    place();
    ui.simulateSystemMotion(M::noPreference);
    place();
    settings.motion = Motion::automatic;
    ui.setPresentation(settings);
    place();
    ui.onMotionPreferenceChange({});
    ui.simulateSystemMotion(M::off);
    test::check(changes == std::vector<M>{M::reduce, M::off, M::noPreference}, "notify only effective changes until cleared");
    test::check(draws == std::vector<M>{M::noPreference, M::noPreference, M::reduce, M::reduce,
                M::off, M::off, M::noPreference, M::noPreference}, "both areas redraw on effective changes");
}

void drawingText() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "text"});
    DrawingTextStyle style{.bold = true};
    area.onDraw([&](const DrawingEnvironment& env) {
        for (const auto& invalid : {std::u16string(u"a\rb"), std::u16string(u"a\x01"), std::u16string(defaultMaxTextLength + 1, u'x')}) {
            test::check(env.drawText(invalid, {}, style, {}).status == Status::invalidValue &&
                        env.measureText(invalid, style).status == Status::invalidValue, "invalid text is rejected");
        }
        const auto size = env.measureText(u"abc\nd", style, 20);
        test::check(size && *size == DipSize{20, 72}, "bold width and three lines from the supplied measure");
        return env.drawText(u"abc\nd", {1,2,20,24}, style, {1,2,3}, 128);
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    arrange(layout::drawingArea(area, 100), ui, webAppearance(), measure, 100, 100, 1, scroll);
    test::check(area.content()->commands == std::vector<DrawingCommand>{{DrawingCommandKind::drawText,
        {1,2,20,24}, {1,2,3}, 128, 0, {}, u"abc\nd", style}}, "original text command survives clipping and wrapping");
}

void retainedFrameDuringDraw() {
    Ui ui;
    auto area = *ui.addDrawingArea({.id = "chart"});
    auto root = layout::scroll(layout::drawingArea(area, 100));
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&] { return arrange(root, ui, webAppearance(), measure, 100, 40, 1, scroll); };
    auto frame = place();
    bool scrolled = false;
    area.onDraw([&](const DrawingEnvironment&) {
        scrolled = frame.scrollTo(ui, scroll, 10);
        return Result{};
    });
    place();
    test::check(!scrolled && scroll.offset == 0, "retained frame cannot scroll during drawing");
    test::check(frame.scrollTo(ui, scroll, 10), "retained frame can scroll after drawing");
}

void drawingConfig() {
    Ui ui;
    for (auto config : {ComponentConfig{.id = "a", .checked = true}, ComponentConfig{.id = "a", .visibleLines = 1},
                        ComponentConfig{.id = "a", .verticalDigits = 1}, ComponentConfig{.id = "a", .value = u"x"}})
        test::check(ui.addDrawingArea(config).status == Status::invalidValue, "reject foreign drawing area options");
    test::check(bool(ui.addDrawingArea({.id = "a", .label = u"A", .enabled = false})), "invalid config reserves no ID");
}

void frameMutations() {
    using namespace std::chrono_literals;
    Ui ui;
    auto a = *ui.addDrawingArea({.id = "a"});
    auto b = *ui.addDrawingArea({.id = "b"});
    test::FakeMeasure measure;
    ScrollState scroll;
    auto place = [&] {
        arrange(layout::scroll(layout::column({layout::drawingArea(a, 20), layout::drawingArea(b, 100)}, 0, 0)),
                ui, webAppearance(), measure, 100, 40, 1, scroll);
    };
    int draws = 0, calls = 0;
    a.onDraw([&](const DrawingEnvironment&) {
        ++draws;
        test::check(a.requestFrame().status == Status::busy, "drawing cannot schedule");
        return Result{};
    });
    place();
    b.onFrame([&](const FrameInfo&) { ++calls; return Result{}; });
    a.onFrame([&](const FrameInfo&) { return b.requestFrame(); });
    a.requestFrame();
    b.requestFrame();
    ui.simulateFrame(0ms);
    ui.simulateFrame(0ms);
    test::check(calls == 2, "request for a later callback in this batch also schedules the next frame");
    a.onFrame([&](const FrameInfo&) { return b.cancelFrame(); });
    a.requestFrame();
    b.requestFrame();
    ui.simulateFrame(0ms);
    test::check(calls == 2, "cancel withdraws a later callback in the same batch");
    int attempts = 0;
    a.onFrame([&](const FrameInfo&) {
        if (++attempts > 1) return Result{};
        a.requestRedraw();
        a.onFrame([&](const FrameInfo&) { ++attempts; return Result{}; });
        a.requestFrame();
        return Result{Status::failed, {}};
    });
    scroll.offset = 50;
    place();
    a.requestFrame();
    test::check(!*ui.animating() && !*ui.semantics().animating(), "clipped request pauses");
    ui.simulateFrame(2ms);
    scroll.offset = 0;
    place();
    auto diagnostics = ui.diagnostics().size();
    auto before = draws;
    ui.simulateFrame(0ms);
    place();
    test::check(draws == before + 1 && ui.diagnostics().size() == diagnostics + 1 && !*ui.animating(),
                "resume, redraw and local failure with no automatic retry");
    a.requestFrame();
    ui.simulateFrame(0ms);
    test::check(attempts == 2 && !*ui.animating() && ui.diagnostics().size() == diagnostics + 1,
                "explicit request reuses the callback after a failure");
    a.onFrame([&](const FrameInfo&) { return b.remove(); });
    a.requestFrame();
    b.requestFrame();
    auto button = *ui.addButton({.id = "advance"});
    button.bind([&] { return ui.simulateFrame(0ms); });
    button.invoke();
    test::check(calls == 2 && !b.describe(), "removal withdraws a later callback");
    Status worker{};
    std::thread([&] { worker = a.requestFrame().status; }).join();
    test::check(worker == Status::wrongThread, "wrong thread cannot schedule");
    before = draws;
    a.onFrame([&](const FrameInfo&) -> Result { a.requestRedraw(); throw 1; });
    a.requestFrame();
    test::check(ui.simulateFrame(0ms).status == Status::failed && a.requestFrame().status == Status::failed,
                "exception stops UI");
    place();
    test::check(draws == before, "stopped UI skips pending drawing");
}

void applicationFrames() {
    using namespace std::chrono_literals;
    Ui ui;
    auto a = *ui.addDrawingArea({.id = "a"});
    auto b = *ui.addDrawingArea({.id = "b"});
    test::FakeMeasure measure;
    ScrollState scroll;
    arrange(layout::column({layout::drawingArea(a, 20), layout::drawingArea(b, 20)}),
            ui, webAppearance(), measure, 200, 200, 1, scroll);
    test::check(a.requestFrame().status == Status::invalidValue, "request requires registration");
    std::vector<int> order;
    std::vector<std::chrono::steady_clock::time_point> times;
    ui.simulateSystemMotion(MotionPreference::reduce);
    a.onFrame([&](const FrameInfo& frame) {
        test::check(frame.motionPreference == MotionPreference::reduce, "effective preference reaches application");
        times.push_back(frame.time);
        order.push_back(1);
        test::check(ui.simulateFrame(0ms).status == Status::busy, "recursive frame rejected");
        return Result{};
    });
    b.onFrame([&](const FrameInfo& frame) {
        times.push_back(frame.time);
        order.push_back(2);
        return b.requestFrame();
    });
    ui.simulateFrame(0ms);
    test::check(order.empty(), "registration does not replay an invalid request");
    b.requestFrame();
    a.requestFrame();
    b.requestFrame();
    test::check(*ui.animating() && *ui.semantics().animating(), "visible requests animate");
    ui.simulateFrame(4ms);
    ui.simulateFrame(6ms);
    test::check(order == std::vector<int>{2, 1, 2} && times[0] == times[1] && times[2]-times[1] == 6ms,
                "one timestamp and one invocation per accepted request in order");
    ui.simulateFrame(0ms);
    test::check(times.size() == 4 && times[3] == times[2], "zero advance dispatches at the previous timestamp");
    b.onFrame({});
    test::check(!*ui.animating() && b.requestFrame().status == Status::invalidValue, "clearing a callback cancels its request");
    b.cancelFrame();
    test::check(!*ui.animating() && ui.simulateFrame(-1ms).status == Status::invalidValue, "cancel and negative advance");
}

int main() {
    const test::Case cases[] = {{"retained frame during draw", retainedFrameDuringDraw}, {"drawing config", drawingConfig}, {"frame mutations", frameMutations}, {"application frames", applicationFrames}, {"motion changes", motionChanges}, {"drawing text", drawingText}, {"nested clips", nestedClips}, {"invisible drawings", invisibleDrawings}, {"localized failure", localizedFailure},
        {"callback access", callbackAccess}, {"presentation redraw", presentationRedraw}, {"retained rectangle", retainedRectangle}, {"redraw lifecycle", redrawLifecycle},
        {"invalid rectangles and height", invalidRectanglesAndHeight}, {"image semantics", imageSemantics}, {"page and dialog", pageAndDialog}};
    return test::run("drawing area", cases);
}
