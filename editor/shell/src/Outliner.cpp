// The Outliner panel: virtualized scene hierarchy tree with search filters, multi-selection, inline rename,
// drag-and-drop reparenting and reordering, context menus, and viewport visibility/lock toggles.

#include "Panels.hpp"

#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/text/FuzzyMatcher.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <imgui_internal.h>
#include <unordered_set>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr const char* kEntityPayload = "ILLUSION_ENTITY";

const char* indexed(ui::FrameText& text, std::string_view prefix, usize index)
{
    char number[24];
    const auto [end, _] = std::to_chars(number, number + sizeof(number), index);
    return text.concat(prefix, std::string_view(number, static_cast<usize>(end - number)));
}

bool containsFoldedAscii(std::string_view text, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    const auto lower = [](char c) noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (usize i = 0; i + needle.size() <= text.size(); ++i)
    {
        usize j = 0;
        while (j < needle.size() && lower(text[i + j]) == lower(needle[j]))
        {
            ++j;
        }
        if (j == needle.size())
        {
            return true;
        }
    }
    return false;
}

std::string_view shortTypeName(std::string_view stableName) noexcept
{
    const usize dot = stableName.rfind('.');
    return dot == std::string_view::npos ? stableName : stableName.substr(dot + 1);
}

struct ParsedFilter
{
    std::array<std::string_view, 8> nameTokens{};
    usize nameCount = 0;
    std::array<std::string_view, 8> typeTokens{};
    usize typeCount = 0;
    std::optional<bool> disabled;
    std::optional<std::string_view> prefab;

    [[nodiscard]] bool active() const noexcept
    {
        return nameCount > 0 || typeCount > 0 || disabled.has_value() || prefab.has_value();
    }
};

ParsedFilter parseFilter(std::string_view query) noexcept
{
    ParsedFilter out;
    while (!query.empty())
    {
        while (!query.empty() && query.front() == ' ')
        {
            query.remove_prefix(1);
        }
        if (query.empty())
        {
            break;
        }
        const usize space = query.find(' ');
        const std::string_view token = query.substr(0, space);
        query = space == std::string_view::npos ? std::string_view() : query.substr(space + 1);

        if (token.starts_with("t:") || token.starts_with("T:"))
        {
            const std::string_view value = token.substr(2);
            if (!value.empty() && out.typeCount < out.typeTokens.size())
            {
                out.typeTokens[out.typeCount++] = value;
            }
        }
        else if (token.starts_with("has:") || token.starts_with("HAS:"))
        {
            const std::string_view value = token.substr(4);
            if (!value.empty() && out.typeCount < out.typeTokens.size())
            {
                out.typeTokens[out.typeCount++] = value;
            }
        }
        else if (token.starts_with("disabled:") || token.starts_with("DISABLED:"))
        {
            const std::string_view value = token.substr(9);
            if (containsFoldedAscii(value, "true") || value == "1" || containsFoldedAscii(value, "yes"))
            {
                out.disabled = true;
            }
            else if (containsFoldedAscii(value, "false") || value == "0" || containsFoldedAscii(value, "no"))
            {
                out.disabled = false;
            }
        }
        else if (token.starts_with("prefab:") || token.starts_with("PREFAB:"))
        {
            out.prefab = token.substr(7);
        }
        else if (out.nameCount < out.nameTokens.size())
        {
            out.nameTokens[out.nameCount++] = token;
        }
    }
    return out;
}

} // namespace

struct Outliner::State
{
    struct Row
    {
        Entity entity{};
        EntityGuid guid{};
        EntityGuid parentGuid{};
        std::array<char, kGuidTextLength + 1> idText{};
        const char* name = nullptr; // null when unnamed
        u32 depth = 0;
        u32 childCount = 0;
        u32 siblingIndex = 0;
        ui::Icon icon = ui::icons::kDeployedCode;
        bool disabled = false;
        bool hidden = false;
        bool locked = false;
    };

    struct StackEntry
    {
        Entity entity{};
        EntityGuid guid{};
        u32 nextChildIndex = 0;
    };

    std::array<char, 128> search{};
    std::array<char, 128> lastSearch{};
    std::vector<Row> rows;
    std::unordered_set<u64> collapsed;
    std::vector<StackEntry> parentStack;
    std::vector<std::pair<u64, Entity>> unindexed;

