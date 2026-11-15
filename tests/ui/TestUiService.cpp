// UiWorldService, the host's UiService: finding elements, texts by key with arguments, values, visibility,
// focus, events and the language, over a World, its UiWorld and a Localization.

#include "FontTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/StringTable.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWorld.hpp>
#include <illusion/ui/UiWorldService.hpp>

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr UVec2 kHd{1920, 1080};

class FontAssets final : public UiAssets
{
public:
    UiFontFace font(const AssetRef&) override { return {&test::testFont(), UiTextureId{3}}; }
    UiImageSource image(const AssetRef&) override { return {}; }
};

constexpr std::string_view kKeys[] = {"game.apples", "game.hello", "game.title"};
constexpr std::string_view kEnglish[] = {"{count, plural, one {#apple} other {#apples}}", "Hi{name}!",
                                         "Illusion"};
const text::StringTableData kTable{"game", kKeys, kEnglish};

constexpr std::string_view kTurkish = R"({"format": "illusion.locale", "version": 1, "locale": "tr",
    "displayName": "Turkce", "plural": "tr", "number": {"decimal": ",", "group": "."}, "fallback": "en",
    "strings": {"game.apples": "{count, plural, one {#elma} other {#elmalar}}"}})";

// A UI with a canvas, a text and a button, and the service over it.
struct Fixture
{
    Fixture()
    {
        reflectCoreComponents();
        registerUiComponents();
        localization.addTable(kTable);
        REQUIRE(localization.addLocale(kTurkish).hasValue());
        ui.setAssets(&assets);
        ui.setLocalization(&localization);
        UiCanvas settings;
        settings.useSafeArea = false;
        canvas = world.create(settings, Name{"Menu"});
        UiElement wide;
        wide.placement.anchorMax = {1.0f, 0.0f};
        wide.placement.offsetMax = {0.0f, 100.0f};
        UiText text;
        text.font = AssetRef("assets:/fonts/test.ttf");
        text.wrap = false;
        label = child(canvas, wide, text, Name{"Label"});
        UiElement square;
        square.placement.offsetMin = {100.0f, 200.0f};
        square.placement.offsetMax = {300.0f, 300.0f};
        button = child(canvas, square, UiInteractable{}, UiButton{}, Name{"Button"});
    }

    template <class... Ts>
    Entity child(Entity parent, Ts&&... components)
    {
        const Entity entity = world.create(std::forward<Ts>(components)...);
        REQUIRE(setParent(world, entity, parent).hasValue());
        return entity;
    }

    void step(const UiInput& input = {})
    {
        localization.applyPendingLocale();
        updateTransformHierarchy(world, index);
        ui.update({kHd}, input);
    }

    // Glyphs drawn this update: one quad each, the texts having no spaces.
    [[nodiscard]] usize glyphs() const { return ui.drawData().vertices.size() / 4; }

    World world;
    HierarchyIndex index;
    FontAssets assets;
    text::Localization localization;
    UiWorld ui{world};
    UiWorldService service{world, ui, localization};
    Entity canvas{};
    Entity label{};
    Entity button{};
};

} // namespace

TEST_CASE("UiService::find searches a subtree in hierarchy order, or every canvas in draw order")
{
    Fixture f;
    const Entity inner = f.child(f.button, UiElement{}, Name{"Label"});
    UiCanvas front;
    front.order = 5;
    const Entity overlay = f.world.create(front, Name{"Overlay"});
    const Entity overlayLabel = f.child(overlay, UiElement{}, Name{"Label"});
    UiCanvas back;
    back.order = -1;
    const Entity under = f.world.create(back, Name{"Under"});
    const Entity underLabel = f.child(under, UiElement{}, Name{"Label"});

    CHECK(f.service.find({}, "Label") == underLabel); // the lowest order is drawn first
    CHECK(f.service.find({}, "Overlay") == overlay);
    CHECK(f.service.find(f.canvas, "Label") == f.label); // pre-order: the canvas's first child first
    CHECK(f.service.find(f.button, "Label") == inner);
    CHECK(f.service.find(f.button, "Button") == f.button);
    CHECK(f.service.find(overlay, "Label") == overlayLabel);
    CHECK(f.service.find(f.canvas, "Nothing").isNull());
    const Entity loose = f.world.create(Name{"Loose"});
    CHECK(f.service.find(loose, "Loose") == loose); // an Entity outside any hierarchy
    CHECK(f.service.find({}, "Loose").isNull());    // not under a canvas

    {
        test::ScopedAllocationCounter allocations;
        static_cast<void>(f.service.find({}, "Button"));
        static_cast<void>(f.service.find(f.canvas, "Nothing"));
        CHECK(allocations.count() == 0);
    }
    f.world.destroy(under);
    CHECK(f.service.find(under, "Label").isNull());
}

