#include "text/TextTestSupport.hpp"

#include <doctest/doctest.h>

#include <TextTools.hpp>
#include <string>
#include <string_view>
#include <utility>

using namespace illusion;
using namespace illusion::text;
using namespace illusion::text::tools;

namespace
{

constexpr std::string_view kEnglish = R"({
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "strings": {
    "app.files": "{count, plural, one {# file} other {# files}}",
    "app.greeting": "Hello, {name}!",
    "app.quit": "Quit"
  }
})";

// A Turkish translation of kEnglish with the given "strings" body.
std::string turkish(std::string_view strings, std::string_view plural = "tr")
{
    return std::string(
               R"({"format": "illusion.locale", "version": 1, "locale": "tr", "displayName": "Türkçe", "plural": ")") +
           std::string(plural) + R"(", "strings": {)" + std::string(strings) + "}}";
}

std::string source(std::string_view english)
{
    return sourceHashText(sourceHash(english));
}

// A complete, current translation entry for an English text.
std::string entry(std::string_view key, std::string_view text, std::string_view english)
{
    return "\"" + std::string(key) + "\": {\"text\": \"" + std::string(text) + "\", \"source\": \"" +
           source(english) + "\"}";
}

LocaleFile englishFile()
{
    CheckReport report;
    const std::optional<LocaleFile> file = checkEnglish(kEnglish, "en.json", report);
    REQUIRE(file.has_value());
    REQUIRE(report.diagnostics.empty());
    return *file;
}

CheckReport checkTurkish(std::string_view strings, bool strict = false, std::string_view plural = "tr")
{
    CheckReport report;
    checkTranslation(englishFile(), turkish(strings, plural), "tr.json", CheckOptions{strict}, report);
    return report;
}

const Diagnostic* findDiagnostic(const CheckReport& report, std::string_view key, std::string_view text)
{
    for (const Diagnostic& diagnostic : report.diagnostics)
    {
        if (diagnostic.key == key && diagnostic.message.find(text) != std::string::npos)
        {
            return &diagnostic;
        }
    }
    return nullptr;
}

const std::string kComplete = entry("app.files", "{count, plural, one {# dosya} other {# dosya}}",
                                    "{count, plural, one {# file} other {# files}}") +
                              ", " + entry("app.greeting", "Merhaba, {name}!", "Hello, {name}!") + ", " +
                              entry("app.quit", "Çık", "Quit");

} // namespace

TEST_CASE("a complete, current translation passes the check")
{
    const CheckReport report = checkTurkish(kComplete, true);
    INFO(report.format());
    CHECK(report.diagnostics.empty());
}

TEST_CASE("missing keys and stale sources are warnings, or errors when strict")
{
    const std::string partial = entry("app.greeting", "Merhaba, {name}!", "Hello, {name}!") +
                                R"(, "app.quit": {"text": "Çık", "source": "0000000000000000"})";
    const CheckReport report = checkTurkish(partial);
    CHECK(report.errorCount() == 0);
    CHECK(report.warningCount() == 2);
    const Diagnostic* missing = findDiagnostic(report, "app.files", "missing translation");
    REQUIRE(missing != nullptr);
    CHECK(missing->severity == Severity::Warning);
    CHECK(findDiagnostic(report, "app.quit", "stale translation") != nullptr);

    const CheckReport strict = checkTurkish(partial, true);
    CHECK(strict.errorCount() == 2);
    CHECK(strict.format().find("tr.json: error: app.files: missing translation") != std::string::npos);

    const CheckReport unsourced = checkTurkish(R"("app.quit": "Çık")");
    CHECK(findDiagnostic(unsourced, "app.quit", "no \"source\" hash") != nullptr);
}