    // Cached versions for zero-allocation idle frames.
    const World* cachedWorld = nullptr;
    u64 structuralVersion = ~0ull;
    u64 hierarchyVersion = ~0ull;
    u64 nameVersion = ~0ull;
    u64 disabledVersion = ~0ull;
    u64 inactiveVersion = ~0ull;
    u64 viewVersion = ~0ull;
    u64 editorRevision = ~0ull;
    u64 collapseVersion = 0;
    u64 cachedCollapseVersion = ~0ull;
    bool hasFilter = false;

    // Cached special ComponentIds for icons.
    ComponentId cameraId = kInvalidComponentId;
    ComponentId dirLightId = kInvalidComponentId;
    ComponentId pointLightId = kInvalidComponentId;
    ComponentId skyLightId = kInvalidComponentId;
    ComponentId characterId = kInvalidComponentId;
    ComponentId audioSourceId = kInvalidComponentId;
    ComponentId meshRendererId = kInvalidComponentId;
    u32 cachedRegistryCount = 0;

    // Selection & inline rename state.
    EntityGuid lastClickedGuid{};
    EntityGuid renamingGuid{};
    bool focusRename = false;
    std::array<char, 128> renameBuffer{};

    void refreshComponentIds(const ComponentRegistry& registry) noexcept
    {
        const u32 count = registry.count();
        if (count == cachedRegistryCount)
        {
            return;
        }
        cachedRegistryCount = count;
        cameraId = registry.findByStableName("illusion.Camera");
        dirLightId = registry.findByStableName("illusion.DirectionalLight");
        pointLightId = registry.findByStableName("illusion.PointLight");
        skyLightId = registry.findByStableName("illusion.SkyLight");
        characterId = registry.findByStableName("illusion.CharacterController");
        audioSourceId = registry.findByStableName("illusion.AudioSource");
        meshRendererId = registry.findByStableName("illusion.MeshRenderer");
    }

    [[nodiscard]] ui::Icon iconFor(const World& world, Entity entity, u32 childCount) const noexcept
    {
        if (cameraId != kInvalidComponentId && world.componentData(entity, cameraId) != nullptr)
        {
            return ui::icons::kVideocam;
        }
        if (dirLightId != kInvalidComponentId && world.componentData(entity, dirLightId) != nullptr)
        {
            return ui::icons::kLightMode;
        }
        if (pointLightId != kInvalidComponentId && world.componentData(entity, pointLightId) != nullptr)
        {
            return ui::icons::kLightbulb;
        }
        if (skyLightId != kInvalidComponentId && world.componentData(entity, skyLightId) != nullptr)
        {
            return ui::icons::kWbTwilight;
        }
        if (characterId != kInvalidComponentId && world.componentData(entity, characterId) != nullptr)
        {
            return ui::icons::kDirectionsWalk;
        }
        if (audioSourceId != kInvalidComponentId && world.componentData(entity, audioSourceId) != nullptr)
        {
            return ui::icons::kVolumeUp;
        }
        if (meshRendererId != kInvalidComponentId && world.componentData(entity, meshRendererId) != nullptr)
        {
            return ui::icons::kDeployedCode;
        }
        return childCount > 0 ? ui::icons::kInventory2 : ui::icons::kDeployedCode;
    }

