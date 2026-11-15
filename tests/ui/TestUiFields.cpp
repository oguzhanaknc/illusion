#include "FontTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/Utf8.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

constexpr UVec2 kHd{1920, 1080};
constexpr f64 kFrame = 1.0 / 60.0;
constexpr UiTextureId kAtlas{3};
constexpr Vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};
constexpr u32 kRedPacked = 0xFF0000FFu;
constexpr Vec2 kUp{0.0f, 1.0f};
constexpr Vec2 kDown{0.0f, -1.0f};
constexpr Vec2 kLeft{-1.0f, 0.0f};
constexpr Vec2 kRight{1.0f, 0.0f};

// The test font with the default ranges: Turkish letters and the bullet, but no emoji.
const Font& fieldFont()
{
    static const Font font = [] {
        FontBakeSettings settings;
        settings.pixelSize = 32;
        return std::move(Font::bake(test::readTestFontFile(), settings)).value();
    }();
    return font;
}

class FakeAssets final : public UiAssets
{
public:
    UiFontFace font(const AssetRef& font) override
    {
        return font == fontRef ? UiFontFace{asciiOnly ? &test::testFont() : &fieldFont(), kAtlas}
                               : UiFontFace{};
    }

    UiImageSource image(const AssetRef&) override { return {}; }

    AssetRef fontRef{"assets:/fonts/test.ttf"};
    bool asciiOnly = false;
};

class FakeGlyphs final : public UiGlyphSource
{
public:
    [[nodiscard]] std::string_view actionLabel(std::string_view action) override
    {
        ++calls;
        return action == "jump" ? std::string_view(label) : std::string_view();
    }

    [[nodiscard]] u64 version() const noexcept override { return current; }

    std::string label = "Space";
    u64 current = 1;
    u32 calls = 0;
};

std::string utf8(std::u32string_view codePoints)
{
    std::string out;
    for (const char32_t c : codePoints)
    {
        text::appendUtf8(out, c);
    }
    return out;
}

UiCanvas fullCanvas()
{
    UiCanvas canvas;
    canvas.useSafeArea = false;
    return canvas;
}

UiElement placed(Vec2 min, Vec2 max)
{
    UiElement element;
    element.placement.offsetMin = min;
    element.placement.offsetMax = max;
    return element;
}

UiElement sized(Vec2 size)
{
    UiElement element;
    element.size = size;
    return element;
}

UiImage solid()
{
    return UiImage{};
}

UiStack column(f32 spacing = 0.0f)
{
    UiStack stack;
    stack.axis = UiAxis::Vertical;
    stack.spacing = spacing;
    return stack;
}

UiInput idle(f64 deltaSeconds = kFrame)
{
    UiInput input;
    input.deltaSeconds = deltaSeconds;
    return input;
}

UiInput typing(std::u32string_view characters)
{
    UiInput input = idle();
    input.text = characters;
    return input;
}

UiInput pointerAt(Vec2 pixels, bool down = false)
{
    UiInput input = idle();
    input.pointerPixels = pixels;
    input.pointerPresent = true;
    input.pointerDown = down;
    return input;
}

UiInput wheelAt(Vec2 pixels, f32 notches)
{
    UiInput input = pointerAt(pixels);
    input.wheel = notches;
    return input;
}

UiInput navigating(Vec2 direction)
{
    UiInput input = idle();
    input.navigate = direction;
    return input;
}

usize quadCount(const UiDrawData& data)
{
    return data.vertices.size() / 4;
}

// The rectangle of the caret (the quads in kRed), if one was drawn.
std::optional<UiRect> caretOf(const UiDrawData& data)
{
    for (usize v = 0; v + 3 < data.vertices.size(); v += 4)
    {
        if (data.vertices[v].color == kRedPacked)
        {
            return UiRect{data.vertices[v].position, data.vertices[v + 2].position};
        }
    }
    return std::nullopt;
}

f32 widthOf(std::string_view utf8Text, const Font& font = fieldFont())
{
    UiTextStyle style;
    return measureText(font, utf8Text, style).size.x;
}

// A World with a full-screen canvas, its UiWorld and fake assets.
struct Form
{
    Form()
        : ui(world)
    {
        registerUiComponents();
        ui.setAssets(&assets);
        canvas = world.create(fullCanvas());
    }