TEST_CASE("extra keys, argument mismatches and unparsable patterns are errors")
{
    CHECK(findDiagnostic(checkTurkish(kComplete + ", " + entry("app.zzz", "Fazla", "x")), "app.zzz",
                         "not in the English source") != nullptr);

    const std::string renamed = entry("app.greeting", "Merhaba, {isim}!", "Hello, {name}!");
    const CheckReport renamedReport = checkTurkish(renamed);
    const Diagnostic* mismatch = findDiagnostic(renamedReport, "app.greeting", "differ from the English");
    REQUIRE(mismatch != nullptr);
    CHECK(mismatch->severity == Severity::Error);
    CHECK(mismatch->message.find("(isim)") != std::string::npos);

    const std::string broken = entry("app.greeting", "Merhaba, {name!", "Hello, {name}!");
    CHECK(findDiagnostic(checkTurkish(broken), "app.greeting", "does not parse") != nullptr);
}

TEST_CASE("a plural without every form of the locale's rule is an error")
{
    // Russian needs one, few, many and other.
    const std::string files = entry("app.files", "{count, plural, one {# файл} other {# файла}}",
                                    "{count, plural, one {# file} other {# files}}");
    const CheckReport report = checkTurkish(files, false, "ru");
    const Diagnostic* diagnostic = findDiagnostic(report, "app.files", "lacks the forms few, many");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity == Severity::Error);
}

TEST_CASE("unsorted keys, invalid keys and non-strict JSON are errors")
{
    const std::string unsorted =
        entry("app.quit", "Çık", "Quit") + ", " + entry("app.greeting", "Merhaba, {name}!", "Hello, {name}!");
    CHECK(findDiagnostic(checkTurkish(unsorted), "app.greeting", "not sorted") != nullptr);

    const CheckReport invalidKey = checkTurkish(R"("App.Quit": "Çık")");
    REQUIRE(invalidKey.errorCount() == 1);
    CHECK(invalidKey.diagnostics[0].message.find("not a valid key") != std::string::npos);

    for (const std::string_view json :
         {std::string_view(R"({"format": "illusion.locale", /* comment */ "version": 1})"),
          std::string_view(R"({"format": "illusion.locale", "version": 1,})")})
    {
        CheckReport report;
        checkTranslation(englishFile(), json, "tr.json", {}, report);
        REQUIRE(report.errorCount() == 1);
        CHECK(report.diagnostics[0].message.find("1:") == 0); // line and column of the JSON error
    }
}

TEST_CASE("the English source is checked as well")
{
    CheckReport report;
    const std::string wrongLocale =
        std::string(kEnglish).replace(std::string(kEnglish).find("\"en\""), 4, "\"fr\"");
    CHECK(checkEnglish(wrongLocale, "en.json", report).has_value());
    CHECK(report.errorCount() == 1);
    CHECK(report.diagnostics[0].message.find("must have locale \"en\"") != std::string::npos);

    CheckReport plural;
    const std::string noOne =
        std::string(kEnglish).replace(std::string(kEnglish).find("one {# file} "), 13, "");
    (void)checkEnglish(noOne, "en.json", plural);
    CHECK(findDiagnostic(plural, "app.files", "lacks the forms one") != nullptr);

    CheckReport broken;
    CHECK_FALSE(checkEnglish("[]", "en.json", broken).has_value());
    CHECK(broken.errorCount() == 1);
}

TEST_CASE("the test fixtures in tests/data/locales pass with warnings only")
{
    CheckReport report;
    const std::optional<LocaleFile> english =
        checkEnglish(test::readDataFile("locales/en.json"), "en.json", report);
    REQUIRE(english.has_value());
    checkTranslation(*english, test::readDataFile("locales/tr.json"), "tr.json", {}, report);
    INFO(report.format());
    CHECK(report.errorCount() == 0);
    CHECK(report.warningCount() == 3); // tr.json leaves out three keys
}

TEST_CASE("constant names drop the first key part and use PascalCase")
{
    CHECK(constantName("input.key.left_shift") == "kKeyLeftShift");
    CHECK(constantName("editor.menu.file.save") == "kMenuFileSave");
    CHECK(constantName("input.glyph.mouse_button4") == "kGlyphMouseButton4");
    CHECK(constantName("app.HUD_title") == "kHUDTitle");
}

