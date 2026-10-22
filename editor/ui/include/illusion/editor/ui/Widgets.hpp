#pragma once

/// @file
/// The editor's widget kit: the controls panels are built from, drawn with the theme's tokens. Every
/// interactive widget takes a stable id (e.g. "inspector.transform.position"); sub-items get the id plus a
/// suffix (".x", ".clear", ".menu"), so tests and agents can address each of them (see UiDriver).
///
/// ```cpp
/// UiKit kit(theme, &fonts, text);
/// if (beginComponentCard(kit, "inspector.transform", icons::kOpenWith, text.format(strings::kTransform),
/// nullptr))
/// {
///     if (beginPropertyTable(kit, "inspector.transform.props"))
///     {
///         propertyLabel(kit, text.format(strings::kPosition));
///         vec3Field(kit, "inspector.transform.position", position);
///         endPropertyTable();
///     }
///     endComponentCard();
/// }
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/ui/Icon.hpp>
#include <illusion/editor/ui/Theme.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace illusion::editor::ui
{

class FontManager;
class FrameText;

/// Text the widgets themselves show (tooltips of their buttons), already localized by the caller.
struct WidgetStrings
{
    const char* clearSearch = ""; ///< Tooltip of a search field's clear button.
    const char* moreOptions = ""; ///< Tooltip of a component card's menu button.
    const char* enabled = "";     ///< Tooltip of a component card's enable toggle.
    const char* intensity = "";   ///< Label of the intensity of an HDR colour.
};

/// What widgets draw with: the theme, the fonts (optional; null to use the current font everywhere), the
/// frame's text and options. Cheap to create; create one per window and frame or keep one.
struct UiKit
{
    /// Draws with @p kitTheme, @p kitFonts and @p kitText, which must outlive the kit.
    UiKit(const Theme& kitTheme, const FontManager* kitFonts, FrameText& kitText) noexcept
        : theme(kitTheme)
        , fonts(kitFonts)
        , text(kitText)
    {
    }

    const Theme& theme;          ///< Colours.
    const FontManager* fonts;    ///< Fonts, or nullptr to use the current font everywhere.
    FrameText& text;             ///< Per-frame text.
    WidgetStrings strings;       ///< Text of the widgets' own buttons.
    bool colorBlindAxes = false; ///< Use the colour-blind axis colours (axis.cb.*).
    bool reduceMotion = false;   ///< Disable animations.
    f32 scale = 1.0f;            ///< UI scale times content scale, for sizes the style does not hold.
};

/// The theme colour of @p token as a packed Dear ImGui colour.
[[nodiscard]] u32 tokenColor(const UiKit& kit, ThemeToken token, f32 alphaFactor = 1.0f) noexcept;

/// A button that shows only @p icon, with @p tooltip (and @p shortcut, e.g. "Ctrl+S", in a dimmer colour)
/// on hover. @p selected draws it as pressed (tool buttons). Returns true when clicked.
bool iconButton(UiKit& kit, const Icon& icon, std::string_view id, const char* tooltip = nullptr,
                const char* shortcut = nullptr, bool selected = false);

/// A button filled with the accent colour, for the main action of a dialog. Returns true when clicked.
bool primaryButton(UiKit& kit, const char* label);

/// An on/off switch followed by @p label (which carries its "###id"). Returns true when toggled.
bool toggleSwitch(UiKit& kit, const char* label, bool& value);

/// A row of mutually exclusive buttons. @p labels are visible texts; item i gets the id "<id>.<i>".
/// Returns true when the selection changed.
bool segmentedControl(UiKit& kit, std::string_view id, std::span<const char* const> labels, int& selected);

/// A text field with a search icon, @p hint while empty, and a clear button ("<id>.clear") while not.
/// @p buffer holds zero-terminated UTF-8. Returns true when the text changed.
bool searchField(UiKit& kit, std::string_view id, std::span<char> buffer, const char* hint);

/// Three drag fields with coloured X, Y and Z chips ("<id>.x", ...). Dragging moves by @p speed per pixel,
/// ten times faster with Shift and ten times slower with Alt. Returns true when a value changed.
bool vec3Field(UiKit& kit, std::string_view id, Vec3& value, f32 speed = 0.01f, const char* format = "%.3f");

/// An HDR colour: a colour button and picker for the colour plus a separate intensity field
/// ("<id>.intensity"). Returns true when either changed.
bool colorHdr(UiKit& kit, std::string_view id, Vec3& color, f32& intensity);

/// Starts a component card: a header with @p icon, @p name, an enable toggle (when @p enabled is not
/// null, "<id>.enabled") and a menu button ("<id>.menu") that opens the card's menu (see
/// beginComponentCardMenu()). Returns true while the card is open; then call endComponentCard().
bool beginComponentCard(UiKit& kit, std::string_view id, const Icon& icon, const char* name, bool* enabled);

/// Ends a card started with beginComponentCard() that returned true.
void endComponentCard();

/// Starts the menu of the card @p id if its menu button opened it, whether the card is open or not; call
/// endComponentCardMenu() when it returns true. Call it with the same ID stack as beginComponentCard().
bool beginComponentCardMenu(UiKit& kit, std::string_view id);

/// Ends the menu started with beginComponentCardMenu().
void endComponentCardMenu();

/// Starts a two-column property table (label, value) with a resizable split. Returns true when visible;
/// then call endPropertyTable().
bool beginPropertyTable(UiKit& kit, std::string_view id);

/// Starts a row: writes @p label in the first column (with @p tooltip on hover) and prepares the second
/// column for a full-width value widget.
void propertyLabel(UiKit& kit, const char* label, const char* tooltip = nullptr);

/// Ends a property table.
void endPropertyTable();

/// A path of clickable parts separated by chevrons ("<id>.<i>"). Returns the index of the clicked part.
std::optional<usize> breadcrumbs(UiKit& kit, std::string_view id, std::span<const char* const> parts);

/// An asset tile of @p size pixels (at 100% scale): a large icon, a type badge and the name. Returns true
/// when clicked.
bool assetTile(UiKit& kit, std::string_view id, const Icon& icon, const char* name, const char* badge,
               bool selected, f32 size = 88.0f);

/// A small rounded label in the colour of @p token.
void badge(UiKit& kit, const char* text, ThemeToken token);

/// A section title in the secondary text colour with a separator line.
void sectionHeader(UiKit& kit, const char* label);

/// A centred hint for empty panels: a large icon, a title and a line of suggestions.
void emptyState(UiKit& kit, const Icon& icon, const char* title, const char* hint);

/// Shows a rich tooltip for the last item while it is hovered: a title, a description and a dimmer detail
/// line (unit, default value). Any text may be null.
void richTooltip(UiKit& kit, const char* title, const char* description, const char* detail);

/// Notifications in the bottom-right corner of the window, each with an optional action button. Fixed
/// capacity: pushing never allocates, and the oldest toast makes room for a new one.
class ToastQueue
{
public:
    /// Maximum number of visible toasts.
    static constexpr usize kCapacity = 4;
    /// Maximum bytes of a message or action label (longer text is cut at a UTF-8 boundary).
    static constexpr usize kTextCapacity = 128;

    /// Shows @p message (with an icon in the colour of @p token) for @p duration seconds from @p now. A
    /// non-empty @p action adds a button whose click draw() reports with @p actionId.
    void push(const Icon& icon, ThemeToken token, std::string_view message, f64 now, f64 duration = 5.0,
              std::string_view action = {}, u32 actionId = 0);

    /// Draws the toasts at time @p now (seconds) and drops expired ones. Returns the action id of a clicked
    /// action button. Item ids are "toast.<slot>.action" and "toast.<slot>.close".
    std::optional<u32> draw(UiKit& kit, f64 now);

    /// Number of toasts shown.
    [[nodiscard]] usize size() const noexcept { return m_count; }

    /// The message of toast @p index (0 is the oldest).
    [[nodiscard]] std::string_view message(usize index) const noexcept
    {
        return m_toasts[index].message.data();
    }

private:
    struct Toast
    {
        Icon icon;
        ThemeToken token = ThemeToken::Info;
        f64 start = 0.0;
        f64 end = 0.0;
        u32 actionId = 0;
        std::array<char, kTextCapacity> message{};
        std::array<char, kTextCapacity> action{};
    };

    std::array<Toast, kCapacity> m_toasts{};
    usize m_count = 0;
};

/// Ease-out progress (0 to 1) of an animation that started at @p start and lasts @p duration seconds;
/// always 1 when @p reduceMotion is set.
[[nodiscard]] f32 easeOut(f64 now, f64 start, f64 duration, bool reduceMotion) noexcept;

} // namespace illusion::editor::ui
