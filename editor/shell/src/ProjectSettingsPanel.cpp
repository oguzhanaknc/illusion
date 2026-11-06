// The Project Settings panel: one page per project settings type of the editor's SettingsCatalog (physics,
// audio, input, rendering and the game's own), edited with the Inspector's widgets through
// `project.settings_set`; the physics collision matrix and the audio bus volumes have drawers of their own.

#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr std::string_view kPhysicsType = "illusion.PhysicsSettings";
constexpr std::string_view kAudioType = "illusion.AudioSettings";
constexpr std::string_view kCollisionMatrix = "collisionMatrix";
constexpr std::string_view kLayerNames = "layerNames";
constexpr std::string_view kBusVolumes = "busVolumes";
constexpr u32 kLayers = 16; // kProjectPhysicsLayers
constexpr u32 kBuses = 5;   // kProjectAudioBuses
constexpr usize kLayerNameCapacity = 64;

// Text of the layer name fields; the one being edited keeps what the user typed.
struct LayerNameBuffers
{
    std::array<std::array<char, kLayerNameCapacity>, kLayers> names{};
    i32 editing = -1;
};

// A default value of a reflected struct on the heap.
class OwnedValue : NonCopyable
{
public:
    explicit OwnedValue(const TypeReflection& type)
        : m_type(type)
        , m_data(::operator new(type.size, std::align_val_t(type.alignment)))
    {
        m_type.ops->construct(m_data);
    }
    ~OwnedValue()
    {
        m_type.ops->destroy(m_data);
        ::operator delete(m_data, std::align_val_t(m_type.alignment));
    }
    [[nodiscard]] void* data() noexcept { return m_data; }
    [[nodiscard]] const void* data() const noexcept { return m_data; }

private:
    const TypeReflection& m_type;
    void* m_data;
};

// The field @p name of @p type, or nullptr.
const FieldInfo* findField(const TypeReflection& type, std::string_view name) noexcept
{
    for (const FieldInfo& field : type.fields)
    {
        if (field.name == name)
        {
            return &field;
        }
    }
    return nullptr;
}

// The title of a settings type: its display name, or its name without the prefix.
std::string_view titleOf(const TypeReflection& type) noexcept
{
    if (!type.meta.displayName.empty())
    {
        return type.meta.displayName;
    }
    const usize dot = type.name.rfind('.');
    return dot == std::string_view::npos ? type.name : type.name.substr(dot + 1);
}

} // namespace

struct ProjectSettingsPanel::State
{
    struct Page
    {
        const SettingsType* settings = nullptr;
        std::unique_ptr<OwnedValue> work;
        std::unique_ptr<OwnedValue> defaults;
    };

    std::vector<Page> pages;
    usize selected = 0;
    const Project* project = nullptr;
    usize typeCount = 0;
    // What the selected page was read at: the undo history (undo and redo change settings files).
    usize readPosition = ~usize{0};
    usize readSteps = ~usize{0};
    usize readPage = ~usize{0};
    u64 dragSerial = 1; // one undo step per drag of a bus slider
    LayerNameBuffers layerNames;
    Inspector fields;

    void rebuild(Impl& shell)
    {
        pages.clear();
        for (const SettingsType& type : shell.editor.settings().types())
        {
            if (type.scope == SettingsScope::Project && type.type != nullptr && type.type->size > 0)
            {
                pages.push_back(Page{&type, std::make_unique<OwnedValue>(*type.type),
                                     std::make_unique<OwnedValue>(*type.type)});
            }
        }
        selected = std::min(selected, pages.empty() ? usize{0} : pages.size() - 1);
        readPage = ~usize{0};
    }

