#include <illusion/text/FuzzyMatcher.hpp>
#include <illusion/text/SearchFold.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

using namespace illusion;
using namespace illusion::text;
using namespace std::string_view_literals;

TEST_CASE("search folding treats every Turkish i as i and drops diacritics")
{
    for (const std::string_view i : {"I"sv, "i"sv, "\xC4\xB1"sv, "\xC4\xB0"sv}) // I i ı İ
    {
        CHECK(searchFold(i) == "i");
    }
    CHECK(searchFold("I\xC5\x9F\xC4\xB1k Ayarlar\xC4\xB1") == "isik ayarlari"); // Işık Ayarları
    CHECK(searchFold("\xC5\x9E\xC4\x9E\xC3\x9C\xC3\x96\xC3\x87") == "sguoc");   // ŞĞÜÖÇ
    CHECK(searchFold("Cr\xC3\xA8me Br\xC3\xBBl\xC3\xA9") == "creme brule");     // Crème Brûlée
}

TEST_CASE("search folding expands ligatures and folds letters without decompositions")
{
    CHECK(searchFold("Stra\xC3\x9F"
                     "e") == "strasse"); // Straße
    CHECK(searchFold("\xC3\x86ther \xC5\x92uvre") == "aether oeuvre");
    CHECK(searchFold("\xC5\x81\xC3\xB3"
                     "d\xC5\xBA") == "lodz");                // Łódź
    CHECK(searchFold("\xC4\x90\xC3\x98\xC3\x9E") == "doth"); // ĐØÞ
    const FoldedCodePoint sharpS = searchFold(U'ß');
    CHECK(sharpS.count == 2);
    CHECK(sharpS.codePoints[0] == U's');
    CHECK(sharpS.codePoints[1] == U's');
}

TEST_CASE("search folding lowercases Greek and Cyrillic and removes their accents")
{
    CHECK(searchFold("\xCE\x86\xCE\x98\xCE\x97\xCE\x9D\xCE\x91") ==
          "\xCE\xB1\xCE\xB8\xCE\xB7\xCE\xBD\xCE\xB1"); // ΆΘΗΝΑ -> αθηνα
    CHECK(searchFold("\xCF\x82") == "\xCF\x83");       // ς -> σ
    CHECK(searchFold("\xD0\x81\xD0\xBB\xD0\xBA\xD0\xB0") ==
          "\xD0\xB5\xD0\xBB\xD0\xBA\xD0\xB0");                                   // Ёлка -> елка
    CHECK(searchFold("\xD0\x99\xD0\xBE\xD0\xB4") == "\xD0\xB8\xD0\xBE\xD0\xB4"); // Йод -> иод
}

TEST_CASE("search folding keeps other characters")
{
    CHECK(searchFold("\xE6\x97\xA5\xE6\x9C\xAC 42 \xC3\x97 \xC3\xB7 _-!") ==
          "\xE6\x97\xA5\xE6\x9C\xAC 42 \xC3\x97 \xC3\xB7 _-!");
    CHECK(searchFold("bad \xFF byte") == "bad \xEF\xBF\xBD byte");
    CHECK(searchFold("") == "");
}

TEST_CASE("fuzzy matching needs the pattern as a subsequence and ignores case, accents and spaces")
{
    FuzzyMatcher matcher("sv sc");
    CHECK(matcher.patternLength() == 4);
    CHECK(matcher.score("Save Scene").has_value());
    CHECK_FALSE(matcher.score("Scene Save").has_value());
    CHECK_FALSE(matcher.score("").has_value());

    FuzzyMatcher accents("isik");
    CHECK(accents.score("I\xC5\x9F\xC4\xB1k Ayarlar\xC4\xB1").has_value()); // Işık Ayarları

    FuzzyMatcher empty("");
    CHECK(empty.score("anything") == 0);
    CHECK(empty.score("") == 0);
}

TEST_CASE("fuzzy ranking prefers word starts, runs, humps and early matches")
{
    // Each row: pattern, the candidate that must rank higher, the candidate that must rank lower.
    struct Row
    {
        std::string_view pattern;
        std::string_view better;
        std::string_view worse;
    };
    const Row rows[] = {
        {"scene", "Scene", "Save Scene"},         // earlier
        {"scene", "Save Scene", "Obscene"},       // word start against the middle of a word
        {"ss", "Save Scene", "Assets"},           // two word starts against a run inside a word
        {"gp", "gamePad", "gamepad"},             // camel-case hump
        {"abc", "xabc", "xaxbxc"},                // a run against scattered matches
        {"x", "xyz", "aaxyz"},                    // a match at the start
        {"f1", "Key F1", "Keyf1"},                // a word start, and a digit after letters
        {"ast", "Asset Store", "Last Saved Tab"}, // the first character at a word start
    };
    for (const Row& row : rows)
    {
        INFO(row.pattern, " ", row.better, " / ", row.worse);
        FuzzyMatcher matcher(row.pattern);
        const auto better = matcher.score(row.better);
        const auto worse = matcher.score(row.worse);
        REQUIRE(better.has_value());
        REQUIRE(worse.has_value());
        CHECK(*better > *worse);
    }
}

TEST_CASE("fuzzy scores are deterministic and the matcher can be reused and moved")
{
    FuzzyMatcher matcher("opn");
    const auto first = matcher.score("Open Project");
    (void)matcher.score(std::string(300, 'o') + "pn"); // grows the scratch memory
    CHECK(matcher.score("Open Project") == first);
    FuzzyMatcher moved = std::move(matcher);
    CHECK(moved.score("Open Project") == first);
}
