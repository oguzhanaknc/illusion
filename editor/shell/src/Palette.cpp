// The command palette: commands and UI actions, entities ("@", Go to Anything) and preference pages (">"),
// fuzzy-matched on the localized and the English titles, with a form for commands that need params.

#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/text/FuzzyMatcher.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui_internal.h>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

// Keys the palette reads while its text field is active (which owns them).
bool pressed(ImGuiKey key, bool repeat = false) noexcept
{
    return ImGui::IsKeyPressed(key, repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None,
                               ImGuiKeyOwner_Any);
}

constexpr usize kMaxResults = 200;
constexpr f32 kWidth = 640.0f;

enum class Mode : u8
{
    Commands,
    Entities,
    Settings,
};

enum class Kind : u8
{
    Action,  // a UI action (index into uiActions())
    Command, // a palette command no action runs (index into the registry's sorted commands)
    Entity,  // an entity of the scene (index into the entity cache)
    Page,    // a preferences page
};

struct Result
{
    Kind kind = Kind::Action;
    usize index = 0;
    i32 score = 0;
};

// One field of a command's params form.
struct FormField
{
    std::string name;
    FieldType type = FieldType::String;
    bool optional = false;
    std::span<const EnumValue> enumValues;
    std::array<char, 512> text{}; // strings, numbers and JSON values as typed
    bool flag = false;
    int choice = 0;
    bool set = false; // an optional field was given a value
};

FieldType valueType(const ValueType& type, bool& optional) noexcept
{
    optional = type.type == FieldType::Optional;
    return optional && type.element != nullptr ? type.element->type : type.type;
}

std::span<const EnumValue> enumValuesOf(const FieldInfo& field) noexcept
{
    return field.type == FieldType::Optional && field.element != nullptr ? field.element->enumValues
                                                                         : field.enumValues;
}

bool isNumber(FieldType type) noexcept
{
    switch (type)
    {
    case FieldType::I8:
    case FieldType::I16:
    case FieldType::I32:
    case FieldType::I64:
    case FieldType::U8:
    case FieldType::U16:
    case FieldType::U32:
    case FieldType::U64:
    case FieldType::F32:
    case FieldType::F64:
        return true;
    default:
        return false;
    }
}

void copyText(std::array<char, 512>& out, std::string_view text) noexcept
{
    const usize length = std::min(text.size(), out.size() - 1);
    std::memcpy(out.data(), text.data(), length);
    out[length] = '\0';
}

} // namespace

struct Palette::State
{
    Mode mode = Mode::Commands;
    std::array<char, 256> query{};
    std::string lastQuery;
    Mode lastMode = Mode::Commands;
    u64 lastRevision = ~u64{0};
    u64 lastCommands = ~u64{0};
    bool focusInput = false;
    bool scrollToSelected = false;
    usize selected = 0;
    std::vector<Result> results;
    std::vector<EntitySummary> entities; // cached per scene revision
    u64 entitiesRevision = ~u64{0};

    // The params form.
    bool form = false;
    std::string formCommand;
    std::string formOrigin;
    std::vector<FormField> fields;

    State() { results.reserve(kMaxResults); }

    void refresh(Impl& shell);
    void runSelected(Impl& shell, Palette& palette);
    void drawResults(Impl& shell, Palette& palette, ui::UiKit& kit);
    void drawForm(Impl& shell, Palette& palette, ui::UiKit& kit);
};

