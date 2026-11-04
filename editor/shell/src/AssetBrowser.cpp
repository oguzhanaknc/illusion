// The Asset Browser panel: the project's `assets:` mount as a navigable grid and list with thumbnails, a
// folder tree and favourites, search, sorting and a kind filter. File operations (rename, move, delete,
// reimport, reveal) run the asset commands, and the import settings of a selected texture or model are
// edited inline.

#include "Panels.hpp"

#include <illusion/assets/ImportSettings.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/ThumbnailCache.hpp>
#include <illusion/editor/core/ThumbnailHost.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr f32 kSidebarWidth = 190.0f;
constexpr f32 kInspectorWidth = 290.0f;
constexpr usize kMaxThumbnailsPerFrame = 4;

// The last path component (a file or folder name).
std::string_view leaf(std::string_view path) noexcept
{
    const usize slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// The folder that contains @p folder ("assets:/a/b" -> "assets:/a"; "assets:/" -> "assets:/").
std::string parentFolder(std::string_view folder)
{
    std::string_view base = folder;
    while (base.size() > 1 && base.back() == '/')
    {
        base.remove_suffix(1);
    }
    if (base == "assets:/" || base == "assets:")
    {
        return "assets:/";
    }
    const usize slash = base.find_last_of('/');
    if (slash == std::string_view::npos)
    {
        return "assets:/";
    }
    std::string parent(base.substr(0, slash));
    if (parent == "assets" || parent == "assets:")
    {
        return "assets:/";
    }
    return parent;
}

// True if @p path is a direct child of @p folder.
bool isDirectChild(std::string_view folder, std::string_view path) noexcept
{
    if (folder == "assets:/")
    {
        return path.starts_with("assets:/") && path.size() > folder.size() &&
               path.find('/', folder.size()) == std::string_view::npos;
    }
    return path.starts_with(folder) && path.size() > folder.size() + 1 && path[folder.size()] == '/' &&
           path.find('/', folder.size() + 1) == std::string_view::npos;
}

// Case-insensitive substring (ASCII folding), for searching by name.
bool containsIgnoreCase(std::string_view text, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    if (needle.size() > text.size())
    {
        return false;
    }
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (usize i = 0; i + needle.size() <= text.size(); ++i)
    {
        usize j = 0;
        for (; j < needle.size(); ++j)
        {
            if (lower(text[i + j]) != lower(needle[j]))
            {
                break;
            }
        }
        if (j == needle.size())
        {
            return true;
        }
    }
    return false;
}

// The icon of an asset kind.
ui::Icon kindIcon(std::string_view kind) noexcept
{
    if (kind == "Folder")
    {
        return ui::icons::kFolder;
    }
    if (kind == "Scene")
    {
        return ui::icons::kLandscape;
    }
    if (kind == "Material")
    {
        return ui::icons::kPalette;
    }
    if (kind == "Model")
    {
        return ui::icons::kDeployedCode;
    }
    if (kind == "Texture")
    {
        return ui::icons::kImage;
    }
    if (kind == "Environment")
    {
        return ui::icons::kWbTwilight;
    }
    if (kind == "AudioClip")
    {
        return ui::icons::kVolumeUp;
    }
    return ui::icons::kDescription;
}

// The file kinds of kAssetKinds without "Folder", in display order.
constexpr std::array<std::string_view, 7> kFileKinds{"Scene",       "Material",  "Model", "Texture",
                                                     "Environment", "AudioClip", "Other"};

// @p bytes as a short human-readable size into @p out.
void formatSize(u64 bytes, char* out, usize cap) noexcept
{
    const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    usize unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units))
    {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
    {
        std::snprintf(out, cap, "%llu B", static_cast<unsigned long long>(bytes));
    }
    else
    {
        std::snprintf(out, cap, "%.1f %s", value, units[unit]);
    }
}

} // namespace

struct AssetBrowser::State
{
    // One entry of the current folder, rebuilt when the catalog or the thumbnail cache changes.
    struct Entry
    {
        const AssetRecord* record = nullptr;
        bool folder = false;
        bool hasThumb = false;
        u64 thumbKey = 0;
        char thumbPath[40]{};
    };

    // Navigation.
    std::string folder = "assets:/";
    std::vector<std::string> back;
    std::vector<std::string> forward;

