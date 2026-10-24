#include <illusion/core/Log.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/Localization.hpp>

#include <filesystem>
#include <format>
#include <vector>

namespace illusion::editor::ui
{

bool mountEditorData(VirtualFileSystem& vfs)
{
    const std::filesystem::path directory = executableDirectory() / "editor-data";
    const FileError error = vfs.mount("editor", directory);
    if (error != FileError::None)
    {
        const std::u8string text = directory.u8string();
        ILL_LOG_ERROR("Editor", "cannot mount '{}': {}",
                      std::string_view(reinterpret_cast<const char*>(text.data()), text.size()),
                      fileErrorName(error));
        return false;
    }
    return true;
}

Expected<Theme, std::string> loadTheme(const VirtualFileSystem& vfs, std::string_view name)
{
    const std::string path = std::format("editor:/themes/{}.json", name);
    std::string json;
    if (const FileError error = vfs.readText(path, json); error != FileError::None)
    {
        return unexpected(std::format("cannot read '{}': {}", path, fileErrorName(error)));
    }
    auto theme = Theme::parse(json);
    if (!theme)
    {
        return unexpected(std::format("{}: {}", path, theme.error()));
    }
    return std::move(theme.value());
}

u32 loadEditorLocales(const VirtualFileSystem& vfs, text::Localization& localization)
{
    std::vector<DirectoryEntry> entries;
    if (vfs.list("editor:/locales", entries) != FileError::None)
    {
        return 0;
    }
    u32 added = 0;
    for (const DirectoryEntry& entry : entries)
    {
        if (entry.isDirectory || entry.name == "en.json" || !entry.name.ends_with(".json"))
        {
            continue;
        }
        std::string json;
        if (vfs.readText("editor:/locales/" + entry.name, json) != FileError::None)
        {
            continue;
        }
        if (auto result = localization.addLocale(json); !result)
        {
            ILL_LOG_WARN("Editor", "skipping locales/{}: {}", entry.name, result.error().message);
            continue;
        }
        ++added;
    }
    return added;
}

} // namespace illusion::editor::ui
