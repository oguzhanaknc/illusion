#include "CommandSupport.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/editor/core/Preferences.hpp>

namespace illusion::editor::detail
{
namespace
{

constexpr usize kMaxLayoutName = 64;
constexpr usize kMaxNoticeBytes = 256;
constexpr u32 kMinScreenshot = 64;
constexpr u32 kMaxScreenshotWidth = 7680;
constexpr u32 kMaxScreenshotHeight = 4320;

Expected<UiHost*, CommandError> requireUi(CommandContext& context)
{
    UiHost* ui = context.editor().ui();
    if (ui == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported,
                                             "this editor has no user interface (it runs with --no-gpu)"));
    }
    return ui;
}

Expected<void, CommandError> checkLayoutName(std::string_view name)
{
    const bool valid =
        !name.empty() && name.size() <= kMaxLayoutName && std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                   c == '_';
        });
    if (!valid)
    {
        return unexpected(CommandError::invalid("/name", "must be 1 to 64 letters, digits, '-' or '_'"));
    }
    return {};
}

std::string encodeBase64(std::string_view bytes)
{
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (usize i = 0; i < bytes.size(); i += 3)
    {
        const usize left = bytes.size() - i;
        const u32 a = static_cast<u8>(bytes[i]);
        const u32 b = left > 1 ? static_cast<u8>(bytes[i + 1]) : 0u;
        const u32 c = left > 2 ? static_cast<u8>(bytes[i + 2]) : 0u;
        const u32 triple = (a << 16) | (b << 8) | c;
        out += kAlphabet[(triple >> 18) & 63];
        out += kAlphabet[(triple >> 12) & 63];
        out += left > 1 ? kAlphabet[(triple >> 6) & 63] : '=';
        out += left > 2 ? kAlphabet[triple & 63] : '=';
    }
    return out;
}

Expected<UiStateResult, CommandError> uiState(CommandContext& context, const NoParams&)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    UiStateResult result;
    for (UiPanelInfo& panel : (*ui)->panels())
    {
        result.panels.push_back(
            UiPanelEntry{std::move(panel.id), std::move(panel.title), panel.open, panel.docked});
    }
    for (UiLayoutInfo& layout : (*ui)->layouts())
    {
        result.layouts.push_back(UiLayoutEntry{std::move(layout.name), layout.builtIn, layout.active});
    }
    return result;
}

Expected<NoResult, CommandError> uiShowPanel(CommandContext& context, const UiShowPanelParams& params)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    const std::vector<UiPanelInfo> panels = (*ui)->panels();
    if (std::none_of(panels.begin(), panels.end(),
                     [&](const UiPanelInfo& panel) { return panel.id == params.panel; }))
    {
        std::vector<std::string_view> ids;
        for (const UiPanelInfo& panel : panels)
        {
            ids.push_back(panel.id);
        }
        CommandError error =
            CommandError::make(ErrorKind::NotFound, "there is no panel '" + params.panel + "'");
        error.errors.push_back(ValueError{"/panel", "is not a panel id"});
        error.suggestions = closestMatches(params.panel, ids);
        return unexpected(std::move(error));
    }
    if (context.dryRun())
    {
        return NoResult{};
    }
    if (Expected<void, CommandError> shown = (*ui)->showPanel(params.panel, params.open.value_or(true));
        !shown)
    {
        return unexpected(std::move(shown).error());
    }
    return NoResult{};
}

