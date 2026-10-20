#pragma once

/// @file
/// The editor's fonts: Roboto for text, Roboto Mono for code and numbers, and Material Symbols merged into
/// every text font, so labels can mix text and icons. Fonts are dynamic (Dear ImGui 1.92+): glyphs are
/// rasterised on demand at the size in use, so changing the UI scale or moving to another monitor only
/// changes `ImGuiStyle::FontScaleDpi`.
///
/// ```cpp
/// auto fonts = FontManager::load(context, vfs, "editor:/fonts");
/// fonts.value()->setScale(settings.uiScale, window.contentScale());
/// {
///     FontScope title(**fonts, FontRole::Title);
///     ImGui::TextUnformatted(text.format(strings::kWelcomeTitle));
/// }
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImFont;

namespace illusion
{
class VirtualFileSystem;
} // namespace illusion

namespace illusion::editor::ui
{

class UiContext;

/// The roles text plays in the editor; each has a face and a size (see the editor architecture page).
enum class FontRole : u8
{
    Body,    ///< Roboto Regular 15 px: body text (the default font).
    Caption, ///< Roboto Regular 13 px: captions, hints, status bar.
    Strong,  ///< Roboto Medium 15 px: headers, tabs and buttons.
    Title,   ///< Roboto Bold 17 px: titles.
    Mono,    ///< Roboto Mono 14 px: console, JSON, code and numeric columns.
    Icon,    ///< Material Symbols 20 px: toolbar icons (larger tiles push their own size).
};

/// Number of font roles.
inline constexpr usize kFontRoleCount = 6;

/// Owns the editor's font data and the ImFont of each role in a UiContext's atlas.
class FontManager : NonCopyable
{
public:
    /// The files load() reads from its directory.
    static constexpr std::array<std::string_view, 5> kFiles = {
        "Roboto-Regular.ttf",     "Roboto-Medium.ttf",          "Roboto-Bold.ttf",
        "RobotoMono-Regular.ttf", "MaterialSymbolsRounded.ttf",
    };

    /// Reads the fonts from the virtual directory @p directory and adds them to the atlas of @p context
    /// (which must be current and outlive the manager). Body becomes the default font. Returns why a file
    /// could not be read or used.
    [[nodiscard]] static Expected<std::unique_ptr<FontManager>, std::string>
    load(UiContext& context, const VirtualFileSystem& vfs, std::string_view directory);

    ~FontManager();

    /// The font of @p role.
    [[nodiscard]] ImFont* font(FontRole role) const noexcept { return m_fonts[static_cast<usize>(role)]; }

    /// The size of @p role in pixels at 100% scale.
    [[nodiscard]] static f32 size(FontRole role) noexcept;

    /// Sets the global font scale: @p uiScale (the user setting, 0.75 to 2) times @p contentScale (the
    /// monitor's). Takes effect at the next frame.
    void setScale(f32 uiScale, f32 contentScale) const noexcept;

    /// Pushes the font and size of @p role; pop with ImGui::PopFont() or use FontScope.
    void push(FontRole role) const noexcept;

private:
    FontManager() = default;

    UiContext* m_context = nullptr;
    std::array<std::vector<std::byte>, kFiles.size()> m_data;
    std::array<ImFont*, kFontRoleCount> m_fonts{};
};

/// Pushes a font role for the lifetime of the scope.
class FontScope : NonCopyable
{
public:
    /// Pushes @p role of @p fonts.
    FontScope(const FontManager& fonts, FontRole role) noexcept;

    /// Pops the font.
    ~FontScope();
};

} // namespace illusion::editor::ui