    template <class... Ts>
    Entity child(Entity parent, Ts&&... components)
    {
        const Entity entity = world.create(std::forward<Ts>(components)...);
        REQUIRE(setParent(world, entity, parent).hasValue());
        return entity;
    }

    [[nodiscard]] UiText label(std::string_view literal = {}) const
    {
        UiText text;
        text.text = std::string(literal);
        text.font = assets.fontRef;
        return text;
    }

    Entity field(Vec2 min, Vec2 max, UiTextField settings = {})
    {
        settings.caretColor = kRed;
        return child(canvas, placed(min, max), label(), UiInteractable{}, std::move(settings));
    }

    Entity button(Entity parent, UiElement element)
    {
        return child(parent, element, solid(), UiInteractable{}, UiButton{});
    }

    void step(const UiInput& input = idle())
    {
        updateTransformHierarchy(world, index);
        ui.update({kHd}, input);
    }

    void focusOn(Entity entity)
    {
        step();
        ui.setFocus(entity);
        step();
        REQUIRE(ui.focused() == entity);
    }

    void click(Vec2 pixels)
    {
        step(pointerAt(pixels, true));
        step(pointerAt(pixels, false));
    }

    void navigate(Vec2 direction)
    {
        step(navigating(direction));
        step();
    }

    void type(std::u32string_view characters) { step(typing(characters)); }

    void press(bool UiInput::* key)
    {
        UiInput input = idle();
        input.*key = true;
        step(input);
    }

    [[nodiscard]] const std::string& textOf(Entity entity) const
    {
        return world.get<UiTextField>(entity).text;
    }

    [[nodiscard]] u32 count(UiEventType type) const
    {
        u32 found = 0;
        for (const UiEvent& event : ui.events())
        {
            found += event.type == type ? 1u : 0u;
        }
        return found;
    }

    [[nodiscard]] UiRect rectOf(Entity entity) const { return world.get<UiComputedRect>(entity).rect; }

    FakeAssets assets;
    World world;
    HierarchyIndex index;
    UiWorld ui;
    Entity canvas{};
};

bool inside(const UiRect& inner, const UiRect& outer, f32 tolerance = 1e-3f)
{
    return inner.min.x >= outer.min.x - tolerance && inner.min.y >= outer.min.y - tolerance &&
           inner.max.x <= outer.max.x + tolerance && inner.max.y <= outer.max.y + tolerance;
}

} // namespace

TEST_CASE("a focused text field takes typed ASCII and multi-byte text at the caret")
{
    Form form;
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f});
    form.type(U"ab");
    CHECK(form.textOf(field).empty()); // not focused yet

    form.focusOn(field);
    form.type(U"ab");
    CHECK(form.textOf(field) == "ab");
    CHECK(form.count(UiEventType::TextChanged) == 1);
    form.type(U"\u015F\u011F\u0131\u00DC");
    CHECK(form.textOf(field) == utf8(U"ab\u015F\u011F\u0131\u00DC"));
    form.step();
    CHECK(form.ui.statistics().missingGlyphs == 0);

    form.type(U"\U0001F600");
    CHECK(form.textOf(field) == utf8(U"ab\u015F\u011F\u0131\u00DC\U0001F600"));
    CHECK(text::isValidUtf8(form.textOf(field)));
    CHECK(form.ui.statistics().missingGlyphs == 1); // drawn with the fallback glyph

    form.type(U"\t\n\r\x7F\x85");
    CHECK(form.count(UiEventType::TextChanged) == 0);
    CHECK(text::utf8Length(form.textOf(field)) == 7);
}

TEST_CASE("a text field takes at most maxLength code points, 1 when it is 0")
{
    Form form;
    UiTextField settings;
    settings.maxLength = 3;
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    form.focusOn(field);
    form.type(U"\u015Fa\u011Fb");
    CHECK(form.textOf(field) == utf8(U"\u015Fa\u011F"));
    form.type(U"c");
    CHECK(form.count(UiEventType::TextChanged) == 0);

    form.world.get<UiTextField>(field).maxLength = 0;
    form.world.get<UiTextField>(field).text.clear();
    form.world.markChanged<UiTextField>();
    form.type(U"xy");
    CHECK(form.textOf(field) == "x");
}