void Palette::State::refresh(Impl& shell)
{
    // "@" lists entities and ">" preference pages, whatever the mode.
    std::string_view typed(query.data());
    Mode effective = mode;
    if (typed.starts_with('@'))
    {
        effective = Mode::Entities;
        typed.remove_prefix(1);
    }
    else if (typed.starts_with('>'))
    {
        effective = Mode::Settings;
        typed.remove_prefix(1);
    }
    EditorContext& editor = shell.editor;
    if (typed == lastQuery && effective == lastMode && editor.revision() == lastRevision &&
        editor.commands().revision() == lastCommands)
    {
        return;
    }
    lastQuery = std::string(typed);
    lastMode = effective;
    lastRevision = editor.revision();
    lastCommands = editor.commands().revision();
    results.clear();
    selected = 0;
    text::FuzzyMatcher matcher(typed);
    ui::FrameText& text = shell.text;
    const auto score = [&](std::string_view localized, std::string_view english) -> std::optional<i32> {
        if (typed.empty())
        {
            return 0;
        }
        const std::optional<i32> a = matcher.score(localized);
        const std::optional<i32> b = matcher.score(english);
        if (!a && !b)
        {
            return std::nullopt;
        }
        return std::max(a.value_or(b.value_or(0)), b.value_or(a.value_or(0)));
    };
    if (effective == Mode::Commands)
    {
        const std::span<const UiAction> actions = uiActions();
        for (usize i = 0; i < actions.size(); ++i)
        {
            const text::TextKey title = shell.actionTitle(i);
            if (const std::optional<i32> value = score(text.format(title), title.english()))
            {
                results.push_back(Result{Kind::Action, i, *value});
            }
        }
        const std::vector<const CommandDescriptor*>& commands = editor.commands().commands();
        for (usize i = 0; i < commands.size(); ++i)
        {
            const CommandInfo& info = commands[i]->info;
            const bool hasAction = std::any_of(actions.begin(), actions.end(), [&](const UiAction& action) {
                return action.command == info.name;
            });
            if (!info.palette || hasAction)
            {
                continue;
            }
            if (const std::optional<i32> value = score(text.format(info.title), info.title.english()))
            {
                results.push_back(Result{Kind::Command, i, *value});
            }
        }
    }
    else if (effective == Mode::Entities)
    {
        if (entitiesRevision != editor.revision())
        {
            entitiesRevision = editor.revision();
            SceneQueryParams params;
            params.limit = 10000;
            CommandOptions options;
            options.origin = "palette";
            Expected<SceneQueryResult, CommandError> found =
                editor.execute<SceneQueryResult>("scene.query", params, options);
            entities = found ? std::move(found->entities) : std::vector<EntitySummary>();
        }
        for (usize i = 0; i < entities.size(); ++i)
        {
            if (const std::optional<i32> value = score(entities[i].path, entities[i].name))
            {
                results.push_back(Result{Kind::Entity, i, *value});
            }
        }
    }
    else
    {
        for (usize i = 0; i < static_cast<usize>(PreferencesTab::Count); ++i)
        {
            const text::TextKey title = preferencesTabTitle(static_cast<PreferencesTab>(i));
            if (const std::optional<i32> value = score(text.format(title), title.english()))
            {
                results.push_back(Result{Kind::Page, i, *value});
            }
        }
    }
    std::stable_sort(results.begin(), results.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });
    if (results.size() > kMaxResults)
    {
        results.resize(kMaxResults);
    }
}

void Palette::State::runSelected(Impl& shell, Palette& palette)
{
    if (selected >= results.size())
    {
        return;
    }
    const Result result = results[selected];
    switch (result.kind)
    {
    case Kind::Action:
        if (!shell.actionEnabled(result.index))
        {
            return;
        }
        palette.close();
        shell.runAction(result.index, "palette");
        break;
    case Kind::Command:
    {
        const CommandDescriptor* command = shell.editor.commands().commands()[result.index];
        palette.close();
        palette.openForm(shell, command->info.name, {}, "palette");
        break;
    }
    case Kind::Entity:
        palette.close();
        (void)shell.run("selection.set", "{\"entities\":[" + jsonQuote(entities[result.index].id) + "]}",
                        "palette");
        break;
    case Kind::Page:
        palette.close();
        shell.preferences->select(static_cast<PreferencesTab>(result.index));
        (void)shell.run("ui.show_panel", R"({"panel":"window.preferences"})", "palette");
        break;
    }
}

