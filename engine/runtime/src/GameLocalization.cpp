#include <illusion/core/Log.hpp>
#include <illusion/input/InputStrings.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/GameLocalization.hpp>
#include <illusion/text/LocaleFile.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/MessageFormat.hpp>

#include <algorithm>
#include <utility>

namespace illusion
{
namespace
{

constexpr std::string_view kLocaleDirectory = "assets:/locales";

std::vector<text::LocaleFile> readLocaleFiles(const VirtualFileSystem& files)
{
    std::vector<text::LocaleFile> locales;
    std::vector<DirectoryEntry> entries;
    if (!files.exists(kLocaleDirectory) || files.list(kLocaleDirectory, entries) != FileError::None)
    {
        return locales;
    }
    for (const DirectoryEntry& entry : entries)
    {
        if (entry.isDirectory || !entry.name.ends_with(".json"))
        {
            continue;
        }
        const std::string path = std::string(kLocaleDirectory) + "/" + entry.name;
        std::string json;
        if (const FileError error = files.readText(path, json); error != FileError::None)
        {
            ILL_LOG_WARN("Game", "cannot read the locale file {} ({})", path, fileErrorName(error));
            continue;
        }
        Expected<text::LocaleFile, text::LocaleError> parsed = text::LocaleFile::parse(json);
        if (!parsed)
        {
            ILL_LOG_WARN("Game", "{}:{}:{}: {}", path, parsed.error().line, parsed.error().column,
                         parsed.error().message);
            continue;
        }
        locales.push_back(std::move(parsed).value());
    }
    // English first: its texts are the source of the game's table.
    std::stable_partition(locales.begin(), locales.end(),
                          [](const text::LocaleFile& file) { return file.locale.tag() == "en"; });
    return locales;
}

} // namespace

void loadGameLocalization(const VirtualFileSystem& files, text::Localization& localization,
                          GameStringTable& table)
{
    localization.addTable(input::strings::kTable);
    std::vector<text::LocaleFile> locales = readLocaleFiles(files);
    if (locales.empty())
    {
        return;
    }
    std::vector<std::pair<std::string, std::string>> texts;
    for (const text::LocaleFile& file : locales)
    {
        for (const text::LocaleEntry& entry : file.entries)
        {
            texts.emplace_back(entry.key, entry.text);
        }
    }
    std::stable_sort(texts.begin(), texts.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    texts.erase(std::unique(texts.begin(), texts.end(),
                            [](const auto& a, const auto& b) { return a.first == b.first; }),
                texts.end());
    for (auto& [key, english] : texts)
    {
        if (Expected<text::MessageFormat, text::MessageError> compiled =
                text::MessageFormat::compile(english);
            !compiled)
        {
            ILL_LOG_WARN("Game", "the text of \"{}\" does not compile: {}", key, compiled.error().message);
            continue;
        }
        table.keys.push_back(std::move(key));
        table.english.push_back(std::move(english));
    }
    table.keyViews.assign(table.keys.begin(), table.keys.end());
    table.englishViews.assign(table.english.begin(), table.english.end());
    table.table = text::StringTableData{"game", table.keyViews, table.englishViews};
    localization.addTable(table.table);
    for (text::LocaleFile& file : locales)
    {
        if (file.locale.tag() != "en")
        {
            localization.addLocale(std::move(file));
        }
    }
    localization.applyPendingLocale();
}

} // namespace illusion