TEST_CASE("the caret moves and deletes whole code points, never splitting UTF-8")
{
    Form form;
    UiTextField settings;
    settings.text = utf8(U"a\u015F\U0001F600b");
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    form.focusOn(field); // the caret starts at the end

    form.press(&UiInput::backspace);
    CHECK(form.textOf(field) == utf8(U"a\u015F\U0001F600"));
    form.press(&UiInput::caretLeft);
    form.press(&UiInput::deleteForward);
    CHECK(form.textOf(field) == utf8(U"a\u015F"));
    form.press(&UiInput::deleteForward); // at the end: nothing
    CHECK(form.count(UiEventType::TextChanged) == 0);
    form.press(&UiInput::caretLeft);
    form.type(U"\u00E7");
    CHECK(form.textOf(field) == utf8(U"a\u00E7\u015F"));
    form.press(&UiInput::caretHome);
    form.press(&UiInput::backspace); // at the start: nothing
    form.press(&UiInput::deleteForward);
    CHECK(form.textOf(field) == utf8(U"\u00E7\u015F"));
    form.press(&UiInput::caretRight);
    form.press(&UiInput::backspace);
    CHECK(form.textOf(field) == utf8(U"\u015F"));
    form.press(&UiInput::caretEnd);
    form.press(&UiInput::backspace);
    CHECK(form.textOf(field).empty());
    CHECK(form.count(UiEventType::TextChanged) == 1);
}

TEST_CASE("navigation left and right moves the caret of a focused field, up and down move the focus")
{
    Form form;
    UiTextField settings;
    settings.text = "ab";
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    const Entity below = form.button(form.canvas, placed({100.0f, 200.0f}, {900.0f, 250.0f}));
    const Entity right = form.button(form.canvas, placed({1000.0f, 100.0f}, {1200.0f, 150.0f}));
    form.focusOn(field);
    form.navigate(kLeft);
    CHECK(form.ui.focused() == field);
    form.type(U"X");
    CHECK(form.textOf(field) == "aXb");
    form.navigate(kRight);
    form.navigate(kRight); // at the end: the focus stays
    form.type(U"Y");
    CHECK(form.textOf(field) == "aXbY");
    CHECK(form.ui.focused() == field);
    form.navigate(kDown);
    CHECK(form.ui.focused() == below);
    form.navigate(kUp);
    CHECK(form.ui.focused() == field);
    CHECK(form.ui.focused() != right);
}

TEST_CASE("a pointer press focuses a field and puts the caret at the nearest glyph boundary")
{
    Form form;
    UiTextField settings;
    settings.text = "abcdef";
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    form.step();
    const f32 boundary = 100.0f + widthOf("abc");
    form.click({boundary + 1.0f, 120.0f});
    CHECK(form.ui.focused() == field);
    form.type(U"X");
    CHECK(form.textOf(field) == "abcXdef");

    form.click({boundary - 1.0f, 120.0f});
    form.type(U"Y");
    CHECK(form.textOf(field) == "abcYXdef");
    form.click({101.0f, 120.0f});
    form.type(U"<");
    CHECK(form.textOf(field) == "<abcYXdef");
    form.click({890.0f, 120.0f});
    form.type(U">");
    CHECK(form.textOf(field) == "<abcYXdef>");
}

TEST_CASE("a password field shows a bullet per code point, or an asterisk when the font lacks it")
{
    Form form;
    UiTextField settings;
    settings.text = utf8(U"a\u015F\U0001F600");
    settings.password = true;
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    form.focusOn(field);
    REQUIRE(fieldFont().find(0x2022) != nullptr);
    std::optional<UiRect> caret = caretOf(form.ui.drawData());
    REQUIRE(caret.has_value());
    CHECK(caret->min.x == doctest::Approx(static_cast<f64>(100.0f + widthOf(utf8(U"\u2022\u2022\u2022")))));
    CHECK(quadCount(form.ui.drawData()) == 4); // three bullets and the caret
    CHECK(form.ui.statistics().missingGlyphs == 0);

    form.assets.asciiOnly = true;
    form.ui.invalidateLayout();
    form.step();
    caret = caretOf(form.ui.drawData());
    REQUIRE(caret.has_value());
    CHECK(caret->min.x == doctest::Approx(static_cast<f64>(100.0f + widthOf("***", test::testFont()))));
}