TEST_CASE("UiService texts: literals, keys with number and text arguments, and their limits")
{
    Fixture f;
    CHECK(f.service.setText(f.label, "Hello"));
    f.step();
    CHECK(f.glyphs() == 5);

    const std::array apples{UiTextArgument::number("count", 1.0)};
    CHECK(f.service.setTextKey(f.label, "game.apples", apples));
    CHECK(f.world.get<UiText>(f.label).key == "game.apples");
    REQUIRE(f.world.has<UiTextArguments>(f.label));
    f.step();
    CHECK(f.glyphs() == std::string_view("1apple").size());

    const std::array three{UiTextArgument::number("count", 3.0)};
    CHECK(f.service.setTextKey(f.label, "game.apples", three));
    f.step();
    CHECK(f.glyphs() == std::string_view("3apples").size());
    const std::array half{UiTextArgument::number("count", 1.5)};
    CHECK(f.service.setTextKey(f.label, "game.apples", half));
    f.step();
    CHECK(f.glyphs() == std::string_view("1.5apples").size());

    const std::array name{UiTextArgument::string("name", "Ada")};
    CHECK(f.service.setTextKey(f.label, "game.hello", name));
    f.step();
    CHECK(f.glyphs() == std::string_view("HiAda!").size());

    // The same text again changes nothing, so the layout is kept.
    CHECK(f.service.setTextKey(f.label, "game.hello", name));
    f.step();
    CHECK(f.ui.statistics().layoutReused);

    CHECK(f.service.setTextKey(f.label, "game.title"));
    CHECK_FALSE(f.world.has<UiTextArguments>(f.label));
    f.step();
    CHECK(f.glyphs() == std::string_view("Illusion").size());

    CHECK(f.service.setTextKey(f.label, "game.hello", name));
    CHECK(f.service.setText(f.label, "Bye"));
    CHECK(f.world.get<UiText>(f.label).key.empty());
    CHECK_FALSE(f.world.has<UiTextArguments>(f.label));
    f.step();
    CHECK(f.glyphs() == 3);

    // Limits: 8 arguments, names of 1 to 31 bytes, texts up to 255 bytes; a key.
    std::array<UiTextArgument, 9> many{};
    many.fill(UiTextArgument::number("n", 1.0));
    CHECK(f.service.setTextKey(f.label, "game.title", std::span(many).first(8)));
    CHECK_FALSE(f.service.setTextKey(f.label, "game.title", many));
    const std::string longName(32, 'n');
    const std::string longText(256, 't');
    CHECK(f.service.setTextKey(f.label, "game.title",
                               std::array{UiTextArgument::string(std::string_view(longName).substr(1),
                                                                 std::string_view(longText).substr(1))}));
    CHECK_FALSE(
        f.service.setTextKey(f.label, "game.title", std::array{UiTextArgument::number(longName, 1.0)}));
    CHECK_FALSE(
        f.service.setTextKey(f.label, "game.title", std::array{UiTextArgument::string("t", longText)}));
    CHECK_FALSE(f.service.setTextKey(f.label, "game.title", std::array{UiTextArgument::number("", 1.0)}));
    CHECK_FALSE(f.service.setTextKey(f.label, ""));

    // Entities without a UiText, and dead ones.
    CHECK_FALSE(f.service.setText(f.button, "x"));
    CHECK_FALSE(f.service.setTextKey(f.button, "game.title"));
    const Entity gone = f.world.create(UiElement{}, UiText{});
    f.world.destroy(gone);
    CHECK_FALSE(f.service.setText(gone, "x"));
    CHECK_FALSE(f.service.setTextKey(gone, "game.title"));
    CHECK_FALSE(f.service.setText({}, "x"));
}