void Palette::State::drawResults(Impl& shell, Palette& palette, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    if (focusInput)
    {
        ImGui::SetKeyboardFocusHere();
        focusInput = false;
    }
    const text::TextKey hint = mode == Mode::Entities   ? s::kPaletteHintEntities
                               : mode == Mode::Settings ? s::kPaletteHintSettings
                                                        : s::kPaletteHintCommands;
    ImGui::SetNextItemWidth(-1.0f);
    ui::searchField(kit, "palette.query", query, text.format(hint));
    refresh(shell);
    if (pressed(ImGuiKey_DownArrow, true) && selected + 1 < results.size())
    {
        ++selected;
        scrollToSelected = true;
    }
    if (pressed(ImGuiKey_UpArrow, true) && selected > 0)
    {
        --selected;
        scrollToSelected = true;
    }
    const bool enter = pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter);
    if (results.empty())
    {
        ImGui::TextDisabled("%s", text.format(s::kPaletteEmpty));
    }
    const f32 rows = std::min<f32>(static_cast<f32>(results.size()), 12.0f);
    if (ImGui::BeginChild(text.hidden("palette.results"),
                          ImVec2(0.0f, rows * ImGui::GetFrameHeightWithSpacing())))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(results.size()));
        if (scrollToSelected)
        {
            clipper.IncludeItemByIndex(static_cast<int>(selected));
        }
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const usize i = static_cast<usize>(row);
                const Result& result = results[i];
                char number[24];
                std::snprintf(number, sizeof(number), "%zu", i);
                const char* id = text.concat("palette.item.", number);
                const char* label = nullptr;
                const char* detailText = nullptr;
                bool enabled = true;
                switch (result.kind)
                {
                case Kind::Action:
                {
                    const UiAction& action = uiActions()[result.index];
                    label =
                        text.iconLabel(shell.actionIcon(result.index), shell.actionTitle(result.index), id);
                    enabled = shell.actionEnabled(result.index);
                    detailText = enabled
                                     ? text.copy(shell.keymap.text(result.index))
                                     : text.format(s::kPaletteDisabled, {{"milestone", action.milestone}});
                    break;
                }
                case Kind::Command:
                {
                    const CommandInfo& info = shell.editor.commands().commands()[result.index]->info;
                    label =
                        text.iconLabel(ui::findIcon(info.icon).value_or(ui::icons::kCircle), info.title, id);
                    detailText = text.copy(info.name);
                    break;
                }
                case Kind::Entity:
                    label = text.verbatim(entities[result.index].path, id);
                    break;
                case Kind::Page:
                    label =
                        text.iconLabel(ui::icons::kSettings,
                                       preferencesTabTitle(static_cast<PreferencesTab>(result.index)), id);
                    break;
                }
                ImGui::BeginDisabled(!enabled);
                if (ImGui::Selectable(label, i == selected))
                {
                    selected = i;
                    ImGui::EndDisabled();
                    runSelected(shell, palette);
                    ImGui::EndChild();
                    return;
                }
                ImGui::EndDisabled();
                if (i == selected && scrollToSelected)
                {
                    ImGui::SetScrollHereY();
                    scrollToSelected = false;
                }
                if (detailText != nullptr && detailText[0] != '\0')
                {
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(
                        ImGui::GetCursorPosX() +
                        std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(detailText).x));
                    ImGui::TextDisabled("%s", detailText);
                }
            }
        }
    }
    ImGui::EndChild();
    if (enter)
    {
        runSelected(shell, palette);
    }
}