    // The cached listing, and the inputs it was built from.
    std::vector<Entry> entries;
    u64 entriesVersion = ~u64{0};
    u32 storedVersion = 0;
    std::string entriesFolder;
    std::string entriesSearch;
    int entriesKind = 0;
    int entriesSort = 0;

    // View state.
    bool grid = true;
    int kind = 0; // index into kFileKinds + 1 (0 = all)
    int sort = 0; // 0 name, 1 kind, 2 size, 3 modified
    std::array<char, 256> search{};

    // Selection (paths of files and folders), in click order.
    std::vector<std::string> selection;

    // Thumbnail keys requested this session, never asked again.
    std::unordered_set<u64> requested;

    // The import settings inspector: the single selected asset and its edited settings.
    std::string inspected;
    ImportSettings settings;
    ImportSettingsKind settingsKind = ImportSettingsKind::None;
    u32 references = 0;
    bool hasInfo = false;
    u64 infoVersion = ~u64{0};

    // The asset the context menu opened for.
    std::string contextPath;
    bool contextFolder = false;

    // Dialogs.
    enum class Dialog : u8
    {
        None,
        NewFolder,
        Rename,
        Move,
        Delete,
    };
    Dialog dialog = Dialog::None;
    std::array<char, 256> dialogText{};
    std::array<char, 256> moveTarget{};
    u32 deleteReferences = 0;

    State()
    {
        entries.reserve(64);
        selection.reserve(8);
    }

    void goTo(std::string target)
    {
        if (target == folder)
        {
            return;
        }
        back.push_back(std::move(folder));
        folder = std::move(target);
        forward.clear();
        selection.clear();
        inspected.clear();
        entriesVersion = ~u64{0};
    }

    bool isSelected(std::string_view path) const noexcept
    {
        return std::find(selection.begin(), selection.end(), path) != selection.end();
    }

    // Selects @p path: a plain click replaces the selection, a Ctrl click toggles it.
    void select(std::string_view path, bool additive)
    {
        if (additive)
        {
            const auto found = std::find(selection.begin(), selection.end(), path);
            if (found == selection.end())
            {
                selection.emplace_back(path);
            }
            else
            {
                selection.erase(found);
            }
        }
        else
        {
            selection.assign(1, std::string(path));
        }
        inspected.clear();
    }

    void rebuild(Impl& shell);
    void refreshInspector(Impl& shell);
    void openEntry(Impl& shell, std::string_view path, bool isFolder);
    void requestThumbnails(Impl& shell);
    void drawSidebar(Impl& shell, ui::UiKit& kit);
    void drawToolbar(Impl& shell, ui::UiKit& kit);
    void drawContent(Impl& shell, ui::UiKit& kit);
    void drawInspector(Impl& shell, ui::UiKit& kit);
    void drawContextMenu(Impl& shell);
    void drawDialogs(Impl& shell, ui::UiKit& kit);
};

void AssetBrowser::State::rebuild(Impl& shell)
{
    EditorContext& editor = shell.editor;
    const AssetCatalog& catalog = editor.assetCatalog();
    entries.clear();
    entriesFolder = folder;
    entriesSearch = std::string(search.data());
    entriesKind = kind;
    entriesSort = sort;
    entriesVersion = catalog.version();
    storedVersion = editor.thumbnails().isOpen() ? editor.thumbnails().statistics().stored : 0;
    const bool haveHost = editor.imageHost() != nullptr;
    const bool thumbsOpen = editor.thumbnails().isOpen();
    for (const auto& [path, record] : catalog.records())
    {
        if (!isDirectChild(folder, path))
        {
            continue;
        }
        Entry entry;
        entry.record = &record;
        entry.folder = record.kind == "Folder";
        if (!entry.folder)
        {
            if (kind > 0 && record.kind != kFileKinds[static_cast<usize>(kind - 1)])
            {
                continue;
            }
            if (!containsIgnoreCase(leaf(path), entriesSearch))
            {
                continue;
            }
            if (haveHost && thumbsOpen && thumbnailKindOf(path))
            {
                entry.thumbKey = thumbnailKey(catalog, path);
                if (entry.thumbKey != 0)
                {
                    std::snprintf(entry.thumbPath, sizeof(entry.thumbPath), "thumbnails:/%016llx.png",
                                  static_cast<unsigned long long>(entry.thumbKey));
                    entry.hasThumb = editor.thumbnails().find(path, entry.thumbKey);
                }
            }
        }
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        if (a.folder != b.folder)
        {
            return a.folder;
        }
        const AssetRecord& x = *a.record;
        const AssetRecord& y = *b.record;
        if (sort == 1)
        {
            if (x.kind != y.kind)
            {
                return x.kind < y.kind;
            }
        }
        else if (sort == 2 && x.size != y.size)
        {
            return x.size > y.size;
        }
        else if (sort == 3 && x.modified != y.modified)
        {
            return x.modified > y.modified;
        }
        return x.path < y.path;
    });
}