TEST_CASE("UiService switches the language to a locale with a file, and keyed texts follow")
{
    Fixture f;
    CHECK(f.service.language() == "en");
    const std::array three{UiTextArgument::number("count", 3.0)};
    REQUIRE(f.service.setTextKey(f.label, "game.apples", three));
    f.step();
    CHECK(f.glyphs() == std::string_view("3apples").size());

    CHECK(f.service.setLanguage("tr"));
    CHECK(f.service.language() == "tr");
    f.step();
    CHECK_FALSE(f.ui.statistics().layoutReused);
    CHECK(f.glyphs() == std::string_view("3elmalar").size());
    const std::array one{UiTextArgument::number("count", 1.0)};
    REQUIRE(f.service.setTextKey(f.label, "game.apples", one));
    f.step();
    CHECK(f.glyphs() == std::string_view("1elma").size());
    const std::array half{UiTextArgument::number("count", 1.5)};
    REQUIRE(f.service.setTextKey(f.label, "game.apples", half));
    f.step();
    CHECK(f.glyphs() == std::string_view("1,5elmalar").size());

    CHECK(f.service.setLanguage("tr-TR")); // through its parent's file
    CHECK(f.service.language() == "tr-TR");
    CHECK_FALSE(f.service.setLanguage("de"));
    CHECK_FALSE(f.service.setLanguage("not a tag"));
    CHECK(f.service.language() == "tr-TR");
    CHECK(f.service.setLanguage("en"));
    f.step();
    CHECK(f.localization.locale().tag() == "en");
    CHECK(f.glyphs() == std::string_view("1.5apples").size());
}

TEST_CASE("UiService shows and hides subtrees and sets interactable, toggle and slider values")
{
    Fixture f;
    const Entity inner = f.child(f.button, UiElement{}, UiImage{});
    CHECK(f.service.isVisible(inner));
    CHECK(f.service.setVisible(f.button, false));
    CHECK_FALSE(f.service.isVisible(f.button));
    CHECK_FALSE(f.service.isVisible(inner));
    f.step();
    CHECK(f.world.has<Inactive>(inner));
    CHECK(f.service.setVisible(f.button, true));
    CHECK(f.service.isVisible(inner));

    CHECK(f.service.setInteractable(f.button, false));
    CHECK_FALSE(f.world.get<UiInteractable>(f.button).enabled);
    CHECK_FALSE(f.service.setInteractable(f.label, false));

    UiElement place;
    place.placement.offsetMax = {100.0f, 20.0f};
    UiSlider range;
    range.min = 1.0f;
    range.max = 5.0f;
    range.step = 0.5f;
    const Entity slider = f.child(f.canvas, place, UiInteractable{}, range);
    const Entity toggle = f.child(f.canvas, place, UiInteractable{}, UiToggle{});
    f.step();
    CHECK(f.ui.statistics().layoutReused == false);
    f.step();
    REQUIRE(f.ui.statistics().layoutReused);

    CHECK(f.service.setSliderValue(slider, 2.3f));
    CHECK(f.service.sliderValue(slider) == 2.5f);
    CHECK(f.service.setSliderValue(slider, 9.0f));
    CHECK(f.service.sliderValue(slider) == 5.0f);
    CHECK(f.service.setToggle(toggle, true));
    CHECK(f.service.toggle(toggle) == true);
    f.step();
    CHECK_FALSE(f.ui.statistics().layoutReused);
    CHECK(f.ui.events().empty()); // values set by code report no events

    CHECK_FALSE(f.service.setSliderValue(toggle, 1.0f));
    CHECK_FALSE(f.service.sliderValue(toggle).has_value());
    CHECK_FALSE(f.service.setToggle(slider, true));
    CHECK_FALSE(f.service.toggle(slider).has_value());
    f.world.destroy(slider);
    CHECK_FALSE(f.service.setVisible(slider, true));
    CHECK_FALSE(f.service.isVisible(slider));
    CHECK_FALSE(f.service.sliderValue(slider).has_value());
    CHECK_FALSE(f.service.isVisible({}));
}