void Palette::State::drawForm(Impl& shell, Palette& palette, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const CommandDescriptor* command = shell.editor.commands().find(formCommand);
    if (command == nullptr)
    {
        palette.close();
        return;
    }
    {
        const ScopedFont font(shell.options.fonts, ui::FontRole::Strong);
        ImGui::TextUnformatted(text.format(command->info.title));
    }
    ImGui::TextDisabled("%s", text.format(command->info.description));
    ImGui::Separator();
    if (ui::beginPropertyTable(kit, "palette.form"))
    {
        for (usize i = 0; i < fields.size(); ++i)
        {
            FormField& field = fields[i];
            ui::propertyLabel(kit, text.copy(field.name));
            const char* id = text.concat("palette.form.", field.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (focusInput && i == 0)
            {
                ImGui::SetKeyboardFocusHere();
                focusInput = false;
            }
            if (field.type == FieldType::Bool)
            {
                field.set |= ImGui::Checkbox(text.hidden(id), &field.flag);
            }
            else if (field.type == FieldType::Enum && !field.enumValues.empty())
            {
                if (ImGui::BeginCombo(text.hidden(id),
                                      text.copy(field.enumValues[static_cast<usize>(field.choice)].name)))
                {
                    for (usize v = 0; v < field.enumValues.size(); ++v)
                    {
                        char number[24];
                        std::snprintf(number, sizeof(number), ".%zu", v);
                        if (ImGui::Selectable(
                                text.verbatim(field.enumValues[v].name, text.concat(id, number)),
                                static_cast<usize>(field.choice) == v))
                        {
                            field.choice = static_cast<int>(v);
                            field.set = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                const char* hint = field.type == FieldType::String || isNumber(field.type)
                                       ? ""
                                       : text.format(s::kPaletteJson);
                ImGui::InputTextWithHint(text.hidden(id), hint, field.text.data(), field.text.size());
            }
        }
        ui::endPropertyTable();
    }
    const bool enter = pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter);
    if (ui::primaryButton(kit, text.label(s::kPaletteRun, "palette.form.run")) || enter)
    {
        // Build the params: strings quoted, numbers and JSON values as typed, empty optional fields left out.
        std::string params = "{";
        for (const FormField& field : fields)
        {
            std::string value;
            const std::string_view typed(field.text.data());
            if (field.type == FieldType::Bool)
            {
                if (field.optional && !field.set)
                {
                    continue;
                }
                value = field.flag ? "true" : "false";
            }
            else if (field.type == FieldType::Enum && !field.enumValues.empty())
            {
                if (field.optional && !field.set)
                {
                    continue;
                }
                value = jsonQuote(field.enumValues[static_cast<usize>(field.choice)].name);
            }
            else if (typed.empty() && field.optional)
            {
                continue;
            }
            else
            {
                value = field.type == FieldType::String ? jsonQuote(typed) : std::string(typed);
            }
            params += params.size() > 1 ? "," : "";
            params += jsonQuote(field.name) + ":" + value;
        }
        params += "}";
        palette.close();
        (void)shell.run(formCommand, params, formOrigin);
    }
}

Palette::Palette()
    : m_state(std::make_unique<State>())
{
}

Palette::~Palette() = default;

void Palette::open(std::string_view mode, std::string_view query)
{
    State& state = *m_state;
    state.mode = mode == "entities" ? Mode::Entities : mode == "settings" ? Mode::Settings : Mode::Commands;
    const usize length = std::min(query.size(), state.query.size() - 1);
    std::memcpy(state.query.data(), query.data(), length);
    state.query[length] = '\0';
    state.lastRevision = ~u64{0};
    state.form = false;
    state.focusInput = true;
    m_open = true;
}

void Palette::openForm(Impl& shell, std::string_view command, std::string_view params,
                       std::string_view origin)
{
    State& state = *m_state;
    const CommandDescriptor* descriptor = shell.editor.commands().find(command);
    if (descriptor == nullptr)
    {
        return;
    }
    state.form = true;
    state.formCommand = std::string(command);
    state.formOrigin = std::string(origin);
    state.fields.clear();
    Expected<JsonDocument, JsonError> given = JsonDocument::parse(params.empty() ? "{}" : params);
    if (descriptor->params != nullptr)
    {
        for (const FieldInfo& info : descriptor->params->fields)
        {
            FormField& field = state.fields.emplace_back();
            field.name = std::string(info.name);
            field.type = valueType(info, field.optional);
            field.enumValues = enumValuesOf(info);
            const JsonValue value = given ? given->root().find(info.name) : JsonValue();
            if (value.isAbsent())
            {
                continue;
            }
            field.set = true;
            if (const std::optional<bool> flag = value.asBool())
            {
                field.flag = *flag;
            }
            else if (const std::optional<std::string_view> text = value.asString();
                     text && field.type != FieldType::Enum)
            {
                copyText(field.text, *text);
            }
            else if (text && field.type == FieldType::Enum)
            {
                for (usize v = 0; v < field.enumValues.size(); ++v)
                {
                    if (field.enumValues[v].name == *text)
                    {
                        field.choice = static_cast<int>(v);
                    }
                }
            }
            else
            {
                std::string json;
                if (value.write(json, JsonWriteOptions{.pretty = false}))
                {
                    copyText(field.text, json);
                }
            }
        }
    }
    state.focusInput = true;
    m_open = true;
}

void Palette::draw(Impl& shell)
{
    if (!m_open)
    {
        return;
    }
    State& state = *m_state;
    ui::UiKit kit = shell.kit();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const f32 width = std::min(kWidth * shell.scale, viewport->WorkSize.x - 32.0f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f,
                                   viewport->WorkPos.y + 48.0f * shell.scale));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    if (state.focusInput)
    {
        ImGui::SetNextWindowFocus();
    }
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
    const bool opening = state.focusInput;
    if (ImGui::Begin("###palette", nullptr, kFlags))
    {
        if (state.form)
        {
            state.drawForm(shell, *this, kit);
        }
        else
        {
            state.drawResults(shell, *this, kit);
        }
        if (pressed(ImGuiKey_Escape) ||
            (!opening && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
        {
            close();
        }
    }
    ImGui::End();
}

} // namespace illusion::editor::shell::detail
