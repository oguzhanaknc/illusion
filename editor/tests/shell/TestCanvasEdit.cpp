#include "shell/ShellTestSupport.hpp"

#include <illusion/ecs/Name.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <cmath>
#include <imgui.h>

using namespace illusion;
using namespace illusion::editor;
using test::ShellFixture;

// Canvas editing in the Game view and the Inspector's placement drawer (ADR 0057, UI-3d).

namespace
{

// The shell with a Game view whose layout comes from a preview UiWorld over the edit World, laid out for
// the Game view's image, so that the handles sit where the gameui commands put the element.
struct CanvasShell
{
    ShellFixture fixture;
    test::FakeViewports viewports;
    std::unique_ptr<UiWorld> preview;
    UVec2 target{0};
    std::unique_ptr<ui::UiDriver> driver;
    ui::UiItem image{};

    CanvasShell()
    {
        viewports.frames = true;
        EditorContext& editor = *fixture.editor;
        editor.setViewports(&viewports);
        test::ok(editor, "gameui.create", R"({"kind":"canvas","name":"HUD"})");
        test::ok(editor, "gameui.create", R"({"kind":"panel","name":"Box","parent":"/HUD"})");
        test::ok(editor, "gameui.create", R"({"kind":"stack","name":"List","parent":"/HUD"})");
        test::ok(editor, "gameui.create", R"({"kind":"panel","name":"Row","parent":"/HUD/List"})");
        test::ok(editor, "ui.show_panel", R"({"panel":"panel.game"})");
        UiWorldDesc desc;
        desc.preview = true;
        preview = std::make_unique<UiWorld>(editor.world(), desc);
        driver = std::make_unique<ui::UiDriver>(fixture.context, [this] {
            refreshLayout();
            fixture.frame();
        });
        driver->runFrames(3);
        const std::optional<ui::UiItem> found = driver->find("viewport.image");
        REQUIRE(found.has_value());
        image = *found;
        target = viewports.gamePanel;
        REQUIRE(target.x != 0);
        // The box well inside the image, anchored at the canvas's top left, and the stack beside it.
        const std::string size =
            ",\"width\":" + std::to_string(target.x) + ",\"height\":" + std::to_string(target.y) + "}";
        test::ok(editor, "gameui.place", R"({"entity":"/HUD/Box","min":[200,200],"max":[600,500])" + size);
        test::ok(editor, "gameui.place", R"({"entity":"/HUD/List","min":[900,100],"max":[1300,400])" + size);
        driver->runFrames(2);
    }

    ~CanvasShell()
    {
        driver.reset();
        preview.reset();
        fixture.editor->setViewports(nullptr);
    }

    CanvasShell(const CanvasShell&) = delete;
    CanvasShell& operator=(const CanvasShell&) = delete;

    void refreshLayout()
    {
        // The image of the previous frame (its item is clipped to the panel, the image is not).
        if (const std::optional<ui::UiItem> found = driver ? driver->find("viewport.image") : std::nullopt)
        {
            image = *found;
        }
        target = viewports.gamePanel;
        if (target.x == 0)
        {
            return;
        }
        preview->update(UiFrame{.targetPixels = target, .safeInsetPixels = {}});
        UiLayout layout;
        preview->layout(layout);
        viewports.gameLayout = std::move(layout);
    }

    [[nodiscard]] f32 pixelsPerUnit() const
    {
        REQUIRE(viewports.gameLayout.has_value());
        REQUIRE_FALSE(viewports.gameLayout->canvases.empty());
        return viewports.gameLayout->canvases[0].metrics.pixelsPerUnit;
    }

    // The laid-out rectangle of the element named @p name, in canvas units.
    [[nodiscard]] UiRect rectOf(std::string_view name) const
    {
        REQUIRE(viewports.gameLayout.has_value());
        const World& world = fixture.editor->world();
        for (const UiLayoutElement& element : viewports.gameLayout->elements)
        {
            const Name* named = world.tryGet<Name>(element.entity);
            if (named != nullptr && named->value == name)
            {
                return element.rect;
            }
        }
        FAIL("no element named " << name);
        return {};
    }

    // A point of the canvas in window coordinates.
    [[nodiscard]] Vec2 window(Vec2 canvas) const { return image.min + canvas * pixelsPerUnit(); }