TEST_CASE("an empty field shows its placeholder at half alpha")
{
    Form form;
    UiTextField settings;
    settings.placeholder = "Name";
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    form.step();
    UiDrawData data = form.ui.drawData();
    REQUIRE(quadCount(data) == 4);
    CHECK((data.vertices[0].color >> 24u) == 128u);
    form.focusOn(field);
    form.type(U"Al");
    data = form.ui.drawData();
    REQUIRE(quadCount(data) == 3); // two letters and the caret
    CHECK((data.vertices[0].color >> 24u) == 255u);
}

TEST_CASE("submit on a focused field reports Submitted and keeps the focus")
{
    Form form;
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f});
    form.focusOn(field);
    UiInput submit = idle();
    submit.submitDown = true;
    form.step(submit);
    REQUIRE(form.ui.events().size() == 1);
    CHECK(form.ui.events()[0].type == UiEventType::Submitted);
    CHECK(form.ui.events()[0].entity == field);
    form.step();
    CHECK(form.ui.events().empty());
    CHECK(form.ui.focused() == field);
}

TEST_CASE("a text wider than its field scrolls so that the caret stays visible, clipped to the element")
{
    Form form;
    UiTextField settings;
    settings.text = "abcdefghijklmnopqrstuvwxyz0123456789";
    const Entity field = form.field({100.0f, 100.0f}, {200.0f, 150.0f}, settings);
    REQUIRE(widthOf(settings.text) > 200.0f);
    form.focusOn(field);
    const UiRect element = form.rectOf(field);
    std::optional<UiRect> caret = caretOf(form.ui.drawData());
    REQUIRE(caret.has_value());
    CHECK(inside(*caret, element));
    CHECK(caret->max.x == doctest::Approx(200.0));
    bool clipped = false;
    for (const UiDrawCommand& command : form.ui.drawData().commands)
    {
        clipped = clipped || (command.texture == kAtlas && command.clip == element);
    }
    CHECK(clipped);

    form.press(&UiInput::caretHome);
    caret = caretOf(form.ui.drawData());
    REQUIRE(caret.has_value());
    CHECK(caret->min.x == doctest::Approx(100.0));
    for (int i = 0; i < 10; ++i)
    {
        form.press(&UiInput::caretRight);
        caret = caretOf(form.ui.drawData());
        REQUIRE(caret.has_value());
        CHECK(inside(*caret, element));
    }
}

TEST_CASE("the caret blinks with a half-period of 0.5 s that restarts on every edit and caret move")
{
    Form form;
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f});
    form.focusOn(field);
    CHECK(caretOf(form.ui.drawData()).has_value());
    form.step(idle(0.25));
    CHECK(caretOf(form.ui.drawData()).has_value());
    form.step(idle(0.3));
    CHECK_FALSE(caretOf(form.ui.drawData()).has_value());
    form.step(idle(0.5));
    CHECK(caretOf(form.ui.drawData()).has_value());
    form.step(idle(0.47));
    CHECK_FALSE(caretOf(form.ui.drawData()).has_value());
    form.type(U"a");
    CHECK(caretOf(form.ui.drawData()).has_value());
    form.step(idle(0.49));
    CHECK(caretOf(form.ui.drawData()).has_value());
    form.step(idle(0.02));
    CHECK_FALSE(caretOf(form.ui.drawData()).has_value());
    form.press(&UiInput::caretLeft);
    CHECK(caretOf(form.ui.drawData()).has_value());
}

TEST_CASE("a text set from outside puts the caret at its end")
{
    Form form;
    UiTextField settings;
    settings.text = "abc";
    const Entity field = form.field({100.0f, 100.0f}, {900.0f, 150.0f}, settings);
    form.focusOn(field);
    form.press(&UiInput::caretHome);
    form.world.get<UiTextField>(field).text = "xyz12";
    form.world.markChanged<UiTextField>();
    form.type(U"!");
    CHECK(form.textOf(field) == "xyz12!");
}

