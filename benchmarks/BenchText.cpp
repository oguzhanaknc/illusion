#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/input/InputStrings.hpp>
#include <illusion/text/FuzzyMatcher.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/SearchFold.hpp>
#include <illusion/text/TextOutput.hpp>

#include <array>
#include <string>
#include <string_view>

using namespace illusion;

// Budgets are about 8x the result measured on a desktop (Release, MSVC): lookup 2.5 ns, plural 48 ns, fold
// 2.4 ns per byte, fuzzy score 94 ns per candidate. See Budget.hpp.

ILL_BENCHMARK("text/lookup", 20.0)
{
    text::Localization localization;
    localization.addTable(input::strings::kTable);
    constexpr std::array keys = {input::strings::kKeySpace, input::strings::kGlyphPsCross,
                                 input::strings::kKeyF12, input::strings::kGlyphMouseLeft};
    usize index = 0;
    bench.run("Localization::message", [&] {
        const text::MessageFormat& message = localization.message(keys[index++ & 3]);
        ankerl::nanobench::doNotOptimizeAway(&message);
    });
}

ILL_BENCHMARK("text/format-plural", 400.0)
{
    auto compiled = text::MessageFormat::compile(
        "{count, plural, =0 {No entities selected} one {# entity selected} other {# entities selected}}");
    ILL_CHECK(compiled.hasValue(), "the benchmark message does not compile");
    const text::MessageFormat message = std::move(compiled).value();
    const text::MessageContext context{text::PluralRule::Russian, text::NumberFormat(",", " ")};
    LinearAllocator arena(4096);
    i64 count = 0;
    bench.run("format a plural message into an arena", [&] {
        arena.reset();
        text::TextOutput out(arena);
        const auto formatted = message.format({{"count", count++ % 2000}}, context, out);
        ILL_CHECK(formatted.hasValue(), "the benchmark message cannot be formatted");
        ankerl::nanobench::doNotOptimizeAway(out.view().data());
    });
}

ILL_BENCHMARK("text/fold", 20.0)
{
    const std::string sample = "Işık Ayarları – Shadow Cascades, Ölçek: 1.25 – Ёлка Ἀθήνα Straße ";
    std::string folded;
    folded.reserve(sample.size() * 2);
    bench.batch(sample.size()).unit("byte").run("searchFold", [&] {
        folded.clear();
        text::appendSearchFold(sample, folded);
        ankerl::nanobench::doNotOptimizeAway(folded.data());
    });
}

ILL_BENCHMARK("text/fuzzy-score", 750.0)
{
    constexpr std::array<std::string_view, 8> kCandidates = {
        "Save Scene",         "Save Scene As...",         "Open Project Settings", "Toggle Shadow Cascades",
        "Show Asset Browser", "Physics: Step Simulation", "Audio Mixer",           "Input Actions: Rebind",
    };
    text::FuzzyMatcher matcher("sv sc");
    usize index = 0;
    bench.unit("candidate").run("FuzzyMatcher::score", [&] {
        const auto score = matcher.score(kCandidates[index++ & 7]);
        ankerl::nanobench::doNotOptimizeAway(score);
    });
}