    // Presses at @p from, moves to @p to in @p steps and releases; Escape at step @p escapeAt.
    void dragAt(Vec2 from, Vec2 to, u32 steps, std::optional<u32> escapeAt = std::nullopt)
    {
        ImGuiIO& io = ImGui::GetIO();
        driver->moveMouse(from);
        driver->runFrames(1);
        io.AddMouseButtonEvent(0, true);
        driver->runFrames(1);
        for (u32 step = 1; step <= steps; ++step)
        {
            driver->moveMouse(from + (to - from) * (static_cast<f32>(step) / static_cast<f32>(steps)));
            if (escapeAt && step == *escapeAt)
            {
                driver->pressKey(ImGuiKey_Escape);
            }
        }
        io.AddMouseButtonEvent(0, false);
        driver->runFrames(2);
    }

    void click(Vec2 at)
    {
        ImGuiIO& io = ImGui::GetIO();
        driver->moveMouse(at);
        driver->runFrames(1);
        io.AddMouseButtonEvent(0, true);
        driver->runFrames(1);
        io.AddMouseButtonEvent(0, false);
        driver->runFrames(2);
    }
};

f64 placementAt(EditorContext& editor, std::string_view entity, std::string_view pointer)
{
    const JsonDocument got =
        test::ok(editor, "entity.get", std::string(R"({"entity":")") + std::string(entity) + "\"}");
    const std::optional<f64> value =
        got.root().pointer(std::string("/components/illusion.UiElement/") + std::string(pointer)).asF64();
    REQUIRE(value.has_value());
    return *value;
}

bool selectedOnly(EditorContext& editor, std::string_view entity)
{
    const JsonDocument got =
        test::ok(editor, "entity.get", std::string(R"({"entity":")") + std::string(entity) + "\"}");
    const std::optional<std::string_view> guid = got.root().pointer("/entity/id").asString();
    return editor.selection().size() == 1 && guid &&
           *guid == std::string_view(formatGuid(editor.selection().primary()).data(), 16);
}

} // namespace

TEST_CASE("a click in the Game view selects the game UI element under the cursor")
{
    CanvasShell canvas;
    EditorContext& editor = *canvas.fixture.editor;
    test::ok(editor, "selection.clear");
    const UiRect box = canvas.rectOf("Box");
    canvas.click(canvas.window((box.min + box.max) * 0.5f));
    CHECK(selectedOnly(editor, "/HUD/Box"));
    // The row of the stack lies over the canvas elsewhere; a click on it selects it instead.
    const UiRect row = canvas.rectOf("Row");
    canvas.click(canvas.window((row.min + row.max) * 0.5f));
    CHECK(selectedOnly(editor, "/HUD/List/Row"));
}

TEST_CASE("dragging a game UI element in the Game view moves it in one undo step, a corner resizes it")
{
    CanvasShell canvas;
    EditorContext& editor = *canvas.fixture.editor;
    test::ok(editor, "selection.set", R"({"entities":["/HUD/Box"]})");
    canvas.driver->runFrames(2);
    const usize steps = editor.history().steps().size();
    const f32 scale = canvas.pixelsPerUnit();

    // The body: the offsets change by the dragged canvas units, rounded to whole units.
    const UiRect box = canvas.rectOf("Box");
    const Vec2 grab = canvas.window((box.min + box.max) * 0.5f);
    const Vec2 moved(40.0f, 24.0f);
    canvas.dragAt(grab, grab + moved, 8);
    const f64 left = placementAt(editor, "/HUD/Box", "placement/offsetMin/0");
    const f64 top = placementAt(editor, "/HUD/Box", "placement/offsetMin/1");
    CHECK(left == doctest::Approx(200.0 + static_cast<f64>(moved.x / scale)).epsilon(0.02));
    CHECK(top == doctest::Approx(200.0 + static_cast<f64>(moved.y / scale)).epsilon(0.02));
    CHECK(left == std::round(left));
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMax/0") - left == doctest::Approx(400.0));
    REQUIRE(editor.history().steps().size() == steps + 1);
    CHECK(editor.history().steps().back().origin == "gizmo");
    CHECK(editor.transaction() == nullptr);
    CHECK(selectedOnly(editor, "/HUD/Box"));
    test::ok(editor, "history.undo");
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMin/0") == doctest::Approx(200.0));
    canvas.driver->runFrames(2);

    // The bottom right corner: only the bottom right moves.
    canvas.dragAt(canvas.window(box.max), canvas.window(box.max) + Vec2(30.0f, 20.0f), 6);
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMin/0") == doctest::Approx(200.0));
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMin/1") == doctest::Approx(200.0));
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMax/0") ==
          doctest::Approx(600.0 + static_cast<f64>(30.0f / scale)).epsilon(0.02));
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMax/1") ==
          doctest::Approx(500.0 + static_cast<f64>(20.0f / scale)).epsilon(0.02));
    CHECK(editor.history().steps().size() == steps + 1);
}