TEST_CASE("the generator reproduces its golden output")
{
    GenerateOptions options;
    options.table = "test";
    options.nameSpace = "illusion::test::strings";
    options.headerInclude = "TestStrings.hpp";
    options.sourceName = "en.json";
    const auto generated = generateStringTable(test::readDataFile("locales/en.json"), options);
    REQUIRE_MESSAGE(generated.hasValue(), (generated ? std::string() : generated.error()));
    CHECK(generated->header == test::readDataFile("text/TestStrings.hpp.golden"));
    CHECK(generated->source == test::readDataFile("text/TestStrings.cpp.golden"));
}

TEST_CASE("the generator rejects invalid tables")
{
    GenerateOptions options;
    options.table = "app";
    options.nameSpace = "game::strings";
    options.headerInclude = "AppStrings.hpp";
    options.sourceName = "en.json";
    CHECK(generateStringTable(kEnglish, options).hasValue());

    // "app.greeting" and "app.greeting_" both become kGreeting; the keys are still sorted.
    const std::string collision = std::string(kEnglish).replace(
        std::string(kEnglish).find("\"app.quit\""), 10, "\"app.greeting_\": \"Hi\", \"app.quit\"");
    const auto collided = generateStringTable(collision, options);
    REQUIRE_FALSE(collided.hasValue());
    CHECK(collided.error().find("both become the constant kGreeting") != std::string::npos);

    GenerateOptions badTable = options;
    badTable.table = "App";
    CHECK_FALSE(generateStringTable(kEnglish, badTable).hasValue());
    GenerateOptions badNamespace = options;
    badNamespace.nameSpace = "game::";
    CHECK_FALSE(generateStringTable(kEnglish, badNamespace).hasValue());

    const auto empty = generateStringTable(R"({"format": "illusion.locale", "version": 1, "locale": "en",
        "displayName": "English", "plural": "en", "strings": {}})",
                                           options);
    REQUIRE_FALSE(empty.hasValue());
    CHECK(empty.error().find("no strings") != std::string::npos);
    CHECK_FALSE(generateStringTable("{", options).hasValue());
}

namespace
{

// kEnglish with the text of "app.quit" replaced by @p quit, or without the key when @p quit is empty.
LocaleFile englishWithQuit(std::string_view quit)
{
    std::string json(kEnglish);
    const usize at = json.find(",\n    \"app.quit\": \"Quit\"");
    REQUIRE(at != std::string::npos);
    json.replace(at, std::string_view(",\n    \"app.quit\": \"Quit\"").size(),
                 quit.empty() ? std::string() : ",\n    \"app.quit\": \"" + std::string(quit) + "\"");
    CheckReport report;
    const std::optional<LocaleFile> file = checkEnglish(json, "en.json", report);
    REQUIRE(file.has_value());
    return *file;
}

} // namespace

TEST_CASE("a string freeze records every English key with its source hash and reads back the same")
{
    const StringFreeze freeze = freezeStrings(englishFile(), "M7");
    CHECK(freeze.name == "M7");
    REQUIRE(freeze.strings.size() == 3);
    CHECK(freeze.strings[0].first == "app.files");
    CHECK(freeze.strings[1] == std::pair<std::string, u64>("app.greeting", sourceHash("Hello, {name}!")));
    CHECK(freeze.strings[2].first == "app.quit");

    const std::string text = writeFreeze(freeze);
    CHECK(text == "{\n  \"format\": \"illusion.string-freeze\",\n  \"version\": 1,\n  \"freeze\": \"M7\",\n"
                  "  \"strings\": {\n    \"app.files\": \"" +
                      source("{count, plural, one {# file} other {# files}}") +
                      "\",\n    \"app.greeting\": \"" + source("Hello, {name}!") +
                      "\",\n    \"app.quit\": \"" + source("Quit") + "\"\n  }\n}\n");
    const auto parsed = parseFreeze(text);
    REQUIRE(parsed.hasValue());
    CHECK(parsed->name == "M7");
    CHECK(parsed->strings == freeze.strings);

    const auto empty = parseFreeze(writeFreeze(StringFreeze{"empty", {}}));
    REQUIRE(empty.hasValue());
    CHECK(empty->strings.empty());
}

