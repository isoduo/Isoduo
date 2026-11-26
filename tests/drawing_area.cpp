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
    Status workerRedraw{}, workerFill{}, workerMeasure{};
    area.onDraw([&](const DrawingEnvironment& env) {
        saved = env;
        read = field.draft().value == u"before";
        guarded = area.requestRedraw().status == Status::busy &&
            ui.setPresentation(ui.presentation()).status == Status::busy &&
            ui.onChange({}).status == Status::busy && ui.bindAppearance(nullptr).status == Status::busy &&
            field.setValue(u"after").status == Status::busy &&
            ui.addButton({.id = "late"}).status == Status::busy;
        std::thread worker([&] {
            workerRedraw = area.requestRedraw().status;
            workerMeasure = env.measureText(u"worker", {}).status;
            workerFill = env.fillRect({}, {}).status;
        });
        worker.join();
        return Result{};
    });
    test::FakeMeasure measure;
    ScrollState scroll;
    arrange(layout::drawingArea(area, 20), ui, webAppearance(), measure, 100, 40, 1, scroll);
    test::check(read && guarded && field.draft().value == u"before", "draw permits queries and refuses mutation");
    test::check(workerRedraw == Status::wrongThread && workerFill == Status::wrongThread && workerMeasure == Status::wrongThread, "drawing is thread affine");
    test::check(saved && saved->fillRect({}, {}).status == Status::notFound &&
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
        {1,2,20,24}, {1,2,3}, 128, u"abc\nd", style}}, "original text command survives clipping and wrapping");
}

int main() {
    const test::Case cases[] = {{"drawing text", drawingText}, {"invisible drawings", invisibleDrawings}, {"localized failure", localizedFailure},
        {"callback access", callbackAccess}, {"presentation redraw", presentationRedraw}, {"retained rectangle", retainedRectangle}, {"redraw lifecycle", redrawLifecycle},
        {"invalid rectangles and height", invalidRectanglesAndHeight}, {"image semantics", imageSemantics}, {"page and dialog", pageAndDialog}};
    return test::run("drawing area", cases);
}