TEST_CASE("text fields, scroll views and glyphs survive a scene save and load")
{
    registerUiComponents();
    reflectCoreComponents();
    World world;
    UiTextField field;
    field.text = utf8(U"\u015Fifre");
    field.placeholder = "Password";
    field.maxLength = 12;
    field.password = true;
    field.caretColor = {0.1f, 0.2f, 0.3f, 0.4f};
    (void)world.create(UiElement{}, UiText{}, UiInteractable{}, field);
    UiScrollView view;
    view.offset = {12.0f, 34.0f};
    view.horizontal = true;
    view.vertical = false;
    view.wheelStep = 25.0f;
    (void)world.create(UiElement{}, view);
    UiGlyph glyph;
    glyph.action = "ui.submit";
    (void)world.create(UiElement{}, UiText{}, glyph);
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());

    World loaded;
    REQUIRE(loadScene(loaded, saved.json).ok());
    u32 found = 0;
    loaded.query<const UiTextField>().each([&](Entity, const UiTextField& back) {
        ++found;
        CHECK(back.text == field.text);
        CHECK(back.placeholder == field.placeholder);
        CHECK(back.maxLength == field.maxLength);
        CHECK(back.password);
        CHECK(back.caretColor == field.caretColor);
    });
    loaded.query<const UiScrollView>().each([&](Entity, const UiScrollView& back) {
        ++found;
        CHECK(back.offset == view.offset);
        CHECK(back.horizontal);
        CHECK_FALSE(back.vertical);
        CHECK(back.wheelStep == view.wheelStep);
    });
    loaded.query<const UiGlyph>().each([&](Entity, const UiGlyph& back) {
        ++found;
        CHECK(back.action == glyph.action);
    });
    CHECK(found == 3);
}

TEST_CASE("a scroll view keeps its offset within its content on the axes it may scroll")
{
    Form form;
    UiScrollView settings;
    settings.offset = {50.0f, 5000.0f};
    const Entity view = form.child(form.canvas, placed({100.0f, 100.0f}, {400.0f, 300.0f}), settings);
    const Entity content = form.child(view, sized({600.0f, 1000.0f}));
    form.step();
    CHECK(form.world.get<UiScrollView>(view).offset == Vec2(0.0f, 800.0f));
    CHECK(form.rectOf(content).min == Vec2(100.0f, -700.0f));
    CHECK(form.rectOf(content).max == Vec2(700.0f, 300.0f));

    form.world.get<UiScrollView>(view).horizontal = true;
    form.world.get<UiScrollView>(view).offset = {-5.0f, std::nanf("")};
    form.world.markChanged<UiScrollView>();
    form.step();
    CHECK(form.world.get<UiScrollView>(view).offset == Vec2(0.0f, 0.0f));

    // A stack content is as long as its children along its axis and as wide as the view.
    const Entity list = form.child(form.canvas, placed({500.0f, 100.0f}, {800.0f, 300.0f}), UiScrollView{});
    const Entity rows = form.child(list, UiElement{}, column(10.0f));
    for (int i = 0; i < 30; ++i)
    {
        (void)form.child(rows, sized({0.0f, 40.0f}), solid());
    }
    form.world.get<UiScrollView>(list).offset.y = 1e6f;
    form.world.markChanged<UiScrollView>();
    form.step();
    CHECK(form.rectOf(rows).size() == Vec2(300.0f, 30.0f * 40.0f + 29.0f * 10.0f));
    CHECK(form.world.get<UiScrollView>(list).offset.y == doctest::Approx(1490.0 - 200.0));
}