    [[nodiscard]] bool matches(const World& world, Entity entity, std::string_view name, bool isDisabled,
                               const ParsedFilter& filter) const
    {
        if (filter.disabled && *filter.disabled != isDisabled)
        {
            return false;
        }
        if (filter.prefab)
        {
            const std::string_view want = *filter.prefab;
            const bool wantNone =
                want.empty() || containsFoldedAscii(want, "false") || containsFoldedAscii(want, "none");
            if (!wantNone)
            {
                return false;
            }
        }
        for (usize i = 0; i < filter.nameCount; ++i)
        {
            const std::string_view token = filter.nameTokens[i];
            if (!containsFoldedAscii(name, token) && !text::FuzzyMatcher(token).score(name).has_value())
            {
                return false;
            }
        }
        if (filter.typeCount > 0)
        {
            const ComponentRegistry& registry = world.registry();
            const std::span<const ComponentId> types = world.componentsOf(entity);
            for (usize i = 0; i < filter.typeCount; ++i)
            {
                const std::string_view needle = filter.typeTokens[i];
                bool found = false;
                for (const ComponentId id : types)
                {
                    const TypeReflection* reflection = registry.reflection(id);
                    if (reflection == nullptr)
                    {
                        continue;
                    }
                    if (containsFoldedAscii(reflection->name, needle) ||
                        containsFoldedAscii(shortTypeName(reflection->name), needle) ||
                        containsFoldedAscii(displayNameOf(*reflection), needle))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
        }
        return true;
    }

    void rebuildIfNeeded(EditorContext& editor)
    {
        World& world = editor.world();
        const ViewState& view = editor.view();
        const u64 curStructural = world.structuralVersion();
        const u64 curHierarchy = world.componentVersion<Hierarchy>();
        const u64 curName = world.componentVersion<Name>();
        const u64 curDisabled = world.componentVersion<Disabled>();
        const u64 curInactive = world.componentVersion<Inactive>();
        const u64 curView = view.version();
        const u64 curRevision = editor.revision();
        const bool searchChanged = std::strcmp(search.data(), lastSearch.data()) != 0;

        if (&world == cachedWorld && curStructural == structuralVersion && curHierarchy == hierarchyVersion &&
            curName == nameVersion && curDisabled == disabledVersion && curInactive == inactiveVersion &&
            curView == viewVersion && curRevision == editorRevision &&
            collapseVersion == cachedCollapseVersion && !searchChanged)
        {
            return;
        }

        ILL_PROFILE_SCOPE();
        cachedWorld = &world;
        structuralVersion = curStructural;
        hierarchyVersion = curHierarchy;
        nameVersion = curName;
        disabledVersion = curDisabled;
        inactiveVersion = curInactive;
        viewVersion = curView;
        editorRevision = curRevision;
        cachedCollapseVersion = collapseVersion;
        lastSearch = search;

        refreshComponentIds(world.registry());

        const ParsedFilter filter = parseFilter(search.data());
        hasFilter = filter.active();
        const bool checkHidden = !view.hiddenEntities().empty();
        const bool checkLocked = !view.lockedEntities().empty();
        const bool checkCollapsed = !hasFilter && !collapsed.empty();

        rows.clear();
        rows.reserve(world.aliveCount());
        parentStack.clear();

        const HierarchyIndex& index = editor.entities().hierarchy();
        const std::span<const Entity> preOrder = index.preOrder();
        u32 nextRootIndex = 0;

        const auto pushEntity = [&](Entity entity, u32 depth, u32 childCount, u32 siblingIndex,
                                    EntityGuid parentGuid) {
            const EntityGuid* guidPtr = world.tryGet<EntityGuid>(entity);
            const EntityGuid guid = guidPtr != nullptr ? *guidPtr : EntityGuid{};
            const Name* nameComp = world.tryGet<Name>(entity);
            const char* namePtr =
                nameComp != nullptr && !nameComp->value.empty() ? nameComp->value.c_str() : nullptr;
            const std::string_view nameView = namePtr != nullptr ? std::string_view(namePtr) : "Unnamed";
            const bool isDisabled = world.has<Disabled>(entity) || world.has<Inactive>(entity);
            if (hasFilter && !matches(world, entity, nameView, isDisabled, filter))
            {
                return;
            }
            Row& row = rows.emplace_back();
            row.entity = entity;
            row.guid = guid;
            row.parentGuid = parentGuid;
            const auto formatted = formatGuid(guid);
            std::memcpy(row.idText.data(), formatted.data(), kGuidTextLength);
            row.idText[kGuidTextLength] = '\0';
            row.name = namePtr;
            row.depth = hasFilter ? 0u : depth;
            row.childCount = childCount;
            row.siblingIndex = siblingIndex;
            row.icon = iconFor(world, entity, childCount);
            row.disabled = isDisabled;
            row.hidden = checkHidden && view.hidden(guid);
            row.locked = checkLocked && view.locked(guid);
        };

        usize i = 0;
        while (i < preOrder.size())
        {
            const Entity entity = preOrder[i];
            const u32 depth = index.depth(entity);
            while (parentStack.size() > depth)
            {
                parentStack.pop_back();
            }
            const u32 siblingIndex = depth == 0 ? nextRootIndex++ : parentStack.back().nextChildIndex++;
            const EntityGuid parentGuid = depth == 0 ? EntityGuid{} : parentStack.back().guid;
            const u32 childCount = static_cast<u32>(index.children(entity).size());
            const EntityGuid* guidPtr = world.tryGet<EntityGuid>(entity);
            const EntityGuid guid = guidPtr != nullptr ? *guidPtr : EntityGuid{};

            pushEntity(entity, depth, childCount, siblingIndex, parentGuid);

            if (childCount > 0 && checkCollapsed && collapsed.contains(guid.value))
            {
                const usize subtreeSize = index.subtree(entity).size();
                i += subtreeSize > 0 ? subtreeSize : 1;
                continue;
            }
            if (childCount > 0)
            {
                parentStack.push_back(StackEntry{entity, guid, 0});
            }
            ++i;
        }

        if (index.size() < world.aliveCount())
        {
            unindexed.clear();
            world.query<const EntityGuid>().each([&](Entity entity, const EntityGuid& guid) {
                if (!index.contains(entity))
                {
                    unindexed.emplace_back(guid.value, entity);
                }
            });
            std::sort(unindexed.begin(), unindexed.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [_, entity] : unindexed)
            {
                pushEntity(entity, 0, 0, nextRootIndex++, EntityGuid{});
            }
        }
    }
};

Outliner::Outliner()
    : m_state(std::make_unique<State>())
{
}

Outliner::~Outliner() = default;

void Outliner::beginRename(EntityGuid entity) noexcept
{
    m_state->renamingGuid = entity;
    m_state->focusRename = true;
}

void Outliner::draw(Impl& shell)
{
    State& state = *m_state;
    EditorContext& editor = shell.editor;
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const ImGuiStyle& style = ImGui::GetStyle();

    // Header: search field and "+" create entity button.
    const f32 buttonSide = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(
        std::max(60.0f * kit.scale, ImGui::GetContentRegionAvail().x - buttonSide - style.ItemSpacing.x));
    ui::searchField(kit, "outliner.search", state.search, text.format(s::kOutlinerSearch));
    ImGui::SameLine();
    if (ui::iconButton(kit, ui::icons::kAdd, "outliner.create", text.format(s::kOutlinerCreateEmpty)))
    {
        (void)shell.run("entity.create", R"({"name":"Entity"})", "outliner");
    }

    state.rebuildIfNeeded(editor);

    if (editor.world().aliveCount() == 0)
    {
        ui::emptyState(kit, ui::icons::kAccountTree, text.format(s::kOutlinerEmptyTitle),
                       text.format(s::kOutlinerEmptyHint));
        return;
    }

    // Begin rename if requested on the current primary selection.
    if (state.focusRename && !state.renamingGuid.isNull())
    {
        for (const State::Row& row : state.rows)
        {
            if (row.guid == state.renamingGuid)
            {
                const std::string_view current = row.name != nullptr ? row.name : "";
                const usize len = std::min(current.size(), state.renameBuffer.size() - 1);
                if (len > 0)
                {
                    std::memcpy(state.renameBuffer.data(), current.data(), len);
                }
                state.renameBuffer[len] = '\0';
                break;
            }
        }
    }

    if (ImGui::BeginChild(text.hidden("outliner.tree"), ImVec2(0.0f, 0.0f)))
    {
        if (state.rows.empty())
        {
            ImGui::TextDisabled("%s", text.format(s::kPaletteEmpty));
        }

        // Keyboard navigation when the Outliner tree is focused and not typing into a field.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && state.renamingGuid.isNull() &&
            !ImGui::GetIO().WantTextInput && !state.rows.empty())
        {
            const EntityGuid primary = editor.selection().primary();
            std::optional<usize> currentIdx;
            for (usize idx = 0; idx < state.rows.size(); ++idx)
            {
                if (state.rows[idx].guid == primary)
                {
                    currentIdx = idx;
                    break;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                const usize target = currentIdx && *currentIdx > 0 ? *currentIdx - 1 : 0;
                const char* idText = state.rows[target].idText.data();
                (void)shell.run("selection.set",
                                text.concat(R"({"entities":[")", text.concat(idText, R"("]})")), "outliner");
                state.lastClickedGuid = state.rows[target].guid;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                const usize target = currentIdx ? std::min(*currentIdx + 1, state.rows.size() - 1) : 0;
                const char* idText = state.rows[target].idText.data();
                (void)shell.run("selection.set",
                                text.concat(R"({"entities":[")", text.concat(idText, R"("]})")), "outliner");
                state.lastClickedGuid = state.rows[target].guid;
            }
            else if (currentIdx && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            {
                const State::Row& row = state.rows[*currentIdx];
                if (row.childCount > 0 && !state.collapsed.contains(row.guid.value))
                {
                    state.collapsed.insert(row.guid.value);
                    ++state.collapseVersion;
                }
                else if (!row.parentGuid.isNull())
                {
                    const auto parentId = formatGuid(row.parentGuid);
                    const std::string_view parentView(parentId.data(), parentId.size());
                    (void)shell.run("selection.set",
                                    text.concat(R"({"entities":[")", text.concat(parentView, R"("]})")),
                                    "outliner");
                }
            }
            else if (currentIdx && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            {
                const State::Row& row = state.rows[*currentIdx];
                if (row.childCount > 0 && state.collapsed.contains(row.guid.value))
                {
                    state.collapsed.erase(row.guid.value);
                    ++state.collapseVersion;
                }
            }
            else if (currentIdx && (ImGui::IsKeyPressed(ImGuiKey_F2) || ImGui::IsKeyPressed(ImGuiKey_Enter)))
            {
                beginRename(state.rows[*currentIdx].guid);
                const std::string_view current =
                    state.rows[*currentIdx].name != nullptr ? state.rows[*currentIdx].name : "";
                const usize len = std::min(current.size(), state.renameBuffer.size() - 1);
                if (len > 0)
                {
                    std::memcpy(state.renameBuffer.data(), current.data(), len);
                }
                state.renameBuffer[len] = '\0';
            }
        }

        const f32 rowHeight = ImGui::GetFrameHeight();
        const f32 rightButtonsWidth = rowHeight * 2.0f + 2.0f * kit.scale;
        const Selection& selection = editor.selection();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Deferred action from row interaction (executed outside the clipper loop).
        std::optional<usize> clickedRow;
        bool clickedCtrl = false;
        bool clickedShift = false;
        std::optional<EntityGuid> contextRowGuid;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.rows.size()), rowHeight);
        while (clipper.Step())
        {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
            {
                const auto rowIndex = static_cast<usize>(r);
                const State::Row& row = state.rows[rowIndex];
                const char* rowIdText = row.idText.data();
                const bool isSelected = selection.contains(row.guid);

                ImGui::PushID(rowIdText);
                const f32 indent = static_cast<f32>(row.depth) * style.IndentSpacing;
                if (indent > 0.0f)
                {
                    ImGui::Indent(indent);
                }

                // Expand / collapse chevron.
                if (row.childCount > 0 && !state.hasFilter)
                {
                    const bool isCollapsed = state.collapsed.contains(row.guid.value);
                    const ui::Icon chevron = isCollapsed ? ui::icons::kChevronRight : ui::icons::kExpandMore;
                    if (ui::iconButton(kit, chevron, indexed(text, "outliner.expand.", rowIndex)))
                    {
                        if (isCollapsed)
                        {
                            state.collapsed.erase(row.guid.value);
                        }
                        else
                        {
                            state.collapsed.insert(row.guid.value);
                        }
                        ++state.collapseVersion;
                    }
                }
                else
                {
                    ImGui::Dummy(ImVec2(rowHeight, rowHeight));
                }
                ImGui::SameLine(0.0f, 2.0f * kit.scale);

                const f32 availWidth = ImGui::GetContentRegionAvail().x;
                const f32 selectWidth = std::max(40.0f * kit.scale, availWidth - rightButtonsWidth);

                if (state.renamingGuid == row.guid)
                {
                    ImGui::SetNextItemWidth(selectWidth);
                    if (state.focusRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        state.focusRename = false;
                    }
                    const bool enter = ImGui::InputText(
                        text.hidden("outliner.rename"), state.renameBuffer.data(), state.renameBuffer.size(),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    {
                        state.renamingGuid = {};
                    }
                    else if (enter || ImGui::IsItemDeactivatedAfterEdit())
                    {
                        const std::string quotedName = jsonQuote(state.renameBuffer.data());
                        state.renamingGuid = {};
                        (void)shell.run(
                            "entity.rename",
                            text.concat(R"({"entity":")",
                                        text.concat(rowIdText, text.concat(R"(","name":)",
                                                                           text.concat(quotedName, "}")))),
                            "outliner");
                    }
                }
                else
                {
                    const char* displayName =
                        row.name != nullptr ? row.name : text.format(s::kOutlinerUnnamed);
                    const char* label =
                        text.verbatim(text.concat(row.icon.view(), text.concat("  ", displayName)),
                                      indexed(text, "outliner.row.", rowIndex));
                    const bool dimmed = row.disabled || row.hidden || row.name == nullptr;
                    if (dimmed)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              imvec(shell.theme.color(ui::ThemeToken::TextDisabled)));
                    }
                    if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_None,
                                          ImVec2(selectWidth, rowHeight)))
                    {
                        clickedRow = rowIndex;
                        clickedCtrl = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                        clickedShift = ImGui::GetIO().KeyShift;
                    }
                    if (dimmed)
                    {
                        ImGui::PopStyleColor();
                    }

                    const ImVec2 itemMin = ImGui::GetItemRectMin();
                    const ImVec2 itemMax = ImGui::GetItemRectMax();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        contextRowGuid = row.guid;
                        if (!isSelected)
                        {
                            (void)shell.run(
                                "selection.set",
                                text.concat(R"({"entities":[")", text.concat(rowIdText, R"("]})")),
                                "outliner");
                        }
                        ImGui::OpenPopup("###outliner.row_context");
                    }

                    // Drag source: carries the 16-hex EntityGuid.
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
                    {
                        ImGui::SetDragDropPayload(kEntityPayload, rowIdText, kGuidTextLength);
                        ImGui::TextUnformatted(displayName);
                        ImGui::EndDragDropSource();
                    }

                    // Drop target: top 25% inserts before, bottom 25% inserts after, middle 50% reparents.
                    if (ImGui::BeginDragDropTarget())
                    {
                        const f32 mouseY = ImGui::GetMousePos().y;
                        const f32 height = itemMax.y - itemMin.y;
                        const bool before = mouseY < itemMin.y + height * 0.25f;
                        const bool after = mouseY > itemMax.y - height * 0.25f;
                        const ImU32 accent =
                            ImGui::ColorConvertFloat4ToU32(imvec(shell.theme.color(ui::ThemeToken::Accent)));
                        if (before || after)
                        {
                            const f32 lineY = before ? itemMin.y : itemMax.y;
                            drawList->AddLine(ImVec2(itemMin.x, lineY), ImVec2(itemMax.x, lineY), accent,
                                              2.0f * kit.scale);
                        }
                        else
                        {
                            drawList->AddRect(itemMin, itemMax, accent, style.FrameRounding);
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                                kEntityPayload, ImGuiDragDropFlags_AcceptBeforeDelivery))
                        {
                            if (payload->IsDelivery() &&
                                payload->DataSize == static_cast<int>(kGuidTextLength))
                            {
                                const std::string_view droppedId(static_cast<const char*>(payload->Data),
                                                                 kGuidTextLength);
                                if (droppedId != rowIdText)
                                {
                                    const bool keepWorld = !ImGui::GetIO().KeyAlt;
                                    const char* keepStr = keepWorld ? "true" : "false";
                                    if (!before && !after)
                                    {
                                        (void)shell.run(
                                            "hierarchy.set_parent",
                                            text.concat(
                                                R"({"entity":")",
                                                text.concat(
                                                    droppedId,
                                                    text.concat(R"(","parent":")",
                                                                text.concat(rowIdText,
                                                                            text.concat(R"(","keepWorld":)",
                                                                                        text.concat(keepStr,
                                                                                                    "}")))))),
                                            "outliner");
                                    }
                                    else
                                    {
                                        const u32 targetIndex =
                                            before ? row.siblingIndex : row.siblingIndex + 1;
                                        char idxBuf[24];
                                        const auto [idxEnd, _] =
                                            std::to_chars(idxBuf, idxBuf + sizeof(idxBuf), targetIndex);
                                        const std::string_view idxView(idxBuf,
                                                                       static_cast<usize>(idxEnd - idxBuf));
                                        if (row.parentGuid.isNull())
                                        {
                                            (void)shell.run(
                                                "hierarchy.set_parent",
                                                text.concat(
                                                    R"({"entity":")",
                                                    text.concat(
                                                        droppedId,
                                                        text.concat(
                                                            R"(","index":)",
                                                            text.concat(
                                                                idxView,
                                                                text.concat(R"(,"keepWorld":)",
                                                                            text.concat(keepStr, "}")))))),
                                                "outliner");
                                        }
                                        else
                                        {
                                            const auto pGuid = formatGuid(row.parentGuid);
                                            const std::string_view pView(pGuid.data(), pGuid.size());
                                            (void)shell.run(
                                                "hierarchy.set_parent",
                                                text.concat(
                                                    R"({"entity":")",
                                                    text.concat(
                                                        droppedId,
                                                        text.concat(
                                                            R"(","parent":")",
                                                            text.concat(
                                                                pView,
                                                                text.concat(
                                                                    R"(","index":)",
                                                                    text.concat(
                                                                        idxView,
                                                                        text.concat(
                                                                            R"(,"keepWorld":)",
                                                                            text.concat(keepStr, "}")))))))),
                                                "outliner");
                                        }
                                    }
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                // Right-aligned visibility (eye) and lock buttons.
                ImGui::SameLine();
                const ui::Icon eyeIcon = row.hidden ? ui::icons::kVisibilityOff : ui::icons::kVisibility;
                const char* eyeTip = text.format(row.hidden ? s::kOutlinerShow : s::kOutlinerHide);
                if (!row.hidden)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          imvec(shell.theme.color(ui::ThemeToken::TextSecondary)));
                }
                if (ui::iconButton(kit, eyeIcon, indexed(text, "outliner.visible.", rowIndex), eyeTip,
                                   nullptr, row.hidden))
                {
                    const char* nextHidden = row.hidden ? "false" : "true";
                    (void)shell.run(
                        "view.set_entities",
                        text.concat(R"({"entities":[")",
                                    text.concat(rowIdText, text.concat(R"("],"hidden":)",
                                                                       text.concat(nextHidden, "}")))),
                        "outliner");
                }
                if (!row.hidden)
                {
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine(0.0f, 2.0f * kit.scale);
                const ui::Icon lockIcon = row.locked ? ui::icons::kLock : ui::icons::kLockOpen;
                const char* lockTip = text.format(row.locked ? s::kOutlinerUnlock : s::kOutlinerLock);
                if (!row.locked)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          imvec(shell.theme.color(ui::ThemeToken::TextSecondary)));
                }
                if (ui::iconButton(kit, lockIcon, indexed(text, "outliner.lock.", rowIndex), lockTip, nullptr,
                                   row.locked))
                {
                    const char* nextLocked = row.locked ? "false" : "true";
                    (void)shell.run(
                        "view.set_entities",
                        text.concat(R"({"entities":[")",
                                    text.concat(rowIdText, text.concat(R"("],"locked":)",
                                                                       text.concat(nextLocked, "}")))),
                        "outliner");
                }
                if (!row.locked)
                {
                    ImGui::PopStyleColor();
                }

                if (indent > 0.0f)
                {
                    ImGui::Unindent(indent);
                }
                ImGui::PopID();
            }
        }

        // Apply row click selection after the clipper loop.
        if (clickedRow)
        {
            const State::Row& row = state.rows[*clickedRow];
            const char* rowIdText = row.idText.data();
            if (clickedCtrl)
            {
                const char* cmd = selection.contains(row.guid) ? "selection.remove" : "selection.add";
                (void)shell.run(cmd, text.concat(R"({"entities":[")", text.concat(rowIdText, R"("]})")),
                                "outliner");
                state.lastClickedGuid = row.guid;
            }
            else if (clickedShift && !state.lastClickedGuid.isNull())
            {
                std::optional<usize> anchorIdx;
                for (usize idx = 0; idx < state.rows.size(); ++idx)
                {
                    if (state.rows[idx].guid == state.lastClickedGuid)
                    {
                        anchorIdx = idx;
                        break;
                    }
                }
                const usize first = anchorIdx ? std::min(*anchorIdx, *clickedRow) : *clickedRow;
                const usize last = anchorIdx ? std::max(*anchorIdx, *clickedRow) : *clickedRow;
                std::string params = R"({"entities":[)";
                for (usize idx = first; idx <= last; ++idx)
                {
                    if (idx > first)
                    {
                        params += ',';
                    }
                    params += '"';
                    params.append(state.rows[idx].idText.data(), kGuidTextLength);
                    params += '"';
                }
                params += "]}";
                (void)shell.run("selection.set", params, "outliner");
            }
            else
            {
                (void)shell.run("selection.set",
                                text.concat(R"({"entities":[")", text.concat(rowIdText, R"("]})")),
                                "outliner");
                state.lastClickedGuid = row.guid;
            }
        }

        // Empty space below rows: drop target to unparent to root.
        const ImVec2 remain = ImGui::GetContentRegionAvail();
        if (remain.y > 4.0f)
        {
            ImGui::InvisibleButton(text.hidden("outliner.drop_root"),
                                   ImVec2(std::max(1.0f, remain.x), remain.y));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
                {
                    if (payload->DataSize == static_cast<int>(kGuidTextLength))
                    {
                        const std::string_view droppedId(static_cast<const char*>(payload->Data),
                                                         kGuidTextLength);
                        const char* keepStr = !ImGui::GetIO().KeyAlt ? "true" : "false";
                        (void)shell.run(
                            "hierarchy.set_parent",
                            text.concat(R"({"entity":")",
                                        text.concat(droppedId, text.concat(R"(","keepWorld":)",
                                                                           text.concat(keepStr, "}")))),
                            "outliner");
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // Row context menu.
        if (ImGui::BeginPopup("###outliner.row_context"))
        {
            const EntityGuid targetGuid = !contextRowGuid.has_value() ? selection.primary() : *contextRowGuid;
            const Entity targetEntity = editor.entities().lookup(targetGuid);
            const auto targetId = formatGuid(targetGuid);
            const std::string_view targetIdView(targetId.data(), targetId.size());
            const bool isDisabled = !targetEntity.isNull() && editor.world().has<Disabled>(targetEntity);
            const bool hasParent = !targetEntity.isNull() && !parentOf(editor.world(), targetEntity).isNull();

            if (ImGui::MenuItem(text.label(s::kOutlinerCreateEmpty, "outliner.menu.create_empty")))
            {
                (void)shell.run("entity.create", R"({"name":"Entity"})", "outliner");
            }
            if (!targetGuid.isNull() &&
                ImGui::MenuItem(text.label(s::kOutlinerCreateChild, "outliner.menu.create_child")))
            {
                (void)shell.run(
                    "entity.create",
                    text.concat(R"({"name":"Entity","parent":")", text.concat(targetIdView, R"("})")),
                    "outliner");
            }
            ImGui::Separator();
            if (!targetGuid.isNull() &&
                ImGui::MenuItem(text.label(s::kCommandEntityRenameTitle, "outliner.menu.rename"), "F2"))
            {
                beginRename(targetGuid);
            }
            if (ImGui::MenuItem(text.label(s::kCommandEntityDuplicateTitle, "outliner.menu.duplicate"),
                                "Ctrl+D", false, !selection.empty()))
            {
                (void)shell.run("entity.duplicate", R"({"entities":["$selection"]})", "outliner");
            }
            if (ImGui::MenuItem(text.label(s::kCommandClipboardCopyTitle, "outliner.menu.copy"), "Ctrl+C",
                                false, !selection.empty()))
            {
                (void)shell.run("clipboard.copy", R"({"entities":["$selection"]})", "outliner");
            }
            if (ImGui::MenuItem(text.label(s::kCommandClipboardPasteTitle, "outliner.menu.paste"), "Ctrl+V",
                                false, !editor.clipboard().empty()))
            {
                if (!targetGuid.isNull())
                {
                    (void)shell.run("clipboard.paste",
                                    text.concat(R"({"parent":")", text.concat(targetIdView, R"("})")),
                                    "outliner");
                }
                else
                {
                    (void)shell.run("clipboard.paste", "{}", "outliner");
                }
            }
            ImGui::Separator();
            if (!targetGuid.isNull())
            {
                const text::TextKey toggleKey = isDisabled ? s::kActionEntityEnable : s::kActionEntityDisable;
                if (ImGui::MenuItem(text.label(toggleKey, "outliner.menu.toggle_enabled")))
                {
                    const char* enabledStr = isDisabled ? "true" : "false";
                    (void)shell.run(
                        "entity.set_enabled",
                        text.concat(R"({"entity":")",
                                    text.concat(targetIdView, text.concat(R"(","enabled":)",
                                                                          text.concat(enabledStr, "}")))),
                        "outliner");
                }
                if (ImGui::MenuItem(text.label(s::kActionEntityUnparent, "outliner.menu.unparent"), nullptr,
                                    false, hasParent))
                {
                    (void)shell.run("hierarchy.set_parent",
                                    text.concat(R"({"entity":")", text.concat(targetIdView, R"("})")),
                                    "outliner");
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(text.label(s::kCommandEntityDestroyTitle, "outliner.menu.delete"), "Delete",
                                false, !selection.empty()))
            {
                (void)shell.run("entity.destroy", R"({"entities":["$selection"]})", "outliner");
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
}

} // namespace illusion::editor::shell::detail
