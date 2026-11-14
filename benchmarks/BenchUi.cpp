#include "Budget.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/ui/UiDrawList.hpp>

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