    // Reads the selected page from its file (`project.settings_get`) when the history moved.
    void refresh(Impl& shell)
    {
        const UndoStack& history = shell.editor.history();
        if (readPage == selected && readPosition == history.position() && readSteps == history.steps().size())
        {
            return;
        }
        readPage = selected;
        readPosition = history.position();
        readSteps = history.steps().size();
        Page& page = pages[selected];
        const TypeReflection& type = *page.settings->type;
        const std::optional<std::string> result =
            shell.run("project.settings_get", "{\"type\":" + jsonQuote(type.name) + "}", "project_settings");
        if (!result)
        {
            return;
        }
        const Expected<JsonDocument, JsonError> json = JsonDocument::parse(*result);
        if (json)
        {
            page.work = std::make_unique<OwnedValue>(type);
            (void)readValue(type, page.work->data(), json->root().pointer("/settings/0/value"));
        }
    }
};

ProjectSettingsPanel::ProjectSettingsPanel()
    : m_state(std::make_unique<State>())
{
}

ProjectSettingsPanel::~ProjectSettingsPanel() = default;

namespace
{

// The physics layers: a row per named layer (and one more to name) with its editable name and a triangle of
// checkboxes for the layers it collides with.
void drawCollisionMatrix(Impl& shell, const TypeReflection& type, void* work, LayerNameBuffers& buffers)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const FieldInfo* names = findField(type, kLayerNames);
    const FieldInfo* matrix = findField(type, kCollisionMatrix);
    if (names == nullptr || matrix == nullptr)
    {
        return;
    }
    auto* layerNames = reinterpret_cast<std::string*>(static_cast<std::byte*>(work) + names->offset);
    auto* bits = reinterpret_cast<u32*>(static_cast<std::byte*>(work) + matrix->offset);
    // The named layers, the first one, and one more to name.
    u32 count = 1;
    for (u32 i = 0; i < kLayers; ++i)
    {
        if (!layerNames[i].empty())
        {
            count = i + 1;
        }
    }
    count = std::min(count + 1, kLayers);
    ui::sectionHeader(kit, text.format(s::kProjectSettingsCollisionMatrix));
    ImGui::TextDisabled("%s", text.format(s::kProjectSettingsCollisionHint));
    if (!ImGui::BeginTable(text.hidden("project_settings.collision"), static_cast<int>(count + 2),
                           ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
    {
        return;
    }
    char cell[48];
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    for (u32 column = 0; column < count; ++column)
    {
        ImGui::TableNextColumn();
        ImGui::Text("%u", column);
    }
    bool matrixChanged = false;
    bool namesChanged = false;
    for (u32 row = 0; row < count; ++row)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", row);
        ImGui::TableNextColumn();
        std::array<char, kLayerNameCapacity>& buffer = buffers.names[row];
        if (static_cast<i32>(row) != buffers.editing)
        {
            const usize length = std::min(layerNames[row].size(), buffer.size() - 1);
            std::copy_n(layerNames[row].data(), length, buffer.data());
            buffer[length] = '\0';
        }
        std::snprintf(cell, sizeof(cell), "project_settings.layer.%u", row);
        ImGui::SetNextItemWidth(140.0f * kit.scale);
        ImGui::InputText(text.hidden(cell), buffer.data(), buffer.size());
        if (ImGui::IsItemActive())
        {
            buffers.editing = static_cast<i32>(row);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            layerNames[row] = buffer.data();
            namesChanged = true;
        }
        if (ImGui::IsItemDeactivated())
        {
            buffers.editing = -1;
        }
        for (u32 column = 0; column < count; ++column)
        {
            ImGui::TableNextColumn();
            if (column > row)
            {
                continue;
            }
            bool value = (bits[row] & (1u << column)) != 0 && (bits[column] & (1u << row)) != 0;
            std::snprintf(cell, sizeof(cell), "project_settings.collision.%u.%u", row, column);
            if (ImGui::Checkbox(text.hidden(cell), &value))
            {
                bits[row] = value ? bits[row] | (1u << column) : bits[row] & ~(1u << column);
                bits[column] = value ? bits[column] | (1u << row) : bits[column] & ~(1u << row);
                matrixChanged = true;
            }
        }
    }
    ImGui::EndTable();
    if (namesChanged)
    {
        (void)writeSettingsField(shell, type, work, kLayerNames);
    }
    if (matrixChanged)
    {
        (void)writeSettingsField(shell, type, work, kCollisionMatrix);
    }
}

// The audio bus volumes with the names of the buses.
void drawBusVolumes(Impl& shell, const TypeReflection& type, void* work, u64 serial)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const FieldInfo* field = findField(type, kBusVolumes);
    if (field == nullptr)
    {
        return;
    }
    auto* volumes = reinterpret_cast<f32*>(static_cast<std::byte*>(work) + field->offset);
    const std::array<text::TextKey, kBuses> names = {
        s::kProjectSettingsBusSfx, s::kProjectSettingsBusMusic, s::kProjectSettingsBusAmbience,
        s::kProjectSettingsBusDialogue, s::kProjectSettingsBusUi};
    ui::sectionHeader(kit, text.format(s::kProjectSettingsBusVolumes));
    char id[48];
    char key[64];
    for (u32 bus = 0; bus < kBuses; ++bus)
    {
        std::snprintf(id, sizeof(id), "project_settings.bus.%u", bus);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat(text.label(names[bus], id), &volumes[bus], 0.0f, 2.0f, "%.2f"))
        {
            // One undo step per drag of a slider.
            std::snprintf(key, sizeof(key), "project_settings.bus.%llu.%u",
                          static_cast<unsigned long long>(serial), bus);
            (void)writeSettingsField(shell, type, work, kBusVolumes, key);
        }
    }
}

} // namespace