TEST_CASE("Escape during a canvas drag leaves the placement and adds no undo step")
{
    CanvasShell canvas;
    EditorContext& editor = *canvas.fixture.editor;
    test::ok(editor, "selection.set", R"({"entities":["/HUD/Box"]})");
    canvas.driver->runFrames(2);
    const usize steps = editor.history().steps().size();
    const UiRect box = canvas.rectOf("Box");
    const Vec2 grab = canvas.window((box.min + box.max) * 0.5f);
    canvas.dragAt(grab, grab + Vec2(60.0f, 0.0f), 8, 5);
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMin/0") == doctest::Approx(200.0));
    CHECK(placementAt(editor, "/HUD/Box", "placement/offsetMax/0") == doctest::Approx(600.0));
    CHECK(editor.history().steps().size() == steps);
    CHECK(editor.transaction() == nullptr);
    CHECK(selectedOnly(editor, "/HUD/Box"));
}

TEST_CASE("an anchor preset from the Inspector changes the anchors and keeps the element's rectangle")
{
    CanvasShell canvas;
    EditorContext& editor = *canvas.fixture.editor;
    test::ok(editor, "selection.set", R"({"entities":["/HUD/Box"]})");
    canvas.driver->runFrames(2);
    const UiRect before = canvas.rectOf("Box");
    REQUIRE(canvas.driver->click("inspector.card.illusion.UiElement.placement.preset"));
    canvas.driver->runFrames(1);
    REQUIRE(canvas.driver->click("inspector.card.illusion.UiElement.placement.preset.bottom_right"));
    canvas.driver->runFrames(2);
    CHECK(placementAt(editor, "/HUD/Box", "placement/anchorMin/0") == 1.0);
    CHECK(placementAt(editor, "/HUD/Box", "placement/anchorMax/1") == 1.0);
    const UiRect after = canvas.rectOf("Box");
    CHECK(after.min.x == doctest::Approx(before.min.x).epsilon(1e-3));
    CHECK(after.min.y == doctest::Approx(before.min.y).epsilon(1e-3));
    CHECK(after.max.x == doctest::Approx(before.max.x).epsilon(1e-3));
    CHECK(after.max.y == doctest::Approx(before.max.y).epsilon(1e-3));
    // Coinciding anchors show the position and size; a width edit keeps the left edge.
    CHECK(canvas.driver->find("inspector.card.illusion.UiElement.placement.pos_x").has_value());
    CHECK(canvas.driver->find("inspector.card.illusion.UiElement.placement.width").has_value());
    CHECK_FALSE(canvas.driver->find("inspector.card.illusion.UiElement.placement.left").has_value());
    test::ok(editor, "gameui.set_anchors", R"({"entity":"/HUD/Box","preset":"stretch"})");
    canvas.driver->runFrames(2);
    CHECK(canvas.driver->find("inspector.card.illusion.UiElement.placement.left").has_value());
    CHECK(canvas.driver->find("inspector.card.illusion.UiElement.placement.bottom").has_value());
}

TEST_CASE("a child of a stack shows a note instead of the anchors button and keeps its other fields")
{
    CanvasShell canvas;
    EditorContext& editor = *canvas.fixture.editor;
    test::ok(editor, "selection.set", R"({"entities":["/HUD/List/Row"]})");
    canvas.driver->runFrames(2);
    CHECK_FALSE(canvas.driver->find("inspector.card.illusion.UiElement.placement.preset").has_value());
    CHECK(canvas.driver->find("inspector.card.illusion.UiElement.rotation").has_value());
    test::ok(editor, "selection.set", R"({"entities":["/HUD/Box"]})");
    canvas.driver->runFrames(2);
    CHECK(canvas.driver->find("inspector.card.illusion.UiElement.placement.preset").has_value());
}