Expected<NoResult, CommandError> findLayout(UiHost& ui, std::string_view name, bool userOnly)
{
    const std::vector<UiLayoutInfo> layouts = ui.layouts();
    const auto layout = std::find_if(layouts.begin(), layouts.end(),
                                     [&](const UiLayoutInfo& info) { return info.name == name; });
    if (layout == layouts.end())
    {
        std::vector<std::string_view> names;
        for (const UiLayoutInfo& info : layouts)
        {
            names.push_back(info.name);
        }
        CommandError error =
            CommandError::make(ErrorKind::NotFound, "there is no layout '" + std::string(name) + "'");
        error.errors.push_back(ValueError{"/name", "is not a layout"});
        error.suggestions = closestMatches(name, names);
        return unexpected(std::move(error));
    }
    if (userOnly && layout->builtIn)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the layout '" + std::string(name) + "' is built in"));
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiLayoutApply(CommandContext& context, const UiLayoutParams& params)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (Expected<NoResult, CommandError> found = findLayout(**ui, params.name, false); !found)
    {
        return found;
    }
    if (context.dryRun())
    {
        return NoResult{};
    }
    if (Expected<void, CommandError> applied = (*ui)->applyLayout(params.name); !applied)
    {
        return unexpected(std::move(applied).error());
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiLayoutSave(CommandContext& context, const UiLayoutParams& params)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (Expected<void, CommandError> valid = checkLayoutName(params.name); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    const std::vector<UiLayoutInfo> layouts = (*ui)->layouts();
    if (std::any_of(layouts.begin(), layouts.end(),
                    [&](const UiLayoutInfo& info) { return info.builtIn && info.name == params.name; }))
    {
        return unexpected(CommandError::make(
            ErrorKind::AlreadyExists, "'" + params.name + "' is a built-in layout; choose another name"));
    }
    if (context.dryRun())
    {
        return NoResult{};
    }
    if (Expected<void, CommandError> saved = (*ui)->saveLayout(params.name); !saved)
    {
        return unexpected(std::move(saved).error());
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiLayoutDelete(CommandContext& context, const UiLayoutParams& params)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (Expected<NoResult, CommandError> found = findLayout(**ui, params.name, true); !found)
    {
        return found;
    }
    if (context.dryRun())
    {
        return NoResult{};
    }
    if (Expected<void, CommandError> deleted = (*ui)->deleteLayout(params.name); !deleted)
    {
        return unexpected(std::move(deleted).error());
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiLayoutReset(CommandContext& context, const NoParams&)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (!context.dryRun())
    {
        (*ui)->resetLayout();
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiPalette(CommandContext& context, const UiPaletteParams& params)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (params.mode && static_cast<usize>(*params.mode) > static_cast<usize>(PaletteMode::Settings))
    {
        return unexpected(
            CommandError::invalid("/mode", "must be \"commands\", \"entities\" or \"settings\""));
    }
    if (params.query && params.query->size() > 256)
    {
        return unexpected(CommandError::invalid("/query", "must be at most 256 bytes"));
    }
    if (context.dryRun())
    {
        return NoResult{};
    }
    constexpr std::string_view kModes[] = {"commands", "entities", "settings"};
    const std::string_view mode = kModes[static_cast<usize>(params.mode.value_or(PaletteMode::Commands))];
    if (Expected<void, CommandError> opened = (*ui)->openPalette(mode, params.query.value_or("")); !opened)
    {
        return unexpected(std::move(opened).error());
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiNotify(CommandContext& context, const UiNotifyParams& params)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (params.message.empty() || params.message.size() > kMaxNoticeBytes)
    {
        return unexpected(CommandError::invalid("/message", "must be 1 to 256 bytes"));
    }
    if (params.level && static_cast<usize>(*params.level) > static_cast<usize>(UiNotice::Error))
    {
        return unexpected(
            CommandError::invalid("/level", "must be \"info\", \"success\", \"warning\" or \"error\""));
    }
    if (!context.dryRun())
    {
        (*ui)->notify(params.level.value_or(UiNotice::Info), params.message);
    }
    return NoResult{};
}

Expected<NoResult, CommandError> uiQuit(CommandContext& context, const NoParams&)
{
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    if (!context.dryRun())
    {
        (*ui)->requestQuit();
    }
    return NoResult{};
}

// Waits for the UI to capture a frame.
class ScreenshotTask final : public CommandTask<ScreenshotResult>
{
public:
    ScreenshotTask(UiHost& ui, u64 ticket) noexcept
        : m_ui(&ui)
        , m_ticket(ticket)
    {
    }

    Expected<std::optional<ScreenshotResult>, CommandError> update(CommandContext& context,
                                                                   CommandProgress& progress) override
    {
        // The UI may have gone (the window closed) while the capture was pending.
        if (context.editor().ui() != m_ui)
        {
            return unexpected(CommandError::make(ErrorKind::Unsupported, "the user interface closed"));
        }
        std::optional<Expected<UiScreenshot, CommandError>> done = m_ui->screenshot(m_ticket);
        if (!done)
        {
            progress.message = "waiting for the next frame";
            return std::optional<ScreenshotResult>{};
        }
        if (!*done)
        {
            return unexpected(std::move(*done).error());
        }
        UiScreenshot& shot = done->value();
        ScreenshotResult result;
        result.width = shot.size.x;
        result.height = shot.size.y;
        result._images.push_back(ResultImage{"image/png", encodeBase64(shot.png)});
        return std::optional<ScreenshotResult>(std::move(result));
    }

private:
    UiHost* m_ui;
    u64 m_ticket;
};

Expected<std::unique_ptr<CommandTask<ScreenshotResult>>, CommandError>
screenshot(CommandContext& context, const ScreenshotParams& params)
{
    const auto checkSize = [](std::optional<u32> value, u32 max,
                              std::string pointer) -> Expected<void, CommandError> {
        if (value && (*value < kMinScreenshot || *value > max))
        {
            return unexpected(CommandError::invalid(std::move(pointer), "must be between 64 and " +
                                                                            std::to_string(max) + " pixels"));
        }
        return {};
    };
    if (Expected<void, CommandError> width = checkSize(params.width, kMaxScreenshotWidth, "/width"); !width)
    {
        return unexpected(std::move(width).error());
    }
    if (Expected<void, CommandError> height = checkSize(params.height, kMaxScreenshotHeight, "/height");
        !height)
    {
        return unexpected(std::move(height).error());
    }
    if (params.width.has_value() != params.height.has_value())
    {
        return unexpected(CommandError::invalid(params.width ? "/height" : "/width",
                                                "must be given together with the other dimension"));
    }
    Expected<UiHost*, CommandError> ui = requireUi(context);
    if (!ui)
    {
        return unexpected(std::move(ui).error());
    }
    Expected<u64, CommandError> ticket =
        (*ui)->requestScreenshot(UVec2(params.width.value_or(0), params.height.value_or(0)));
    if (!ticket)
    {
        return unexpected(std::move(ticket).error());
    }
    return std::unique_ptr<CommandTask<ScreenshotResult>>(std::make_unique<ScreenshotTask>(**ui, *ticket));
}

Expected<PreferencesResult, CommandError> preferencesResult(const Preferences& preferences)
{
    Expected<std::string, CommandError> text = writeReflected(preferencesReflection(), &preferences);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    return PreferencesResult{JsonText{std::move(text).value()}};
}

Expected<PreferencesResult, CommandError> preferencesGet(CommandContext& context, const NoParams&)
{
    return preferencesResult(context.editor().preferences());
}

Expected<PreferencesResult, CommandError> preferencesSet(CommandContext& context,
                                                         const PreferencesSetParams& params)
{
    Expected<JsonDocument, CommandError> patch = parseParam(params.value, "/value");
    if (!patch)
    {
        return unexpected(std::move(patch).error());
    }
    if (!patch->root().isObject())
    {
        return unexpected(CommandError::invalid("/value", "must be an object of preference values"));
    }
    const StructReflection* type = &preferencesReflection();
    Preferences next = params.replace.value_or(false) ? Preferences{} : context.editor().preferences();
    if (params.replace.value_or(false))
    {
        // Recent projects are history, not a setting: replacing the preferences keeps them.
        next.recentProjects = context.editor().preferences().recentProjects;
    }
    ValueReadOptions options;
    options.mode = JsonReadMode::Merge;
    const ValueReadResult read = readValue(*type, &next, patch->root(), options);
    if (!read.ok())
    {
        return unexpected(
            CommandError::invalid("/value" + read.errors.front().pointer, read.errors.front().message));
    }
    if (!read.unknownKeys.empty())
    {
        CommandError error =
            CommandError::invalid("/value" + read.unknownKeys.front(), "is not a preference");
        std::vector<std::string_view> fields;
        for (const FieldInfo& field : type->fields)
        {
            fields.push_back(field.name);
        }
        error.suggestions = closestMatches(std::string_view(read.unknownKeys.front()).substr(1), fields);
        return unexpected(std::move(error));
    }
    if (Expected<void, std::string> valid = validatePreferences(next); !valid)
    {
        const usize space = valid.error().find(' ');
        return unexpected(CommandError::invalid("/value" + valid.error().substr(0, space),
                                                valid.error().substr(space + 1)));
    }
    if (!context.dryRun())
    {
        if (Expected<void, CommandError> set = context.editor().setPreferences(next); !set)
        {
            return unexpected(std::move(set).error());
        }
    }
    return preferencesResult(next);
}

} // namespace

void registerUiCommands(CommandRegistry& registry)
{
    registry.add<&uiState>({
        .name = "ui.state",
        .title = strings::kCommandUiStateTitle,
        .description = strings::kCommandUiStateDescription,
        .agentSummary = "Lists the editor window's panels (id, title, open) and layouts (name, built in, "
                        "active). Example: {}.",
        .category = "UI",
        .icon = "web",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&uiShowPanel>({
        .name = "ui.show_panel",
        .title = strings::kCommandUiShowPanelTitle,
        .description = strings::kCommandUiShowPanelDescription,
        .agentSummary = "Shows and focuses a panel or window of the editor, or hides it with open false. Ids "
                        "come from ui.state, e.g. panel.console, panel.ai_activity, window.preferences. "
                        "Example: {\"panel\": \"panel.console\"}.",
        .category = "UI",
        .icon = "web_asset",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&uiLayoutApply>({
        .name = "ui.layout_apply",
        .title = strings::kCommandUiLayoutApplyTitle,
        .description = strings::kCommandUiLayoutApplyDescription,
        .agentSummary = "Arranges the panels as a built-in layout (default, level-design, debug, profiling, "
                        "agent) or one saved with ui.layout_save. Example: {\"name\": \"agent\"}.",
        .category = "UI",
        .icon = "dashboard",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&uiLayoutSave>({
        .name = "ui.layout_save",
        .title = strings::kCommandUiLayoutSaveTitle,
        .description = strings::kCommandUiLayoutSaveDescription,
        .agentSummary = "Saves the current arrangement of the panels for this user under a name, replacing a "
                        "saved layout of that name. Example: {\"name\": \"my-layout\"}.",
        .category = "UI",
        .icon = "save",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::System,
        .palette = true,
    });
    registry.add<&uiLayoutDelete>({
        .name = "ui.layout_delete",
        .title = strings::kCommandUiLayoutDeleteTitle,
        .description = strings::kCommandUiLayoutDeleteDescription,
        .agentSummary = "Deletes a layout saved with ui.layout_save (built-in layouts stay). Example: "
                        "{\"name\": \"my-layout\"}.",
        .category = "UI",
        .icon = "delete",
        .flags = {.destructive = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::System,
        .palette = true,
    });
    registry.add<&uiLayoutReset>({
        .name = "ui.layout_reset",
        .title = strings::kCommandUiLayoutResetTitle,
        .description = strings::kCommandUiLayoutResetDescription,
        .agentSummary = "Restores the built-in arrangement of the active layout (the default layout when a "
                        "saved one is active). Example: {}.",
        .category = "UI",
        .icon = "restart_alt",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&uiPalette>({
        .name = "ui.palette",
        .title = strings::kCommandUiPaletteTitle,
        .description = strings::kCommandUiPaletteDescription,
        .agentSummary = "Opens the command palette listing commands, entities or settings pages, optionally "
                        "with a query typed. Example: {\"mode\": \"entities\", \"query\": \"board\"}.",
        .category = "UI",
        .icon = "terminal",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&uiNotify>({
        .name = "ui.notify",
        .title = strings::kCommandUiNotifyTitle,
        .description = strings::kCommandUiNotifyDescription,
        .agentSummary = "Shows a toast to the person at the editor and adds it to the notification centre, "
                        "e.g. to report progress. Example: {\"message\": \"Board built\", \"level\": "
                        "\"success\"}.",
        .category = "UI",
        .icon = "notifications",
        .flags = {},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&uiQuit>({
        .name = "ui.quit",
        .title = strings::kCommandUiQuitTitle,
        .description = strings::kCommandUiQuitDescription,
        .agentSummary = "Closes the editor window as its close button does (unsaved changes are kept in the "
                        "journal and autosave). Example: {}.",
        .category = "UI",
        .icon = "logout",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::System,
        .palette = true,
    });
    registry.addAsync<&screenshot>({
        .name = "editor.screenshot",
        .title = strings::kCommandEditorScreenshotTitle,
        .description = strings::kCommandEditorScreenshotDescription,
        .agentSummary = "Asynchronous: captures the editor UI as a PNG returned in _images (MCP clients get "
                        "image content). A headless editor renders it offscreen at width x height (default "
                        "--ui-size); a window captures itself at its own size. Example: {\"width\": 1280, "
                        "\"height\": 720}.",
        .category = "Editor",
        .icon = "screenshot_monitor",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .palette = true,
    });
    registry.add<&preferencesGet>({
        .name = "preferences.get",
        .title = strings::kCommandPreferencesGetTitle,
        .description = strings::kCommandPreferencesGetDescription,
        .agentSummary =
            "Returns the user's editor preferences: theme, accent, uiScale, locale, keymap and "
            "shortcuts, agentPermissions, autosave, codeEditor, recentProjects, layout. Example: {}.",
        .category = "Preferences",
        .icon = "settings",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&preferencesSet>({
        .name = "preferences.set",
        .title = strings::kCommandPreferencesSetTitle,
        .description = strings::kCommandPreferencesSetDescription,
        .agentSummary = "Changes editor preferences with a merge patch of fields from preferences.get and "
                        "saves them for the user; returns the new preferences. Example: {\"value\": "
                        "{\"theme\": \"light\", \"uiScale\": 1.25}}.",
        .category = "Preferences",
        .icon = "tune",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::System,
    });
}

} // namespace illusion::editor::detail