void ProjectSettingsPanel::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    State& state = *m_state;
    const Project* project = shell.editor.project();
    if (project == nullptr)
    {
        ui::emptyState(kit, ui::icons::kSettingsApplications, text.format(s::kProjectSettingsNone),
                       text.format(s::kProjectSettingsNoneHint));
        return;
    }
    if (project != state.project || shell.editor.settings().types().size() != state.typeCount)
    {
        state.project = project;
        state.typeCount = shell.editor.settings().types().size();
        state.rebuild(shell);
    }
    if (state.pages.empty())
    {
        return;
    }
    if (!ImGui::BeginTable(text.hidden("project_settings.layout"), 2,
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
    {
        return;
    }
    ImGui::TableSetupColumn(text.hidden("project_settings.pages"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(text.hidden("project_settings.page"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    for (usize i = 0; i < state.pages.size(); ++i)
    {
        const TypeReflection& type = *state.pages[i].settings->type;
        if (ImGui::Selectable(text.verbatim(titleOf(type), text.concat("project_settings.page.", type.name)),
                              i == state.selected))
        {
            state.selected = i;
        }
    }
    ImGui::TableNextColumn();
    state.refresh(shell);
    State::Page& page = state.pages[state.selected];
    const TypeReflection& type = *page.settings->type;
    if (ui::iconButton(kit, ui::icons::kRestartAlt, "project_settings.reset",
                       text.format(s::kProjectSettingsReset)))
    {
        (void)shell.run("project.settings_set", "{\"type\":" + jsonQuote(type.name) + ",\"value\":null}",
                        "project_settings");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(text.copy(titleOf(type)));
    ImGui::TextDisabled(
        "%s", text.format(s::kProjectSettingsFile, {{"file", std::string_view(page.settings->file)}}));
    ImGui::Separator();
    if (type.name == kPhysicsType)
    {
        constexpr std::array<std::string_view, 2> kSkip{kLayerNames, kCollisionMatrix};
        state.fields.drawSettings(shell, type, page.work->data(), page.defaults->data(), kSkip);
        drawCollisionMatrix(shell, type, page.work->data(), state.layerNames);
    }
    else if (type.name == kAudioType)
    {
        constexpr std::array<std::string_view, 1> kSkip{kBusVolumes};
        state.fields.drawSettings(shell, type, page.work->data(), page.defaults->data(), kSkip);
        drawBusVolumes(shell, type, page.work->data(), state.dragSerial);
    }
    else
    {
        state.fields.drawSettings(shell, type, page.work->data(), page.defaults->data());
    }
    ImGui::EndTable();
    if (!ImGui::IsAnyItemActive())
    {
        ++state.dragSerial;
    }
}

} // namespace illusion::editor::shell::detail