void AssetBrowser::State::refreshInspector(Impl& shell)
{
    EditorContext& editor = shell.editor;
    inspected = selection.size() == 1 ? selection[0] : std::string();
    settings = ImportSettings{};
    settingsKind = ImportSettingsKind::None;
    references = 0;
    hasInfo = false;
    if (inspected.empty())
    {
        return;
    }
    settingsKind = importSettingsKindOf(inspected);
    FilePathParams params;
    params.path = inspected;
    CommandOptions options;
    options.origin = "asset_browser";
    Expected<AssetInfoResult, CommandError> info =
        editor.execute<AssetInfoResult>("asset.info", params, options);
    if (info)
    {
        hasInfo = true;
        references = info->references;
        if (settingsKind != ImportSettingsKind::None && info->importSettings)
        {
            Expected<JsonDocument, JsonError> json = JsonDocument::parse(info->importSettings->text);
            if (json)
            {
                (void)mergeImportSettings(settings, json->root(), settingsKind);
            }
        }
    }
    infoVersion = editor.assetCatalog().version();
}

void AssetBrowser::State::openEntry(Impl& shell, std::string_view path, bool isFolder)
{
    if (isFolder)
    {
        goTo(std::string(path));
        return;
    }
    if (path.ends_with(".scene"))
    {
        (void)shell.run("scene.open", "{\"path\":" + jsonQuote(path) + "}", "asset_browser");
    }
    else
    {
        (void)shell.run("editor.open_source", "{\"path\":" + jsonQuote(path) + "}", "asset_browser");
    }
}

void AssetBrowser::State::requestThumbnails(Impl& shell)
{
    if (shell.editor.imageHost() == nullptr)
    {
        return;
    }
    usize requestedNow = 0;
    for (const Entry& entry : entries)
    {
        if (entry.folder || entry.thumbKey == 0 || entry.hasThumb || requested.count(entry.thumbKey))
        {
            continue;
        }
        requested.insert(entry.thumbKey);
        (void)shell.run("asset.thumbnail", "{\"path\":" + jsonQuote(entry.record->path) + "}",
                        "asset_browser");
        if (++requestedNow >= kMaxThumbnailsPerFrame)
        {
            break;
        }
    }
}

void AssetBrowser::State::drawSidebar(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    EditorContext& editor = shell.editor;
    const AssetCatalog& catalog = editor.assetCatalog();

    ImGui::BeginChild(text.hidden("asset_browser.sidebar"), ImVec2(kSidebarWidth * kit.scale, 0.0f));
    ui::sectionHeader(kit, text.format(s::kAssetBrowserFolders));
    usize idCount = 0;
    for (const auto& [path, record] : catalog.records())
    {
        if (record.kind != "Folder")
        {
            continue;
        }
        const usize depth = static_cast<usize>(std::count(path.begin(), path.end(), '/')) - 1;
        char number[24];
        std::snprintf(number, sizeof(number), "%zu", idCount++);
        const char* id = text.concat("asset_browser.folder.", number);
        if (depth > 0)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + static_cast<f32>(depth) * 14.0f * kit.scale);
        }
        if (ImGui::Selectable(text.verbatim(leaf(path), id), folder == path))
        {
            goTo(path);
        }
    }

    ImGui::Separator();
    ui::sectionHeader(kit, text.format(s::kAssetBrowserFavourites));
    const Preferences& prefs = editor.preferences();
    for (usize i = 0; i < prefs.assetFavourites.size(); ++i)
    {
        const std::string& favourite = prefs.assetFavourites[i];
        char number[24];
        std::snprintf(number, sizeof(number), "%zu", i);
        const char* id = text.concat("asset_browser.favourite.", number);
        const AssetRecord* record = catalog.find(favourite);
        const bool isFolder = record != nullptr && record->kind == "Folder";
        const ui::Icon icon =
            isFolder ? ui::icons::kFolderOpen : kindIcon(record != nullptr ? record->kind : "Other");
        const char* label = text.verbatim(text.concat(icon.c_str(), text.concat(" ", leaf(favourite))), id);
        if (ImGui::Selectable(label, false))
        {
            if (isFolder)
            {
                goTo(favourite);
            }
            else
            {
                goTo(parentFolder(favourite));
                selection.assign(1, favourite);
                inspected.clear();
            }
        }
    }
    ImGui::EndChild();
}

