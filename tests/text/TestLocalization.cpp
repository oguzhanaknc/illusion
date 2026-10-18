#include "TestStrings.hpp"
#include "TestSupport.hpp"
#include "text/TextTestSupport.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputStrings.hpp>
#include <illusion/text/Localization.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using namespace illusion;
using namespace illusion::text;
namespace strings = illusion::test::strings;

namespace
{

LocaleId locale(std::string_view tag)
{
    const std::optional<LocaleId> id = LocaleId::parse(tag);
    REQUIRE(id.has_value());
    return *id;
}

std::string format(const Localization& localization, TextKey key,
                   std::initializer_list<MessageArgument> arguments = {})
{
    std::string out;
    TextOutput output(out);
    const auto result = localization.format(key, arguments, output);
    CHECK_MESSAGE(result.hasValue(), (result ? std::string() : result.error().message));
    return out;
}

// A localization with the test table and the Turkish fixture, Turkish (Turkey) active.
struct TurkishLocalization
{
    Localization localization;

    TurkishLocalization()
    {
        localization.addTable(strings::kTable);
        REQUIRE(localization.addLocale(test::readDataFile("locales/tr.json")).hasValue());
        localization.setLocale(locale("tr-TR"));
        localization.applyPendingLocale();
    }
};

} // namespace

TEST_CASE("generated tables carry their keys and English text")
{
    CHECK(strings::kTable.name == "test");
    CHECK(strings::kTable.keys.size() == 7);
    CHECK(strings::kGreeting.name() == "test.greeting");
    CHECK(strings::kGreeting.english() == "Hello, {name}!");
    CHECK(strings::kMenuSave.name() == "test.menu.save");
    CHECK(strings::kMenuSave != strings::kMenuQuit);
    CHECK(std::is_sorted(strings::kTable.keys.begin(), strings::kTable.keys.end()));
}

TEST_CASE("English is active until another locale is applied")
{
    Localization localization;
    localization.addTable(strings::kTable);
    CHECK(localization.locale().tag() == "en");
    CHECK(format(localization, strings::kGreeting, {{"name", "Ada"}}) == "Hello, Ada!");
    CHECK(format(localization, strings::kFilesCount, {{"count", 2}}) == "2 files");
    CHECK(format(localization, strings::kQuote) == "It's {literal}");
    CHECK(localization.missingKeyCount() == 0);
}

TEST_CASE("a locale switch takes effect at the frame boundary")
{
    Localization localization;
    localization.addTable(strings::kTable);
    REQUIRE(localization.addLocale(test::readDataFile("locales/tr.json")).hasValue());
    localization.setLocale(locale("tr"));
    CHECK(localization.locale().tag() == "en"); // not yet
    CHECK(format(localization, strings::kMenuSave) == "Save");
    localization.applyPendingLocale();
    CHECK(localization.locale().tag() == "tr");
    CHECK(format(localization, strings::kMenuSave) == "Kaydet");

    // Adding a file for the active locale also waits for the boundary.
    REQUIRE(
        localization
            .addLocale(
                R"json({"format": "illusion.locale", "version": 1, "locale": "tr", "displayName": "Türkçe",
                               "plural": "tr", "strings": {"test.menu.save": "Kaydet!"}})json")
            .hasValue());
    CHECK(format(localization, strings::kMenuSave) == "Kaydet");
    localization.applyPendingLocale();
    CHECK(format(localization, strings::kMenuSave) == "Kaydet!");
}

TEST_CASE("keys fall back per key from tr-TR to tr to English")
{
    TurkishLocalization fixture;
    Localization& localization = fixture.localization;
    REQUIRE(
        localization
            .addLocale(
                R"json({"format": "illusion.locale", "version": 1, "locale": "tr-TR", "displayName": "Türkçe (Türkiye)",
                               "plural": "tr", "strings": {"test.greeting": "Selam, {name}!"}})json")
            .hasValue());
    localization.applyPendingLocale();
    CHECK(format(localization, strings::kGreeting, {{"name", "Ada"}}) == "Selam, Ada!"); // tr-TR
    CHECK(format(localization, strings::kMenuSave) == "Kaydet");                         // tr
    CHECK(format(localization, strings::kMenuQuit) == "Quit");                           // English
}

TEST_CASE("the active locale's plural rule and number format apply, also to English fallbacks")
{
    TurkishLocalization fixture;
    const Localization& localization = fixture.localization;
    CHECK(localization.context().plural == PluralRule::Turkish);
    CHECK(format(localization, strings::kFilesCount, {{"count", 0}}) == "Dosya yok");
    CHECK(format(localization, strings::kFilesCount, {{"count", 1}}) == "1 dosya");
    CHECK(format(localization, strings::kFilesCount, {{"count", 1234}}) == "1.234 dosya");
    CHECK(format(localization, strings::kFilesSize, {{"bytes", 1234.5}}) == "Size: 1.234,5 bytes");
    CHECK(format(localization, strings::kPlayerJoined, {{"gender", "female"}, {"name", "Ada"}}) ==
          "Ada takıma katıldı");
}