TEST_CASE("the wheel scrolls the topmost scroll view under the pointer")
{
    Form form;
    const Entity outer = form.child(form.canvas, placed({100.0f, 100.0f}, {500.0f, 300.0f}), UiScrollView{});
    const Entity rows = form.child(outer, UiElement{}, column());
    UiScrollView sideways;
    sideways.horizontal = true;
    sideways.vertical = false;
    const Entity inner = form.child(rows, sized({0.0f, 100.0f}), sideways);
    (void)form.child(inner, sized({1200.0f, 0.0f}));
    for (int i = 0; i < 10; ++i)
    {
        (void)form.child(rows, sized({0.0f, 50.0f}), solid());
    }
    form.step();
    form.step(wheelAt({300.0f, 150.0f}, -1.0f)); // over the inner view
    CHECK(form.world.get<UiScrollView>(inner).offset == Vec2(60.0f, 0.0f));
    CHECK(form.world.get<UiScrollView>(outer).offset == Vec2(0.0f));
    form.step(wheelAt({300.0f, 250.0f}, -2.0f)); // below it
    CHECK(form.world.get<UiScrollView>(outer).offset == Vec2(0.0f, 120.0f));
    CHECK(form.rectOf(inner).min.y == doctest::Approx(-20.0));
    form.step(wheelAt({300.0f, 250.0f}, 5.0f));
    CHECK(form.world.get<UiScrollView>(outer).offset == Vec2(0.0f));
    form.step(wheelAt({700.0f, 250.0f}, -1.0f)); // outside both
    CHECK(form.world.get<UiScrollView>(outer).offset == Vec2(0.0f));
}

TEST_CASE(
    "dragging scrolls the content; a press on a button scrolls only beyond 8 pixels and then does not click")
{
    Form form;
    const Entity view = form.child(form.canvas, placed({100.0f, 100.0f}, {500.0f, 300.0f}), UiScrollView{});
    const Entity rows = form.child(view, UiElement{}, column(10.0f));
    std::vector<Entity> buttons;
    for (int i = 0; i < 10; ++i)
    {
        buttons.push_back(form.button(rows, sized({0.0f, 50.0f})));
    }
    form.step();
    const auto clicked = [&] {
        for (const UiEvent& event : form.ui.events())
        {
            if (event.type == UiEventType::Clicked)
            {
                return true;
            }
        }
        return false;
    };

    form.step(pointerAt({300.0f, 125.0f}, true));
    form.step(pointerAt({300.0f, 120.0f}, true));
    form.step(pointerAt({300.0f, 120.0f}, false));
    CHECK(clicked());
    CHECK(form.world.get<UiScrollView>(view).offset == Vec2(0.0f));

    form.step(pointerAt({300.0f, 125.0f}, true));
    form.step(pointerAt({300.0f, 105.0f}, true));
    CHECK(form.world.get<UiScrollView>(view).offset == Vec2(0.0f, 20.0f));
    CHECK_FALSE(form.world.get<UiInteraction>(buttons[0]).pressed);
    form.step(pointerAt({300.0f, 105.0f}, false));
    CHECK_FALSE(clicked());

    // Between two buttons nothing is interactable: the content follows at once.
    const f32 gap = form.rectOf(buttons[0]).max.y + 5.0f;
    form.step(pointerAt({300.0f, gap}, true));
    form.step(pointerAt({300.0f, gap - 3.0f}, true));
    CHECK(form.world.get<UiScrollView>(view).offset.y == doctest::Approx(23.0));
    form.step(pointerAt({300.0f, gap - 3.0f}, false));
    CHECK_FALSE(clicked());
}

TEST_CASE("moving the focus by navigation scrolls the focused element into view")
{
    Form form;
    const Entity view = form.child(form.canvas, placed({100.0f, 100.0f}, {500.0f, 300.0f}), UiScrollView{});
    const Entity rows = form.child(view, UiElement{}, column(10.0f));
    std::vector<Entity> buttons;
    for (int i = 0; i < 20; ++i)
    {
        buttons.push_back(form.button(rows, sized({0.0f, 50.0f})));
    }
    const UiRect shown{{100.0f, 100.0f}, {500.0f, 300.0f}};
    form.navigate(kDown);
    REQUIRE(form.ui.focused() == buttons[0]);
    for (usize i = 1; i < buttons.size(); ++i)
    {
        form.navigate(kDown);
        REQUIRE(form.ui.focused() == buttons[i]);
        CHECK(inside(form.rectOf(buttons[i]), shown));
    }
    CHECK(form.world.get<UiScrollView>(view).offset.y == doctest::Approx(1190.0 - 200.0));
    for (usize i = buttons.size() - 1; i-- > 0;)
    {
        form.navigate(kUp);
        REQUIRE(form.ui.focused() == buttons[i]);
        CHECK(inside(form.rectOf(buttons[i]), shown));
    }
    CHECK(form.world.get<UiScrollView>(view).offset.y == doctest::Approx(0.0));
}