TEST_CASE("UiService focus and rectangles follow the last layout")
{
    Fixture f;
    CHECK_FALSE(f.service.rect(f.button).has_value());
    CHECK_FALSE(f.service.setFocus(f.button)); // not laid out yet
    f.step();
    REQUIRE(f.service.rect(f.button).has_value());
    CHECK(f.service.rect(f.button)->min == Vec2(100.0f, 200.0f));
    CHECK(f.service.rect(f.button)->max == Vec2(300.0f, 300.0f));
    CHECK(f.service.setFocus(f.button));
    CHECK(f.service.focused() == f.button);
    CHECK_FALSE(f.service.setFocus(f.label)); // not interactable
    CHECK(f.service.focused() == f.button);
    CHECK(f.service.setFocus({}));
    CHECK(f.service.focused().isNull());
    const Entity gone = f.world.create(UiElement{});
    f.world.destroy(gone);
    CHECK_FALSE(f.service.setFocus(gone));
    CHECK_FALSE(f.service.rect(gone).has_value());
}

TEST_CASE("UiService::invalidateLayout lays out again after Components were written without markChanged")
{
    Fixture f;
    f.step();
    f.world.get<UiElement>(f.button).placement.offsetMax = {400.0f, 300.0f};
    f.step();
    CHECK(f.ui.statistics().layoutReused);
    CHECK(f.service.rect(f.button)->max.x == 300.0f);
    f.service.invalidateLayout();
    f.step();
    CHECK_FALSE(f.ui.statistics().layoutReused);
    CHECK(f.service.rect(f.button)->max.x == 400.0f);
}

TEST_CASE("UiService::pollEvents copies the events of the last update and returns how many there were")
{
    Fixture f;
    f.step();
    std::array<UiEvent, 4> events{};
    CHECK(f.service.pollEvents(events) == 0);

    UiInput input;
    input.pointerPresent = true;
    input.pointerPixels = {200.0f, 250.0f};
    input.pointerDown = true;
    input.cancelPressed = true;
    f.step(input);
    REQUIRE(f.ui.events().size() == 2);
    CHECK(f.service.pollEvents({}) == 2);
    std::array<UiEvent, 1> one{};
    CHECK(f.service.pollEvents(one) == 2);
    CHECK(one[0].type == UiEventType::FocusChanged);
    CHECK(one[0].entity == f.button);
    CHECK(f.service.pollEvents(events) == 2);
    CHECK(events[1].type == UiEventType::Cancelled);
    CHECK(events[1].entity == f.canvas);

    input.pointerDown = false;
    input.cancelPressed = false;
    f.step(input);
    REQUIRE(f.service.pollEvents(events) == 1);
    CHECK(events[0].type == UiEventType::Clicked);
}

TEST_CASE("UiService::playTransition moves a transition either way and transitionProgress reports it")
{
    Fixture f;
    UiTransition hover;
    hover.trigger = UiTransitionTrigger::Hovered;
    hover.duration = 0.5f;
    hover.ease = UiEase::Linear;
    f.world.emplace<UiTransition>(f.button, hover);
    CHECK_FALSE(f.service.playTransition(f.label, true)); // no UiTransition
    CHECK_FALSE(f.service.transitionProgress(f.label).has_value());
    CHECK(f.service.transitionProgress(f.button) == 0.0f); // before the UI ran
    f.step();
    UiInput input;
    input.deltaSeconds = 0.25;
    CHECK(f.service.playTransition(f.button, true)); // a Hovered transition, not hovered
    f.step(input);
    REQUIRE(f.service.transitionProgress(f.button).has_value());
    CHECK(static_cast<f64>(*f.service.transitionProgress(f.button)) == doctest::Approx(0.5));
    f.step(input);
    CHECK(static_cast<f64>(*f.service.transitionProgress(f.button)) == doctest::Approx(1.0));
    f.step(input); // it stays until the trigger's state changes
    CHECK(static_cast<f64>(*f.service.transitionProgress(f.button)) == doctest::Approx(1.0));
    CHECK(f.service.playTransition(f.button, false));
    f.step(input);
    CHECK(static_cast<f64>(*f.service.transitionProgress(f.button)) == doctest::Approx(0.5));

    const Entity gone = f.world.create(UiElement{}, UiTransition{});
    f.world.destroy(gone);
    CHECK_FALSE(f.service.playTransition(gone, true));
    CHECK_FALSE(f.service.transitionProgress(gone).has_value());
    CHECK_FALSE(f.service.playTransition({}, true));
}