TEST_CASE("the freeze check fails on a frozen string that changed or is gone and accepts new keys")
{
    const StringFreeze freeze = freezeStrings(englishWithQuit("Quit"), "M7");
    CheckReport unchanged;
    checkFreeze(englishWithQuit("Quit"), freeze, "en.json", unchanged);
    CHECK(unchanged.diagnostics.empty());

    // A key added after the freeze is not part of it.
    CheckReport added;
    checkFreeze(englishWithQuit("Quit"), freezeStrings(englishWithQuit(""), "M7"), "en.json", added);
    CHECK(added.diagnostics.empty());

    CheckReport changed;
    checkFreeze(englishWithQuit("Exit"), freeze, "en.json", changed);
    REQUIRE(changed.diagnostics.size() == 1);
    CHECK(changed.diagnostics[0].severity == Severity::Error);
    CHECK(changed.diagnostics[0].key == "app.quit");
    CHECK(changed.diagnostics[0].message.find("frozen string (M7) changed from source hash " +
                                              source("Quit")) != std::string::npos);

    CheckReport removed;
    checkFreeze(englishWithQuit(""), freeze, "en.json", removed);
    REQUIRE(removed.diagnostics.size() == 1);
    CHECK(removed.errorCount() == 1);
    CHECK(removed.diagnostics[0].key == "app.quit");
    CHECK(removed.diagnostics[0].message.find("frozen (M7) and was removed") != std::string::npos);
}

TEST_CASE("freeze files and names are validated")
{
    CHECK(isValidFreezeName("M7"));
    CHECK(isValidFreezeName("v1.0-rc_2"));
    CHECK_FALSE(isValidFreezeName(""));
    CHECK_FALSE(isValidFreezeName("M 7"));
    CHECK_FALSE(isValidFreezeName("\"M7\""));
    CHECK_FALSE(isValidFreezeName(std::string(33, 'a')));

    const std::string hash = source("Quit");
    const auto file = [&](std::string_view head, std::string_view strings) {
        return std::string(head) + R"("strings": {)" + std::string(strings) + "}}";
    };
    constexpr std::string_view kHead =
        R"({"format": "illusion.string-freeze", "version": 1, "freeze": "M7", )";
    CHECK(parseFreeze(file(kHead, "\"app.quit\": \"" + hash + "\"")).hasValue());
    CHECK_FALSE(parseFreeze("{").hasValue());
    CHECK(parseFreeze(file(R"({"format": "illusion.locale", "version": 1, "freeze": "M7", )", ""))
              .error()
              .find("format") != std::string::npos);
    CHECK(parseFreeze(file(R"({"format": "illusion.string-freeze", "version": 2, "freeze": "M7", )", ""))
              .error()
              .find("version") != std::string::npos);
    CHECK(parseFreeze(file(R"({"format": "illusion.string-freeze", "version": 1, "freeze": "M 7", )", ""))
              .error()
              .find("freeze") != std::string::npos);
    CHECK_FALSE(
        parseFreeze(R"({"format": "illusion.string-freeze", "version": 1, "freeze": "M7", "strings": []})")
            .hasValue());
    CHECK(parseFreeze(file(kHead, "\"app.quit\": \"" + hash.substr(1) + "\"")).error().find("16-digit") !=
          std::string::npos);
    CHECK(parseFreeze(file(kHead, R"("app.quit": "0123456789ABCDEF")")).error().find("16-digit") !=
          std::string::npos);
    CHECK(parseFreeze(file(kHead, "\"quit\": \"" + hash + "\"")).error().find("valid key") !=
          std::string::npos);
    CHECK(parseFreeze(file(kHead, "\"app.quit\": \"" + hash + "\", \"app.files\": \"" + hash + "\""))
              .error()
              .find("sorted") != std::string::npos);
}