void AssetBrowser::State::drawToolbar(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const auto navigate = [&](std::string_view action) {
        if (action == "back")
        {
            forward.push_back(folder);
            folder = back.back();
            back.pop_back();
        }
        else if (action == "forward")
        {
            back.push_back(folder);
            folder = forward.back();
            forward.pop_back();
        }
        else
        {
            goTo(parentFolder(folder));
        }
        selection.clear();
        inspected.clear();
        entriesVersion = ~u64{0};
    };
    const auto navButton = [&](std::string_view id, ui::Icon icon, bool enabled, text::TextKey tooltip) {
        ImGui::BeginDisabled(!enabled);
        if (ui::iconButton(kit, icon, text.concat("asset_browser.", id), text.format(tooltip)))
        {
            navigate(id);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    };
    navButton("back", ui::icons::kArrowBack, !back.empty(), s::kAssetBrowserBack);
    navButton("forward", ui::icons::kArrowForward, !forward.empty(), s::kAssetBrowserForward);
    navButton("up", ui::icons::kArrowUpward, folder != "assets:/", s::kAssetBrowserUp);
    if (ui::iconButton(kit, ui::icons::kRefresh, "asset_browser.refresh",
                       text.format(s::kAssetBrowserRefresh)))
    {
        (void)shell.editor.assets();
    }
    ImGui::SameLine();

    // Breadcrumbs: the root "Assets" then each folder name.
    std::array<std::string_view, 32> segments{};
    usize count = 0;
    if (folder != "assets:/")
    {
        usize start = folder.find('/') + 1;
        while (start <= folder.size() && count < segments.size())
        {
            const usize end = folder.find('/', start);
            const usize stop = end == std::string::npos ? folder.size() : end;
            segments[count++] = std::string_view(folder).substr(start, stop - start);
            start = stop + 1;
        }
    }
    std::array<const char*, 32> parts{};
    parts[0] = text.format(s::kAssetBrowserRoot);
    usize partCount = 1;
    for (usize i = 0; i < count; ++i)
    {
        parts[partCount++] = text.copy(segments[i]);
    }
    if (const std::optional<usize> clicked = ui::breadcrumbs(
            kit, "asset_browser.crumbs", std::span<const char* const>(parts.data(), partCount)))
    {
        if (*clicked == 0)
        {
            goTo("assets:/");
        }
        else
        {
            std::string target = "assets:/";
            for (usize i = 0; i < *clicked; ++i)
            {
                if (i > 0)
                {
                    target += '/';
                }
                target += std::string(segments[i]);
            }
            goTo(std::move(target));
        }
    }

    // Second row: search, kind filter, sort, view toggle, new folder, favourite.
    ImGui::SetNextItemWidth(-1.0f);
    ui::searchField(kit, "asset_browser.search", search, text.format(s::kAssetBrowserSearch));
    ImGui::SameLine();
    const char* kindItems[8];
    kindItems[0] = text.format(s::kAssetBrowserKindAll);
    for (usize i = 0; i < kFileKinds.size(); ++i)
    {
        kindItems[i + 1] = text.copy(kFileKinds[i]);
    }
    ImGui::SetNextItemWidth(110.0f * kit.scale);
    if (ImGui::Combo(text.hidden("asset_browser.kind"), &kind, kindItems, 8))
    {
        entriesVersion = ~u64{0};
    }
    ImGui::SameLine();
    const char* sortItems[] = {text.format(s::kAssetBrowserSortName), text.format(s::kAssetBrowserSortKind),
                               text.format(s::kAssetBrowserSortSize),
                               text.format(s::kAssetBrowserSortModified)};
    ImGui::SetNextItemWidth(120.0f * kit.scale);
    if (ImGui::Combo(text.hidden("asset_browser.sort"), &sort, sortItems, 4))
    {
        entriesVersion = ~u64{0};
    }
    ImGui::SameLine();
    if (ui::iconButton(kit, grid ? ui::icons::kList : ui::icons::kGridView, "asset_browser.view",
                       text.format(grid ? s::kAssetBrowserList : s::kAssetBrowserGrid)))
    {
        grid = !grid;
    }
    ImGui::SameLine();
    if (ui::iconButton(kit, ui::icons::kCreateNewFolder, "asset_browser.new_folder",
                       text.format(s::kAssetBrowserNewFolder)))
    {
        dialog = Dialog::NewFolder;
        dialogText[0] = '\0';
    }
    ImGui::SameLine();
    const bool favourite =
        selection.size() == 1 && std::find(shell.editor.preferences().assetFavourites.begin(),
                                           shell.editor.preferences().assetFavourites.end(),
                                           selection[0]) != shell.editor.preferences().assetFavourites.end();
    ImGui::BeginDisabled(selection.size() != 1);
    if (ui::iconButton(
            kit, favourite ? ui::icons::kStar : ui::icons::kStarOutline, "asset_browser.favourite",
            text.format(favourite ? s::kAssetBrowserFavouriteRemove : s::kAssetBrowserFavouriteAdd)))
    {
        Preferences prefs = shell.editor.preferences();
        const auto found =
            std::find(prefs.assetFavourites.begin(), prefs.assetFavourites.end(), selection[0]);
        if (found == prefs.assetFavourites.end())
        {
            prefs.assetFavourites.push_back(selection[0]);
        }
        else
        {
            prefs.assetFavourites.erase(found);
        }
        (void)shell.editor.setPreferences(std::move(prefs));
    }
    ImGui::EndDisabled();
}

void AssetBrowser::State::drawContent(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const ImGuiIO& io = ImGui::GetIO();
    ImageHost* host = shell.editor.imageHost();

    if (entries.empty())
    {
        if (std::string_view(search.data()).empty())
        {
            ui::emptyState(kit, ui::icons::kFolderOpen, text.format(s::kAssetBrowserEmpty),
                           text.format(s::kAssetBrowserEmptyHint));
        }
        else
        {
            ui::emptyState(kit, ui::icons::kSearch, text.format(s::kAssetBrowserNoMatches), "");
        }
        return;
    }

    const auto interact = [&](usize i) {
        Entry& entry = entries[i];
        if (entry.folder)
        {
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                openEntry(shell, entry.record->path, true);
            }
        }
        else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            openEntry(shell, entry.record->path, false);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            contextPath = entry.record->path;
            contextFolder = entry.folder;
            if (!isSelected(contextPath))
            {
                select(contextPath, io.KeyCtrl);
            }
            ImGui::OpenPopup("###asset_browser.context");
        }
    };

    if (grid)
    {
        const f32 tile = 88.0f;
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const int columns =
            std::max(1, static_cast<int>((avail + ImGui::GetStyle().ItemSpacing.x) /
                                         (tile * kit.scale + ImGui::GetStyle().ItemSpacing.x)));
        const int rows = (static_cast<int>(entries.size()) + columns - 1) / columns;
        ImGui::BeginChild(text.hidden("asset_browser.grid"), ImVec2(0.0f, 0.0f));
        ImGuiListClipper clipper;
        clipper.Begin(rows);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                for (int col = 0; col < columns; ++col)
                {
                    const usize i =
                        static_cast<usize>(row) * static_cast<usize>(columns) + static_cast<usize>(col);
                    if (i >= entries.size())
                    {
                        break;
                    }
                    Entry& entry = entries[i];
                    char number[24];
                    std::snprintf(number, sizeof(number), "%zu", i);
                    const char* id = text.concat("asset_browser.tile.", number);
                    const bool selected = isSelected(entry.record->path);
                    u64 texture = 0;
                    if (entry.hasThumb && host != nullptr)
                    {
                        texture = host->imageTexture(entry.thumbPath);
                    }
                    bool clicked;
                    if (texture != 0)
                    {
                        clicked =
                            ui::assetTileImage(kit, id, texture, kindIcon(entry.record->kind),
                                               text.copy(leaf(entry.record->path)), nullptr, selected, tile);
                    }
                    else
                    {
                        clicked = ui::assetTile(kit, id, kindIcon(entry.record->kind),
                                                text.copy(leaf(entry.record->path)), nullptr, selected, tile);
                    }
                    if (clicked)
                    {
                        select(entry.record->path, io.KeyCtrl);
                    }
                    interact(i);
                    if (col + 1 < columns)
                    {
                        ImGui::SameLine();
                    }
                }
            }
        }
        ImGui::EndChild();
    }
    else
    {
        if (ImGui::BeginTable(text.hidden("asset_browser.list"), 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn(text.format(s::kAssetBrowserName), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(text.format(s::kAssetBrowserKind), ImGuiTableColumnFlags_WidthFixed,
                                    110.0f * kit.scale);
            ImGui::TableSetupColumn(text.format(s::kAssetBrowserSize), ImGuiTableColumnFlags_WidthFixed,
                                    90.0f * kit.scale);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(entries.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const usize i = static_cast<usize>(row);
                    Entry& entry = entries[i];
                    char number[24];
                    std::snprintf(number, sizeof(number), "%zu", i);
                    const char* id = text.concat("asset_browser.row.", number);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool selected = isSelected(entry.record->path);
                    const char* label = text.verbatim(text.concat(kindIcon(entry.record->kind).c_str(),
                                                                  text.concat(" ", leaf(entry.record->path))),
                                                      id);
                    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        select(entry.record->path, io.KeyCtrl);
                    }
                    interact(i);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(text.copy(entry.record->kind));
                    ImGui::TableNextColumn();
                    if (entry.folder)
                    {
                        ImGui::TextUnformatted("");
                    }
                    else
                    {
                        char sizeText[32];
                        formatSize(entry.record->size, sizeText, sizeof(sizeText));
                        ImGui::TextUnformatted(text.copy(sizeText));
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    drawContextMenu(shell);
}

void AssetBrowser::State::drawContextMenu(Impl& shell)
{
    ui::FrameText& text = shell.text;
    if (!ImGui::BeginPopup("###asset_browser.context"))
    {
        return;
    }
    if (ImGui::MenuItem(text.label(s::kAssetBrowserOpen, "asset_browser.context.open")))
    {
        openEntry(shell, contextPath, contextFolder);
    }
    if (!contextFolder && importSettingsKindOf(contextPath) != ImportSettingsKind::None)
    {
        if (ImGui::MenuItem(text.label(s::kAssetBrowserReimport, "asset_browser.context.reimport")))
        {
            (void)shell.run("asset.reimport", "{\"path\":" + jsonQuote(contextPath) + "}", "asset_browser");
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(text.label(s::kAssetBrowserRename, "asset_browser.context.rename"), "F2"))
    {
        dialog = Dialog::Rename;
        const std::string_view name = leaf(contextPath);
        const usize length = std::min(name.size(), dialogText.size() - 1);
        std::memcpy(dialogText.data(), name.data(), length);
        dialogText[length] = '\0';
    }
    if (ImGui::MenuItem(text.label(s::kAssetBrowserMove, "asset_browser.context.move")))
    {
        dialog = Dialog::Move;
        const std::string target = parentFolder(contextPath) + "/";
        const usize length = std::min(target.size(), moveTarget.size() - 1);
        std::memcpy(moveTarget.data(), target.data(), length);
        moveTarget[length] = '\0';
    }
    if (ImGui::MenuItem(text.label(s::kAssetBrowserDelete, "asset_browser.context.delete")))
    {
        dialog = Dialog::Delete;
        deleteReferences = 0;
        FilePathParams params;
        params.path = contextPath;
        CommandOptions options;
        options.origin = "asset_browser";
        if (Expected<AssetInfoResult, CommandError> info =
                shell.editor.execute<AssetInfoResult>("asset.info", params, options))
        {
            deleteReferences = info->references;
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(text.label(s::kAssetBrowserReveal, "asset_browser.context.reveal")))
    {
        (void)shell.run("editor.reveal", "{\"path\":" + jsonQuote(contextPath) + "}", "asset_browser");
    }
    if (ImGui::MenuItem(text.label(s::kAssetBrowserCopyPath, "asset_browser.context.copy_path")))
    {
        ImGui::SetClipboardText(text.copy(contextPath));
    }
    ImGui::EndPopup();
}

void AssetBrowser::State::drawInspector(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    ImGui::BeginChild(text.hidden("asset_browser.inspector"), ImVec2(kInspectorWidth * kit.scale, 0.0f));
    ui::sectionHeader(kit, text.format(s::kAssetBrowserImportTitle));
    ImGui::TextWrapped("%s", text.copy(leaf(inspected)));
    if (hasInfo)
    {
        ImGui::TextDisabled(
            "%s", text.format(s::kAssetBrowserReferences, {{"count", static_cast<u64>(references)}}));
    }
    ImGui::Separator();
    if (settingsKind == ImportSettingsKind::None)
    {
        ImGui::TextDisabled("%s", text.format(s::kAssetBrowserImportNone));
        ImGui::EndChild();
        return;
    }
    bool changed = false;
    if (ui::beginPropertyTable(kit, "asset_browser.import"))
    {
        if (settingsKind == ImportSettingsKind::Texture)
        {
            ui::propertyLabel(kit, text.format(s::kAssetBrowserImportColorSpace));
            const char* items[] = {text.format(s::kAssetBrowserImportColorSpaceAuto),
                                   text.format(s::kAssetBrowserImportColorSpaceSrgb),
                                   text.format(s::kAssetBrowserImportColorSpaceLinear)};
            int colorSpace = static_cast<int>(settings.texture.colorSpace);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(text.hidden("asset_browser.import.color_space"), &colorSpace, items, 3))
            {
                settings.texture.colorSpace = static_cast<ImportColorSpace>(colorSpace);
                changed = true;
            }
            ui::propertyLabel(kit, text.format(s::kAssetBrowserImportGenerateMips));
            if (ImGui::Checkbox(text.hidden("asset_browser.import.generate_mips"),
                                &settings.texture.generateMips))
            {
                changed = true;
            }
            ui::propertyLabel(kit, text.format(s::kAssetBrowserImportMaxSize));
            int maxSize = static_cast<int>(settings.texture.maxSize);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt(text.hidden("asset_browser.import.max_size"), &maxSize, 1.0f, 0, 16384))
            {
                settings.texture.maxSize = static_cast<u32>(maxSize);
                changed = true;
            }
        }
        else
        {
            ui::propertyLabel(kit, text.format(s::kAssetBrowserImportScale));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat(text.hidden("asset_browser.import.scale"), &settings.model.scale, 0.01f,
                                 0.001f, 1000.0f))
            {
                changed = true;
            }
        }
        ui::endPropertyTable();
    }
    ImGui::Separator();
    if (ui::primaryButton(kit, text.label(s::kAssetBrowserImportApply, "asset_browser.import.apply")))
    {
        const std::string value = importSettingsValue(settings, settingsKind);
        (void)shell.run("asset.set_import_settings",
                        "{\"path\":" + jsonQuote(inspected) + ",\"value\":" + value + "}", "asset_browser");
    }
    ImGui::SameLine();
    if (ImGui::Button(text.label(s::kAssetBrowserImportRevert, "asset_browser.import.revert")))
    {
        infoVersion = ~u64{0};
    }
    (void)changed;
    ImGui::EndChild();
}

void AssetBrowser::State::drawDialogs(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    if (dialog == Dialog::None)
    {
        return;
    }
    ImGui::OpenPopup("###asset_browser.dialog");
    bool open = true;
    if (!ImGui::BeginPopupModal(text.hidden("asset_browser.dialog"), &open,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    const char* name = text.copy(leaf(contextPath));
    switch (dialog)
    {
    case Dialog::NewFolder:
        ImGui::TextUnformatted(text.format(s::kAssetBrowserNewFolderTitle));
        ImGui::InputTextWithHint(text.hidden("asset_browser.dialog.new_folder"),
                                 text.format(s::kAssetBrowserNewFolderHint), dialogText.data(),
                                 dialogText.size());
        if (ui::primaryButton(kit, text.label(s::kAssetBrowserCreate, "asset_browser.dialog.create")))
        {
            (void)shell.run("asset.create_folder",
                            "{\"path\":" + jsonQuote(folder + "/" + std::string(dialogText.data())) + "}",
                            "asset_browser");
            dialog = Dialog::None;
        }
        break;
    case Dialog::Rename:
        ImGui::TextUnformatted(text.format(s::kAssetBrowserRenameTitle, {{"name", name}}));
        ImGui::InputTextWithHint(text.hidden("asset_browser.dialog.rename"),
                                 text.format(s::kAssetBrowserRenameHint), dialogText.data(),
                                 dialogText.size());
        if (ui::primaryButton(kit, text.label(s::kAssetBrowserCreate, "asset_browser.dialog.rename.confirm")))
        {
            (void)shell.run("asset.move",
                            "{\"from\":" + jsonQuote(contextPath) + ",\"to\":" +
                                jsonQuote(parentFolder(contextPath) + "/" + std::string(dialogText.data())) +
                                "}",
                            "asset_browser");
            dialog = Dialog::None;
        }
        break;
    case Dialog::Move:
        ImGui::TextUnformatted(text.format(s::kAssetBrowserMoveTitle, {{"name", name}}));
        ImGui::InputTextWithHint(text.hidden("asset_browser.dialog.move"),
                                 text.format(s::kAssetBrowserMoveHint), moveTarget.data(), moveTarget.size());
        if (ui::primaryButton(kit,
                              text.label(s::kAssetBrowserMoveAction, "asset_browser.dialog.move.confirm")))
        {
            std::string target(moveTarget.data());
            while (!target.empty() && target.back() == '/')
            {
                target.pop_back();
            }
            const std::string to = target + "/" + std::string(leaf(contextPath));
            (void)shell.run("asset.move",
                            "{\"from\":" + jsonQuote(contextPath) + ",\"to\":" + jsonQuote(to) + "}",
                            "asset_browser");
            dialog = Dialog::None;
        }
        break;
    case Dialog::Delete:
        ImGui::TextUnformatted(text.format(s::kAssetBrowserDeleteTitle, {{"name", name}}));
        ImGui::TextWrapped("%s", text.format(s::kAssetBrowserDeleteMessage, {{"name", name}}));
        if (ImGui::Button(text.label(s::kAssetBrowserDeleteConfirm, "asset_browser.dialog.delete")))
        {
            (void)shell.run("asset.delete", "{\"path\":" + jsonQuote(contextPath) + "}", "asset_browser");
            dialog = Dialog::None;
        }
        if (deleteReferences > 0)
        {
            ImGui::SameLine();
            if (ImGui::Button(text.label(s::kAssetBrowserDeleteForce, "asset_browser.dialog.delete.force")))
            {
                (void)shell.run("asset.delete", "{\"path\":" + jsonQuote(contextPath) + ",\"force\":true}",
                                "asset_browser");
                dialog = Dialog::None;
            }
        }
        break;
    case Dialog::None:
        break;
    }
    ImGui::EndPopup();
    if (!open)
    {
        dialog = Dialog::None;
    }
}

AssetBrowser::AssetBrowser()
    : m_state(std::make_unique<State>())
{
}

AssetBrowser::~AssetBrowser() = default;

void AssetBrowser::reveal(std::string_view path)
{
    State& st = *m_state;
    st.goTo(parentFolder(path));
    st.selection.assign(1, std::string(path));
    st.inspected.clear();
}

std::string_view AssetBrowser::currentFolder() const noexcept
{
    return m_state->folder;
}

void AssetBrowser::draw(Impl& shell)
{
    State& st = *m_state;
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    EditorContext& editor = shell.editor;

    if (editor.project() == nullptr)
    {
        ui::emptyState(kit, ui::icons::kFolderOpen, text.format(s::kPanelAssets),
                       text.format(s::kAssetBrowserEmptyHint));
        return;
    }
    const AssetCatalog& catalog = editor.assetCatalog();
    const u32 stored = editor.thumbnails().isOpen() ? editor.thumbnails().statistics().stored : 0;
    if (st.folder != st.entriesFolder || st.kind != st.entriesKind || st.sort != st.entriesSort ||
        st.entriesSearch != st.search.data() || catalog.version() != st.entriesVersion ||
        stored != st.storedVersion)
    {
        st.rebuild(shell);
    }
    const std::string_view single =
        st.selection.size() == 1 ? std::string_view(st.selection[0]) : std::string_view();
    if (single != st.inspected || st.infoVersion != catalog.version())
    {
        st.refreshInspector(shell);
    }
    st.requestThumbnails(shell);

    st.drawSidebar(shell, kit);
    ImGui::SameLine();
    ImGui::BeginChild(text.hidden("asset_browser.main"), ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar);
    st.drawToolbar(shell, kit);
    ImGui::Separator();
    st.drawContent(shell, kit);
    ImGui::EndChild();
    if (st.selection.size() == 1)
    {
        ImGui::SameLine();
        st.drawInspector(shell, kit);
    }
    st.drawDialogs(shell, kit);
}

} // namespace illusion::editor::shell::detail