TEST_CASE("a key that falls back to English is reported once")
{
    TurkishLocalization fixture;
    const Localization& localization = fixture.localization;
    CHECK(localization.missingKeyCount() == 0);
    (void)format(localization, strings::kMenuQuit);
    (void)format(localization, strings::kMenuQuit);
    CHECK(localization.missingKeyCount() == 1);
    (void)format(localization, strings::kQuote);
    CHECK(localization.missingKeyCount() == 2);
    (void)format(localization, strings::kMenuSave); // translated
    CHECK(localization.missingKeyCount() == 2);
}

#if !defined(NDEBUG)
TEST_CASE("the English fallback of a key is logged once at Debug level")
{
    test::ScopedMemoryLogger log(LogLevel::Debug);
    TurkishLocalization fixture;
    (void)format(fixture.localization, strings::kMenuQuit);
    (void)format(fixture.localization, strings::kMenuQuit);
    usize mentions = 0;
    for (const MemorySink::Entry& entry : log.sink->entries())
    {
        mentions += entry.text.find("test.menu.quit") != std::string::npos ? 1 : 0;
    }
    CHECK(mentions == 1);
}
#endif

TEST_CASE("English locales fall back to the compiled English text without reports")
{
    Localization localization;
    localization.addTable(strings::kTable);
    localization.setLocale(locale("en-GB"));
    localization.applyPendingLocale();
    CHECK(format(localization, strings::kMenuQuit) == "Quit");
    CHECK(localization.missingKeyCount() == 0);

    // A locale without any file shows English with the plural rule of its language.
    localization.setLocale(locale("ru"));
    localization.applyPendingLocale();
    CHECK(localization.context().plural == PluralRule::Russian);
}

TEST_CASE("the pseudo-locale shows every string pseudo-localized")
{
    Localization localization;
    localization.addTable(strings::kTable);
    localization.setLocale(pseudoLocale());
    localization.applyPendingLocale();
    const std::string greeting = format(localization, strings::kGreeting, {{"name", "Ada"}});
    CHECK(greeting.front() == '[');
    CHECK(greeting.find("Ada") != std::string::npos);
    CHECK(greeting.find("Ĥéľľö") != std::string::npos);
    CHECK(localization.missingKeyCount() == 0);
    const std::string files = format(localization, strings::kFilesCount, {{"count", 3}});
    CHECK(files.find('3') != std::string::npos);
}

TEST_CASE("translations that do not compile or use unknown arguments are skipped")
{
    test::ScopedMemoryLogger log;
    Localization localization;
    localization.addTable(strings::kTable);
    REQUIRE(
        localization
            .addLocale(
                R"json({"format": "illusion.locale", "version": 1, "locale": "de", "displayName": "Deutsch",
                               "plural": "de", "strings": {"test.greeting": "Hallo, {nmae}!", "test.menu.quit": "{",
                               "test.menu.save": "Speichern"}})json")
            .hasValue());
    localization.setLocale(locale("de"));
    localization.applyPendingLocale();
    CHECK(format(localization, strings::kGreeting, {{"name", "Ada"}}) == "Hello, Ada!");
    CHECK(format(localization, strings::kMenuQuit) == "Quit");
    CHECK(format(localization, strings::kMenuSave) == "Speichern");
    const std::vector<MemorySink::Entry> warnings = log.sink->entries();
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].text.find("\"test.menu.quit\" does not compile") != std::string::npos);
    CHECK(warnings[1].text.find("\"test.greeting\" uses arguments the English text does not have") !=
          std::string::npos);
}

