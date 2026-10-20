#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <imgui.h>

namespace illusion::editor::ui
{

namespace
{

enum FileIndex : usize
{
    kRegular,
    kMedium,
    kBold,
    kMono,
    kSymbols,
};

// Material Symbols live in the private use areas; restricting the merged source to them keeps the text
// font's own glyphs for everything else.
constexpr ImWchar kSymbolRanges[] = {0xE000, 0xF8FF, 0xF0000, 0xFFFFD, 0};

constexpr std::array<f32, kFontRoleCount> kSizes = {15.0f, 13.0f, 15.0f, 17.0f, 14.0f, 20.0f};

ImFontConfig fontConfig(std::vector<std::byte>& data, const char* name, f32 size)
{
    ImFontConfig config;
    config.FontData = data.data();
    config.FontDataSize = static_cast<int>(data.size());
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = size;
    const usize length = std::min(std::strlen(name), sizeof(config.Name) - 1);
    std::memcpy(config.Name, name, length);
    config.Name[length] = '\0';
    return config;
}

// Icons are drawn about 1.2x the text size, centred on the text's x-height, with a fixed advance so that
// icons in columns line up.
ImFontConfig symbolConfig(std::vector<std::byte>& data, f32 textSize, bool merge)
{
    const f32 size = merge ? std::round(textSize * 1.2f) : textSize;
    ImFontConfig config = fontConfig(data, "Material Symbols Rounded", size);
    config.MergeMode = merge;
    config.GlyphRanges = kSymbolRanges;
    config.GlyphMinAdvanceX = size;
    config.GlyphOffset = ImVec2(0.0f, merge ? std::round(size * 0.18f) : 0.0f);
    return config;
}

} // namespace

Expected<std::unique_ptr<FontManager>, std::string>
FontManager::load(UiContext& context, const VirtualFileSystem& vfs, std::string_view directory)
{
    std::unique_ptr<FontManager> manager(new FontManager());
    manager->m_context = &context;
    for (usize i = 0; i < kFiles.size(); ++i)
    {
        const std::string path = std::format("{}/{}", directory, kFiles[i]);
        const FileError error = vfs.readFile(path, manager->m_data[i]);
        if (error != FileError::None)
        {
            return unexpected(std::format("cannot read '{}': {}", path, fileErrorName(error)));
        }
        if (manager->m_data[i].empty())
        {
            return unexpected(std::format("'{}' is empty", path));
        }
    }

    context.makeCurrent();
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    const auto addText = [&](FileIndex file, const char* name, f32 pixels) -> ImFont* {
        const ImFontConfig text = fontConfig(manager->m_data[file], name, pixels);
        ImFont* font = atlas.AddFont(&text);
        if (font != nullptr)
        {
            const ImFontConfig symbols = symbolConfig(manager->m_data[kSymbols], pixels, true);
            if (atlas.AddFont(&symbols) == nullptr)
            {
                return nullptr;
            }
        }
        return font;
    };

    ImFont* body = addText(kRegular, "Roboto Regular", size(FontRole::Body));
    ImFont* strong = addText(kMedium, "Roboto Medium", size(FontRole::Strong));
    ImFont* title = addText(kBold, "Roboto Bold", size(FontRole::Title));
    ImFont* mono = addText(kMono, "Roboto Mono", size(FontRole::Mono));
    const ImFontConfig iconConfig = symbolConfig(manager->m_data[kSymbols], size(FontRole::Icon), false);
    ImFont* icon = atlas.AddFont(&iconConfig);
    if (body == nullptr || strong == nullptr || title == nullptr || mono == nullptr || icon == nullptr)
    {
        for (ImFont* font : {body, strong, title, mono, icon})
        {
            if (font != nullptr)
            {
                atlas.RemoveFont(font);
            }
        }
        return unexpected(std::format("the fonts in '{}' are not valid TrueType fonts", directory));
    }
    manager->m_fonts = {body, body, strong, title, mono, icon};
    ImGui::GetIO().FontDefault = body;
    ImGui::GetStyle().FontSizeBase = size(FontRole::Body);
    return manager;
}

FontManager::~FontManager()
{
    m_context->makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    if (io.FontDefault == m_fonts[0])
    {
        io.FontDefault = nullptr;
    }
    for (usize i = 0; i < m_fonts.size(); ++i)
    {
        // Caption shares Body's font; a manager that failed to load has none.
        if (i != static_cast<usize>(FontRole::Caption) && m_fonts[i] != nullptr)
        {
            io.Fonts->RemoveFont(m_fonts[i]);
        }
    }
}

f32 FontManager::size(FontRole role) noexcept
{
    return kSizes[static_cast<usize>(role)];
}

void FontManager::setScale(f32 uiScale, f32 contentScale) const noexcept
{
    m_context->makeCurrent();
    ImGui::GetStyle().FontScaleDpi = uiScale * contentScale;
}

void FontManager::push(FontRole role) const noexcept
{
    ImGui::PushFont(font(role), size(role));
}

FontScope::FontScope(const FontManager& fonts, FontRole role) noexcept
{
    fonts.push(role);
}

FontScope::~FontScope()
{
    ImGui::PopFont();
}

} // namespace illusion::editor::ui