TEST_CASE("elements outside a scroll view are neither hit nor drawn, also in nested views")
{
    Form form;
    const Entity view = form.child(form.canvas, placed({100.0f, 100.0f}, {500.0f, 300.0f}), UiScrollView{});
    const Entity rows = form.child(view, UiElement{}, column());
    std::vector<Entity> buttons;
    for (int i = 0; i < 100; ++i)
    {
        buttons.push_back(form.button(rows, sized({0.0f, 40.0f})));
    }
    form.step();
    CHECK(quadCount(form.ui.drawData()) == 5);
    form.step(wheelAt({300.0f, 200.0f}, -1.0f));
    CHECK(quadCount(form.ui.drawData()) == 6);
    CHECK(form.world.get<UiComputedRect>(buttons[99]).rect.min.y > 300.0f); // laid out all the same

    // Row 10 lies below the view, under the pointer: it is not hit.
    REQUIRE(form.rectOf(buttons[10]).min.y > 300.0f);
    const Vec2 below(300.0f, (form.rectOf(buttons[10]).min.y + form.rectOf(buttons[10]).max.y) * 0.5f);
    form.click(below);
    CHECK(form.count(UiEventType::Clicked) == 0);
    CHECK_FALSE(form.world.get<UiInteraction>(buttons[10]).hovered);

    // A horizontal view inside a vertical one: its cells are clipped by both.
    Form nested;
    const Entity outer =
        nested.child(nested.canvas, placed({100.0f, 100.0f}, {500.0f, 300.0f}), UiScrollView{});
    const Entity list = nested.child(outer, UiElement{}, column());
    UiScrollView sideways;
    sideways.horizontal = true;
    sideways.vertical = false;
    const Entity inner = nested.child(list, sized({0.0f, 100.0f}), sideways);
    UiStack cellsAlong;
    cellsAlong.axis = UiAxis::Horizontal;
    const Entity cells = nested.child(inner, UiElement{}, cellsAlong);
    for (int i = 0; i < 30; ++i)
    {
        (void)nested.child(cells, sized({40.0f, 0.0f}), solid());
    }
    for (int i = 0; i < 10; ++i)
    {
        (void)nested.child(list, sized({0.0f, 50.0f}), solid());
    }
    nested.step();
    CHECK(nested.rectOf(cells).size() == Vec2(1200.0f, 100.0f));
    CHECK(quadCount(nested.ui.drawData()) == 10 + 2); // ten cells and the two rows below
    nested.step(wheelAt({300.0f, 150.0f}, -1.0f));
    CHECK(quadCount(nested.ui.drawData()) == 11 + 2);
    nested.world.get<UiScrollView>(outer).offset.y = 100.0f;
    nested.world.markChanged<UiScrollView>();
    nested.step();
    CHECK(quadCount(nested.ui.drawData()) == 4); // the inner view is above the outer one
}

TEST_CASE("a glyph shows the label of its action and asks again only when the source's version changes")
{
    Form form;
    FakeGlyphs glyphs;
    UiGlyph jump;
    jump.action = "jump";
    const Entity known =
        form.child(form.canvas, placed({100.0f, 100.0f}, {600.0f, 150.0f}), form.label("fallback"), jump);
    UiGlyph other;
    other.action = "unknown";
    (void)form.child(form.canvas, placed({100.0f, 200.0f}, {600.0f, 250.0f}), form.label("abc"), other);
    form.step();
    CHECK(quadCount(form.ui.drawData()) == 8 + 3); // no source: their own texts

    form.ui.setGlyphs(&glyphs);
    form.step();
    CHECK(quadCount(form.ui.drawData()) == 5 + 3);
    CHECK(form.world.get<UiText>(known).text == "fallback");
    const u32 calls = glyphs.calls;
    form.step();
    form.step();
    CHECK(glyphs.calls == calls);

    glyphs.label = "A";
    form.step();
    CHECK(quadCount(form.ui.drawData()) == 5 + 3);
    glyphs.current = 2;
    form.step();
    CHECK(quadCount(form.ui.drawData()) == 1 + 3);
    CHECK(glyphs.calls > calls);

    form.ui.setGlyphs(nullptr);
    form.step();
    CHECK(quadCount(form.ui.drawData()) == 8 + 3);
}

