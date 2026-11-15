#include "Budget.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/StringTable.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiDrawList.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>
#include <illusion/ui/UiWorldService.hpp>

#include <array>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

const Font& benchFont()
{
    static const Font font = [] {
        std::ifstream file(utf8Path(ILLUSION_TEST_FONT), std::ios::binary);
        const std::vector<char> chars((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        const auto bytes = std::as_bytes(std::span(chars));
        return std::move(Font::bake(bytes)).value();
    }();
    return font;
}

constexpr std::string_view kParagraph =
    "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs! "
    "Sphinx of black quartz, judge my vow. How vexingly quick daft zebras jump; the five boxing wizards "
    "jump quickly. Waltz, bad nymph, for quick jigs vex. Glib jocks quiz nymph to vex dwarf. "
    "Jackdaws love my big sphinx of quartz. AVAST: Typography wraps at spaces, kerns pairs and counts.";

constexpr u32 kLayoutElements = 1000;

// The benchmark font and a 64x32 image, always loaded.
class BenchAssets final : public UiAssets
{
public:
    UiFontFace font(const AssetRef&) override { return {&benchFont(), UiTextureId{1}}; }
    UiImageSource image(const AssetRef&) override { return {UiTextureId{2}, Vec2(64.0f, 32.0f)}; }
};

// 1,000 elements: ten stacked panels of texts, images and bars on a canvas.
void buildLayout(World& world)
{
    UiCanvas canvas;
    canvas.useSafeArea = false;
    const Entity root = world.create(canvas);
    constexpr u32 kPanels = 10;
    for (u32 p = 0; p < kPanels; ++p)
    {
        UiElement panel;
        const f32 x = static_cast<f32>(p % 5) * 380.0f;
        const f32 y = static_cast<f32>(p / 5) * 540.0f;
        panel.placement.offsetMin = {x, y};
        panel.placement.offsetMax = {x + 370.0f, y + 530.0f};
        panel.clipChildren = true;
        UiStack stack;
        stack.spacing = 2.0f;
        stack.padding = {4.0f, 4.0f, 4.0f, 4.0f};
        const Entity parent = world.create(panel, stack, UiImage{});
        (void)setParent(world, parent, root);
        for (u32 i = 1; i < kLayoutElements / kPanels; ++i)
        {
            Entity row;
            if (i % 3 == 0)
            {
                UiText text;
                text.text = "Item label";
                text.font = AssetRef("assets:/fonts/bench.ttf");
                text.size = 12.0f;
                row = world.create(UiElement{}, std::move(text));
            }
            else if (i % 3 == 1)
            {
                UiImage icon;
                icon.image = AssetRef("assets:/ui/icon.png");
                UiElement element;
                element.size = {0.0f, 4.0f};
                row = world.create(element, icon);
            }
            else
            {
                UiElement element;
                element.size = {0.0f, 3.0f};
                element.opacity = 0.5f;
                row = world.create(element, UiImage{});
            }
            (void)setParent(world, row, parent);
        }
    }
    HierarchyIndex index;
    updateTransformHierarchy(world, index);
}

} // namespace

ILL_BENCHMARK("ui/text-layout", 1000.0)
{
    const Font& font = benchFont();
    UiDrawList list;
    const UiCanvasMetrics canvas = canvasMetrics({}, {1920, 1080});
    UiTextStyle style;
    style.size = 28.0f;
    style.wrapWidth = 640.0f;
    style.align = UiTextAlign::Center;
    bench.batch(kParagraph.size()).unit("glyph").run("lay out and record a wrapped paragraph", [&] {
        list.begin(canvas);
        ankerl::nanobench::doNotOptimizeAway(
            list.text({&font, UiTextureId{1}}, {100.0f, 100.0f}, kParagraph, style));
    });
}

ILL_BENCHMARK("ui/draw-list-quads", 250.0)
{
    constexpr u32 kQuads = 10000;
    UiDrawList list;
    const UiCanvasMetrics canvas = canvasMetrics({}, {1920, 1080});
    bench.batch(kQuads).unit("quad").run("record batched image quads", [&] {
        list.begin(canvas);
        for (u32 i = 0; i < kQuads; ++i)
        {
            const f32 x = static_cast<f32>(i % 100) * 19.0f;
            const f32 y = static_cast<f32>(i / 100) * 10.0f;
            const Vec4 tint(static_cast<f32>(i & 255u) / 255.0f, 0.4f, 0.6f, 0.8f);
            ankerl::nanobench::doNotOptimizeAway(
                list.image({{x, y}, {x + 18.0f, y + 9.0f}}, UiTextureId{2}, tint));
        }
    });
}

ILL_BENCHMARK("ui/layout", 5000.0)
{
    World world;
    buildLayout(world);
    BenchAssets assets;
    UiWorld ui(world);
    ui.setAssets(&assets);
    ui.update({{1920, 1080}});
    bench.minEpochIterations(50)
        .batch(kLayoutElements)
        .unit("element")
        .run("lay out and record 1,000 elements", [&] {
            world.markChanged<UiElement>();
            ui.update({{1920, 1080}});
            ankerl::nanobench::doNotOptimizeAway(ui.drawData().vertices.size());
        });
}

ILL_BENCHMARK("ui/hit-test", 400000.0)
{
    // 1,000 buttons in a 40x25 grid; the pointer moves over a different one every update.
    World world;
    UiCanvas canvas;
    canvas.useSafeArea = false;
    const Entity root = world.create(canvas);
    constexpr u32 kColumns = 40;
    for (u32 i = 0; i < kLayoutElements; ++i)
    {
        UiElement element;
        const Vec2 min(static_cast<f32>(i % kColumns) * 48.0f, static_cast<f32>(i / kColumns) * 43.0f);
        element.placement.offsetMin = min;
        element.placement.offsetMax = min + Vec2(44.0f, 40.0f);
        const Entity button = world.create(element, UiImage{}, UiInteractable{}, UiButton{});
        (void)setParent(world, button, root);
    }
    HierarchyIndex index;
    updateTransformHierarchy(world, index);
    UiWorld ui(world);
    ui.update({{1920, 1080}});
    UiInput input;
    input.pointerPresent = true;
    input.deltaSeconds = 1.0 / 60.0;
    u32 frame = 0;
    bench.minEpochIterations(50).unit("update").run("hit-test and record 1,000 buttons", [&] {
        const u32 cell = (frame++ * 37u) % kLayoutElements;
        input.pointerPixels = {static_cast<f32>(cell % kColumns) * 48.0f + 20.0f,
                               static_cast<f32>(cell / kColumns) * 43.0f + 20.0f};
        ui.update({{1920, 1080}}, input);
        ankerl::nanobench::doNotOptimizeAway(ui.drawData().vertices.size());
    });
}

ILL_BENCHMARK("ui/layout-reused", 3000.0)
{
    World world;
    buildLayout(world);
    BenchAssets assets;
    UiWorld ui(world);
    ui.setAssets(&assets);
    ui.update({{1920, 1080}});
    bench.batch(kLayoutElements).unit("element").run("record 1,000 elements with the layout reused", [&] {
        ui.update({{1920, 1080}});
        ankerl::nanobench::doNotOptimizeAway(ui.drawData().vertices.size());
    });
}

namespace
{

constexpr std::string_view kScoreKeys[] = {"hud.score"};
constexpr std::string_view kScoreEnglish[] = {"Score {points, plural, one {# point} other {# points}}"};
const text::StringTableData kScoreTable{"bench", kScoreKeys, kScoreEnglish};

} // namespace

// A module's HUD: each update sets a keyed text with a new number through UiService, and the UI formats it
// (plural rule and number format) while it lays out and records. Measured about 2.6 us on a desktop.
ILL_BENCHMARK("ui/set-text-key", 20000.0)
{
    World world;
    UiCanvas canvas;
    canvas.useSafeArea = false;
    const Entity root = world.create(canvas);
    UiElement placement;
    placement.placement.offsetMax = {400.0f, 40.0f};
    UiText text;
    text.font = AssetRef("assets:/fonts/bench.ttf");
    const Entity score = world.create(placement, text);
    (void)setParent(world, score, root);
    text::Localization localization;
    localization.addTable(kScoreTable);
    BenchAssets assets;
    UiWorld ui(world);
    ui.setAssets(&assets);
    ui.setLocalization(&localization);
    UiWorldService service(world, ui, localization);
    ui.update({{1920, 1080}});
    f64 points = 0.0;
    bench.minEpochIterations(200).unit("update").run("set a keyed text with a number and update", [&] {
        points += 1.0;
        const std::array arguments{UiTextArgument::number("points", points)};
        (void)service.setTextKey(score, "hud.score", arguments);
        ui.update({{1920, 1080}});
        ankerl::nanobench::doNotOptimizeAway(ui.drawData().vertices.size());
    });
}

// A scroll view with 1,000 rows of text, 20 of them visible; every update the wheel scrolls it, which moves
// the rows' rectangles and records the visible ones. Measured about 43 us on a desktop.
ILL_BENCHMARK("ui/scroll-1000", 300000.0)
{
    World world;
    UiCanvas canvas;
    canvas.useSafeArea = false;
    const Entity root = world.create(canvas);
    UiElement placement;
    placement.placement.offsetMax = {400.0f, 800.0f};
    const Entity view = world.create(placement, UiScrollView{});
    (void)setParent(world, view, root);
    UiStack stack;
    stack.axis = UiAxis::Vertical;
    const Entity content = world.create(UiElement{}, stack);
    (void)setParent(world, content, view);
    for (u32 i = 0; i < kLayoutElements; ++i)
    {
        UiElement row;
        row.size = {0.0f, 40.0f};
        UiText text;
        text.text = "Row label";
        text.font = AssetRef("assets:/fonts/bench.ttf");
        text.size = 20.0f;
        (void)setParent(world, world.create(row, std::move(text)), content);
    }
    HierarchyIndex index;
    updateTransformHierarchy(world, index);
    BenchAssets assets;
    UiWorld ui(world);
    ui.setAssets(&assets);
    ui.update({{1920, 1080}});
    UiInput input;
    input.pointerPresent = true;
    input.pointerPixels = {200.0f, 400.0f};
    input.deltaSeconds = 1.0 / 60.0;
    u32 frame = 0;
    bench.minEpochIterations(50).unit("update").run("scroll 1,000 rows and record the 20 visible", [&] {
        input.wheel = (frame++ % 2 == 0) ? -1.0f : 1.0f;
        ui.update({{1920, 1080}}, input);
        ankerl::nanobench::doNotOptimizeAway(ui.drawData().vertices.size());
    });
}

// 1,000 images, each with a transition that is always moving: every update advances them, accumulates the
// transforms of every element again and records them, without laying out. Measured about 78 us on a desktop.
ILL_BENCHMARK("ui/transitions-1000", 500000.0)
{
    World world;
    UiCanvas canvas;
    canvas.useSafeArea = false;
    const Entity root = world.create(canvas);
    constexpr u32 kColumns = 40;
    UiTransition transition;
    transition.trigger = UiTransitionTrigger::Manual;
    transition.duration = 1.0e6f; // never arrives
    transition.ease = UiEase::BackOut;
    transition.offset = {8.0f, 4.0f};
    transition.scale = Vec2(1.2f);
    transition.opacity = 0.5f;
    transition.tint = {1.0f, 0.5f, 0.5f, 1.0f};
    for (u32 i = 0; i < kLayoutElements; ++i)
    {
        UiElement element;
        const Vec2 min(static_cast<f32>(i % kColumns) * 48.0f, static_cast<f32>(i / kColumns) * 43.0f);
        element.placement.offsetMin = min;
        element.placement.offsetMax = min + Vec2(44.0f, 40.0f);
        const UiTransitionState moving{
            .progress = 0.5f, .waited = 0.0f, .towardsChanged = true, .started = true};
        (void)setParent(world, world.create(element, UiImage{}, transition, moving), root);
    }
    HierarchyIndex index;
    updateTransformHierarchy(world, index);
    UiWorld ui(world);
    UiInput input;
    input.deltaSeconds = 1.0 / 60.0;
    ui.update({{1920, 1080}}, input);
    bench.minEpochIterations(50).unit("update").run("advance and record 1,000 running transitions", [&] {
        ui.update({{1920, 1080}}, input);
        ankerl::nanobench::doNotOptimizeAway(ui.drawData().vertices.size());
    });
}