TEST_CASE("localizations list locales, find keys by name and reject invalid files")
{
    TurkishLocalization fixture;
    Localization& localization = fixture.localization;
    const std::vector<LocaleId> locales = localization.availableLocales();
    REQUIRE(locales.size() == 3);
    CHECK(locales[0].tag() == "en");
    CHECK(locales[1].tag() == "en-XA");
    CHECK(locales[2].tag() == "tr");

    const MessageFormat* save = localization.find("test.menu.save");
    REQUIRE(save != nullptr);
    CHECK(test::formatted(*save, {}) == "Kaydet");
    CHECK(localization.find("test.unknown") == nullptr);

    const auto invalid = localization.addLocale("{\"format\": \"illusion.locale\",}");
    REQUIRE_FALSE(invalid.hasValue());
    CHECK(invalid.error().line == 1);
    localization.addTable(strings::kTable); // a second registration is ignored
    CHECK(format(localization, strings::kMenuSave) == "Kaydet");
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("keys of unregistered tables assert")
{
    test::ScopedAssertCatcher catcher;
    Localization localization;
    CHECK_THROWS_AS((void)localization.message(strings::kGreeting), test::AssertionFailure);
}
#endif

TEST_CASE("input glyph labels have translatable keys")
{
    CHECK(keyLabelKey(Key::LeftControl).name() == "input.key.left_control");
    CHECK(keyLabelKey(Key::LeftControl).english() == "Left Ctrl");
    CHECK(keyLabelKey(Key::Num1).english() == "1");
    CHECK(
        glyphLabelKey(InputBinding::gamepadButton(GamepadButton::South), GamepadFamily::PlayStation).name() ==
        "input.glyph.ps_cross");
    CHECK(
        glyphLabelKey(InputBinding::gamepadButton(GamepadButton::South), GamepadFamily::Nintendo).english() ==
        "B");
    CHECK(
        glyphLabelKey(InputBinding::gamepadAxis(GamepadAxis::RightTrigger), GamepadFamily::Xbox).english() ==
        "RT");
    CHECK(glyphLabelKey(InputBinding::gamepadStick(GamepadStick::Left), GamepadFamily::Generic).english() ==
          "Left Stick");
    CHECK(glyphLabelKey(InputBinding::mouseButton(MouseButton::Right), GamepadFamily::Xbox).english() ==
          "RMB");
    CHECK(glyphLabelKey(InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D), GamepadFamily::Xbox) ==
          input::strings::kGlyphKeyVectorCompact);
    CHECK(glyphLabelKey(InputBinding::keyVector(Key::Up, Key::Down, Key::Left, Key::Right),
                        GamepadFamily::Xbox) == input::strings::kGlyphKeyVector);

    // Every gamepad glyph id has a key named after it.
    for (const GamepadFamily family :
         {GamepadFamily::Xbox, GamepadFamily::PlayStation, GamepadFamily::Nintendo, GamepadFamily::Generic})
    {
        for (u16 button = 0; button < static_cast<u16>(GamepadButton::Count); ++button)
        {
            const InputBinding binding = InputBinding::gamepadButton(static_cast<GamepadButton>(button));
            CHECK(glyphLabelKey(binding, family).name() == "input.glyph." + glyphId(binding, family));
        }
        for (u16 axis = 0; axis < static_cast<u16>(GamepadAxis::Count); ++axis)
        {
            const InputBinding binding = InputBinding::gamepadAxis(static_cast<GamepadAxis>(axis));
            CHECK(glyphLabelKey(binding, family).name() == "input.glyph." + glyphId(binding, family));
        }
    }
}

TEST_CASE("input glyph labels follow the active language")
{
    Localization localization;
    localization.addTable(input::strings::kTable);
    const InputBinding wasd = InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D);
    const InputBinding arrows = InputBinding::keyVector(Key::Up, Key::Down, Key::Left, Key::Right);
    CHECK(glyphLabel(wasd, GamepadFamily::Xbox, localization) == "WASD");
    CHECK(glyphLabel(arrows, GamepadFamily::Xbox, localization) == "Up/Down/Left/Right");
    CHECK(glyphLabel(InputBinding::key(Key::LeftControl), GamepadFamily::Xbox, localization) == "Left Ctrl");

    REQUIRE(
        localization
            .addLocale(
                R"json({"format": "illusion.locale", "version": 1, "locale": "tr", "displayName": "Türkçe",
                               "plural": "tr", "strings": {
                                 "input.glyph.ps_cross": "Çarpı",
                                 "input.key.down": "Aşağı", "input.key.left": "Sol", "input.key.left_control": "Sol Ctrl",
                                 "input.key.right": "Sağ", "input.key.up": "Yukarı"}})json")
            .hasValue());
    localization.setLocale(locale("tr"));
    localization.applyPendingLocale();
    CHECK(glyphLabel(arrows, GamepadFamily::Xbox, localization) == "Yukarı/Aşağı/Sol/Sağ");
    CHECK(glyphLabel(wasd, GamepadFamily::Xbox, localization) == "WASD");
    CHECK(glyphLabel(InputBinding::key(Key::LeftControl), GamepadFamily::Xbox, localization) == "Sol Ctrl");
    CHECK(glyphLabel(InputBinding::gamepadButton(GamepadButton::South), GamepadFamily::PlayStation,
                     localization) == "Çarpı");
    // The English label functions are unchanged.
    CHECK(glyphLabel(InputBinding::key(Key::LeftControl), GamepadFamily::Xbox) == "LeftControl");
}