TEST_CASE("steady updates with a focused idle field, a scroll view and a glyph allocate nothing")
{
    Form form;
    FakeGlyphs glyphs;
    form.ui.setGlyphs(&glyphs);
    UiTextField settings;
    settings.text = "hello";
    const Entity field = form.field({100.0f, 100.0f}, {300.0f, 150.0f}, settings);
    UiTextField secret;
    secret.text = "abc";
    secret.password = true;
    (void)form.field({100.0f, 400.0f}, {300.0f, 450.0f}, secret);
    const Entity view = form.child(form.canvas, placed({500.0f, 100.0f}, {900.0f, 300.0f}), UiScrollView{});
    const Entity rows = form.child(view, UiElement{}, column());
    for (int i = 0; i < 50; ++i)
    {
        (void)form.child(rows, sized({0.0f, 40.0f}), form.label("row"));
    }
    UiGlyph jump;
    jump.action = "jump";
    (void)form.child(form.canvas, placed({100.0f, 600.0f}, {600.0f, 650.0f}), form.label("x"), jump);
    form.focusOn(field);
    form.type(U"!");
    form.step(wheelAt({700.0f, 200.0f}, -1.0f));
    const UiInput resting = pointerAt({700.0f, 200.0f});
    form.step(resting);
    form.step(resting);
    REQUIRE(form.ui.statistics().layoutReused);
    updateTransformHierarchy(form.world, form.index);
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 40; ++i) // the caret blinks a few times
        {
            form.ui.update({kHd}, resting);
        }
        CHECK(counter.count() == 0);
    }
    CHECK(form.ui.statistics().layoutReused);
    CHECK(form.ui.focused() == field);
}

TEST_CASE("scrolling moves the content where a new layout places it, rotated and scaled descendants too")
{
    Form form;
    UiElement turned = placed({100.0f, 100.0f}, {500.0f, 300.0f});
    turned.rotation = 0.3f;
    const Entity view = form.child(form.canvas, turned, UiScrollView{});
    const Entity rows = form.child(view, UiElement{}, column(5.0f));
    std::vector<Entity> parts;
    for (int i = 0; i < 12; ++i)
    {
        UiElement row = sized({0.0f, 40.0f});
        row.rotation = 0.1f * static_cast<f32>(i);
        row.scale = {1.0f + 0.05f * static_cast<f32>(i), 0.9f};
        const Entity entity = form.child(rows, row, solid());
        parts.push_back(entity);
        parts.push_back(form.child(entity, placed({2.0f, 2.0f}, {30.0f, 20.0f}), solid()));
    }
    form.step();
    std::vector<UiComputedRect> scrolled;
    const UiComputedRect& frame = form.world.get<UiComputedRect>(view);
    const Vec2 centre = frame.transform.apply((frame.rect.min + frame.rect.max) * 0.5f);
    form.step(wheelAt(centre, -2.0f));
    REQUIRE(form.world.get<UiScrollView>(view).offset.y == doctest::Approx(120.0));
    for (const Entity part : parts)
    {
        scrolled.push_back(form.world.get<UiComputedRect>(part));
    }
    form.ui.invalidateLayout();
    form.step();
    for (usize i = 0; i < parts.size(); ++i)
    {
        CAPTURE(i);
        const UiComputedRect& laidOut = form.world.get<UiComputedRect>(parts[i]);
        const Vec2 corners[] = {laidOut.rect.min, laidOut.rect.max};
        for (const Vec2 corner : corners)
        {
            const Vec2 a = laidOut.transform.apply(corner);
            const Vec2 b = scrolled[i].transform.apply(corner);
            CHECK(a.x == doctest::Approx(static_cast<f64>(b.x)).epsilon(1e-4));
            CHECK(a.y == doctest::Approx(static_cast<f64>(b.y)).epsilon(1e-4));
        }
        CHECK(laidOut.rect.min.y == doctest::Approx(static_cast<f64>(scrolled[i].rect.min.y)));
    }
}
