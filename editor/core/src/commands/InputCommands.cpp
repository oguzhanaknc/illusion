#include "CommandSupport.hpp"

#include <illusion/input/InputActionsAsset.hpp>

#include <algorithm>
#include <cmath>

namespace illusion::editor::detail
{
namespace
{

Expected<void, CommandError> checkInputPath(CommandContext& context, std::string_view path)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return open;
    }
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || lowerExtension(path) != kInputActionsExtension ||
        path.size() <= kAssetsRoot.size() + kInputActionsExtension.size() ||
        context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/asset", "must be a .input file in the assets: mount, e.g. \"assets:/input/game.input\""));
    }
    return {};
}

// Reads the asset at @p path into @p actions; a missing file gives no actions if @p allowMissing.
Expected<void, CommandError> readAsset(CommandContext& context, std::string_view path, InputActions& actions,
                                       bool allowMissing = false)
{
    if (Expected<void, CommandError> valid = checkInputPath(context, path); !valid)
    {
        return valid;
    }
    std::string text;
    if (const FileError error = context.editor().files().readText(path, text); error != FileError::None)
    {
        if (error == FileError::NotFound && allowMissing)
        {
            actions.clear();
            return {};
        }
        return unexpected(
            error == FileError::NotFound
                ? CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist")
                : CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                             std::string(fileErrorName(error)) + ")"));
    }
    if (Expected<void, std::string> read = readInputActions(text, actions); !read)
    {
        return unexpected(CommandError::invalid("/asset", "is not an input actions asset: " + read.error()));
    }
    return {};
}

InputActionsResult actionsResult(std::string_view path, const InputActions& actions)
{
    InputActionsResult result;
    result.asset = std::string(path);
    for (ActionId action = 0; action < actions.actionCount(); ++action)
    {
        InputActionInfo info;
        info.name = actions.actionName(action);
        info.type = std::string(actionTypeName(actions.actionType(action)));
        for (const InputBinding& binding : actions.bindings(action))
        {
            info.bindings.push_back(bindingToString(binding));
        }
        result.actions.push_back(std::move(info));
    }
    return result;
}

Expected<InputActionsResult, CommandError> writeAsset(CommandContext& context, std::string_view path,
                                                      const InputActions& actions)
{
    if (Expected<bool, CommandError> written = context.edit().writeFile(path, writeInputActions(actions));
        !written)
    {
        return unexpected(std::move(written).error());
    }
    return actionsResult(path, actions);
}

Expected<ActionId, CommandError> findAction(const InputActions& actions, std::string_view name,
                                            std::string_view pointer)
{
    if (const std::optional<ActionId> action = actions.findAction(name))
    {
        return *action;
    }
    std::vector<std::string_view> names;
    for (ActionId action = 0; action < actions.actionCount(); ++action)
    {
        names.push_back(actions.actionName(action));
    }
    CommandError error =
        CommandError::make(ErrorKind::NotFound, "there is no action '" + std::string(name) + "'");
    error.errors.push_back(ValueError{std::string(pointer), "is not an action of the asset"});
    error.suggestions = closestMatches(name, names);
    return unexpected(std::move(error));
}

Expected<InputBinding, CommandError> parseBinding(std::string_view text, std::string pointer)
{
    if (const std::optional<InputBinding> binding = bindingFromString(text))
    {
        return *binding;
    }
    return unexpected(CommandError::invalid(
        std::move(pointer), "is not a binding, e.g. \"Key.Space\", \"Gamepad.South\" or \"Keys.W.S.A.D\""));
}

Expected<InputActionsResult, CommandError> inputActions(CommandContext& context,
                                                        const InputAssetParams& params)
{
    InputActions actions;
    if (Expected<void, CommandError> read = readAsset(context, params.asset, actions); !read)
    {
        return unexpected(std::move(read).error());
    }
    return actionsResult(params.asset, actions);
}

Expected<InputActionsResult, CommandError> actionDefine(CommandContext& context,
                                                        const InputActionDefineParams& params)
{
    InputActions actions;
    if (Expected<void, CommandError> read = readAsset(context, params.asset, actions, true); !read)
    {
        return unexpected(std::move(read).error());
    }
    if (!InputActions::isValidActionName(params.name))
    {
        return unexpected(
            CommandError::invalid("/name", "must be a non-empty name without control characters"));
    }
    const std::optional<ActionType> type = actionTypeFromName(params.type);
    if (!type)
    {
        return unexpected(CommandError::invalid("/type", "must be \"Button\", \"Axis\" or \"Vector2\""));
    }
    std::vector<InputBinding> bindings;
    if (params.bindings)
    {
        for (usize i = 0; i < params.bindings->size(); ++i)
        {
            Expected<InputBinding, CommandError> binding =
                parseBinding((*params.bindings)[i], "/bindings/" + std::to_string(i));
            if (!binding)
            {
                return unexpected(std::move(binding).error());
            }
            bindings.push_back(*binding);
        }
    }
    // An existing action keeps its place.
    if (const std::optional<ActionId> existing = actions.findAction(params.name))
    {
        InputActions rebuilt;
        for (ActionId action = 0; action < actions.actionCount(); ++action)
        {
            const bool replaced = action == *existing;
            const ActionId added =
                rebuilt.addAction(actions.actionName(action), replaced ? *type : actions.actionType(action));
            if (replaced)
            {
                rebuilt.setBindings(added, bindings);
            }
            else
            {
                rebuilt.setBindings(added, actions.bindings(action));
            }
        }
        return writeAsset(context, params.asset, rebuilt);
    }
    actions.setBindings(actions.addAction(params.name, *type), bindings);
    return writeAsset(context, params.asset, actions);
}

Expected<InputActionsResult, CommandError> actionRemove(CommandContext& context,
                                                        const InputActionParams& params)
{
    InputActions actions;
    if (Expected<void, CommandError> read = readAsset(context, params.asset, actions); !read)
    {
        return unexpected(std::move(read).error());
    }
    Expected<ActionId, CommandError> action = findAction(actions, params.name, "/name");
    if (!action)
    {
        return unexpected(std::move(action).error());
    }
    actions.removeAction(*action);
    return writeAsset(context, params.asset, actions);
}

Expected<InputActionsResult, CommandError> actionRename(CommandContext& context,
                                                        const InputActionRenameParams& params)
{
    InputActions actions;
    if (Expected<void, CommandError> read = readAsset(context, params.asset, actions); !read)
    {
        return unexpected(std::move(read).error());
    }
    Expected<ActionId, CommandError> action = findAction(actions, params.name, "/name");
    if (!action)
    {
        return unexpected(std::move(action).error());
    }
    if (!InputActions::isValidActionName(params.newName))
    {
        return unexpected(
            CommandError::invalid("/newName", "must be a non-empty name without control characters"));
    }
    if (params.newName != params.name && actions.findAction(params.newName))
    {
        return unexpected(CommandError::make(ErrorKind::AlreadyExists,
                                             "there is an action '" + params.newName + "' already"));
    }
    static_cast<void>(actions.renameAction(*action, params.newName));
    return writeAsset(context, params.asset, actions);
}

Expected<InputActionsResult, CommandError> bindingChange(CommandContext& context,
                                                         const InputBindParams& params, bool bind)
{
    InputActions actions;
    if (Expected<void, CommandError> read = readAsset(context, params.asset, actions); !read)
    {
        return unexpected(std::move(read).error());
    }
    Expected<ActionId, CommandError> action = findAction(actions, params.action, "/action");
    if (!action)
    {
        return unexpected(std::move(action).error());
    }
    Expected<InputBinding, CommandError> binding = parseBinding(params.binding, "/binding");
    if (!binding)
    {
        return unexpected(std::move(binding).error());
    }
    const std::span<const InputBinding> bindings = actions.bindings(*action);
    const bool bound = std::find(bindings.begin(), bindings.end(), *binding) != bindings.end();
    if (bind && !bound)
    {
        actions.addBinding(*action, *binding);
    }
    else if (!bind && !actions.removeBinding(*action, *binding))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.action + "' has no binding '" +
                                                                      params.binding + "'"));
    }
    return writeAsset(context, params.asset, actions);
}

Expected<InputActionsResult, CommandError> inputBind(CommandContext& context, const InputBindParams& params)
{
    return bindingChange(context, params, true);
}

Expected<InputActionsResult, CommandError> inputUnbind(CommandContext& context, const InputBindParams& params)
{
    return bindingChange(context, params, false);
}

Expected<PlayHost*, CommandError> requireRunningGame(CommandContext& context)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    return play;
}

std::optional<MouseButton> mouseButtonFromName(std::string_view name) noexcept
{
    for (u8 i = 0; i < static_cast<u8>(MouseButton::Count); ++i)
    {
        if (mouseButtonName(static_cast<MouseButton>(i)) == name)
        {
            return static_cast<MouseButton>(i);
        }
    }
    return std::nullopt;
}

Expected<NoResult, CommandError> inputInject(CommandContext& context, const InputInjectParams& params)
{
    Expected<PlayHost*, CommandError> play = requireRunningGame(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    // Validate everything before the game sees any of it.
    std::vector<PlayInputEvent> events;
    if (params.keys)
    {
        for (usize i = 0; i < params.keys->size(); ++i)
        {
            const InputKeyEvent& key = (*params.keys)[i];
            const std::optional<Key> parsed = keyFromName(key.key);
            if (!parsed || *parsed == Key::Unknown)
            {
                return unexpected(
                    CommandError::invalid("/keys/" + std::to_string(i) + "/key",
                                          "is not a key name, e.g. \"Space\", \"Left\" or \"A\""));
            }
            events.push_back(
                PlayInputEvent{.kind = PlayInputEvent::Kind::Key, .key = *parsed, .down = key.down});
        }
    }
    if (params.mouseButtons)
    {
        for (usize i = 0; i < params.mouseButtons->size(); ++i)
        {
            const InputButtonEvent& button = (*params.mouseButtons)[i];
            const std::optional<MouseButton> parsed = mouseButtonFromName(button.button);
            if (!parsed)
            {
                return unexpected(
                    CommandError::invalid("/mouseButtons/" + std::to_string(i) + "/button",
                                          "must be \"Left\", \"Right\", \"Middle\", \"Button4\" "
                                          "or \"Button5\""));
            }
            events.push_back(PlayInputEvent{
                .kind = PlayInputEvent::Kind::MouseButton, .button = *parsed, .down = button.down});
        }
    }
    if (params.mousePosition)
    {
        if (!std::isfinite(params.mousePosition->x) || !std::isfinite(params.mousePosition->y))
        {
            return unexpected(CommandError::invalid("/mousePosition", "must be finite"));
        }
        events.push_back(
            PlayInputEvent{.kind = PlayInputEvent::Kind::MousePosition, .position = *params.mousePosition});
    }
    if (events.empty())
    {
        return unexpected(
            CommandError::invalid("/keys", "give at least one of keys, mouseButtons and mousePosition"));
    }
    if (!context.dryRun())
    {
        for (const PlayInputEvent& event : events)
        {
            if (Expected<void, CommandError> injected = (*play)->inject(event); !injected)
            {
                return unexpected(std::move(injected).error());
            }
        }
    }
    return NoResult{};
}

Expected<InputReadResult, CommandError> inputRead(CommandContext& context, const InputReadParams& params)
{
    Expected<PlayHost*, CommandError> play = requireRunningGame(context);
    if (!play)
    {
        return unexpected(std::move(play).error());
    }
    InputReadResult result;
    (*play)->actionStates(result.actions);
    if (!params.actions)
    {
        return result;
    }
    std::vector<PlayActionState> chosen;
    for (usize i = 0; i < params.actions->size(); ++i)
    {
        const std::string& name = (*params.actions)[i];
        const auto found = std::find_if(result.actions.begin(), result.actions.end(),
                                        [&](const PlayActionState& state) { return state.name == name; });
        if (found == result.actions.end())
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "the game has no action '" + name + "'"));
        }
        chosen.push_back(*found);
    }
    result.actions = std::move(chosen);
    return result;
}

} // namespace

void registerInputCommands(CommandRegistry& registry)
{
    registry.add<&inputActions>({
        .name = "input.actions",
        .title = strings::kCommandInputActionsTitle,
        .description = strings::kCommandInputActionsDescription,
        .agentSummary = "Lists the actions of an input actions asset (.input) with their types and bindings. "
                        "Example: {\"asset\": \"assets:/input/game.input\"}.",
        .category = "Input",
        .icon = "sports_esports",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&actionDefine>({
        .name = "input.action_define",
        .title = strings::kCommandInputActionDefineTitle,
        .description = strings::kCommandInputActionDefineDescription,
        .agentSummary =
            "Adds an action to an input actions asset, creating the file if needed, or replaces the type and "
            "bindings of an existing one; undoable. Types are Button, Axis and Vector2; bindings look like "
            "\"Key.Space\", \"Gamepad.South\", \"Axis.LeftX\", \"Keys.W.S.A.D\" or \"Stick.Left\". Example: "
            "{\"asset\": \"assets:/input/game.input\", \"name\": \"Rotate\", \"type\": \"Button\", "
            "\"bindings\": [\"Key.Up\", \"Gamepad.North\"]}.",
        .category = "Input",
        .icon = "add_circle",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&actionRemove>({
        .name = "input.action_remove",
        .title = strings::kCommandInputActionRemoveTitle,
        .description = strings::kCommandInputActionRemoveDescription,
        .agentSummary = "Removes an action and its bindings from an input actions asset; undoable. Example: "
                        "{\"asset\": \"assets:/input/game.input\", \"name\": \"Rotate\"}.",
        .category = "Input",
        .icon = "remove_circle",
        .flags = {.destructive = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&actionRename>({
        .name = "input.action_rename",
        .title = strings::kCommandInputActionRenameTitle,
        .description = strings::kCommandInputActionRenameDescription,
        .agentSummary =
            "Renames an action of an input actions asset; undoable. Game code finds actions by "
            "name, so update it too. Example: {\"asset\": \"assets:/input/game.input\", \"name\": "
            "\"Rotate\", \"newName\": \"RotateRight\"}.",
        .category = "Input",
        .icon = "edit",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&inputBind>({
        .name = "input.bind",
        .title = strings::kCommandInputBindTitle,
        .description = strings::kCommandInputBindDescription,
        .agentSummary = "Adds a binding to an action of an input actions asset (nothing if it has it); "
                        "undoable. Example: {\"asset\": \"assets:/input/game.input\", \"action\": \"Drop\", "
                        "\"binding\": \"Key.Space\"}.",
        .category = "Input",
        .icon = "link",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&inputUnbind>({
        .name = "input.unbind",
        .title = strings::kCommandInputUnbindTitle,
        .description = strings::kCommandInputUnbindDescription,
        .agentSummary = "Removes a binding from an action of an input actions asset; undoable. Example: "
                        "{\"asset\": \"assets:/input/game.input\", \"action\": \"Drop\", \"binding\": "
                        "\"Key.Space\"}.",
        .category = "Input",
        .icon = "link_off",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&inputInject>({
        .name = "input.inject",
        .title = strings::kCommandInputInjectTitle,
        .description = strings::kCommandInputInjectDescription,
        .agentSummary =
            "Presses or releases keys and mouse buttons, or moves the cursor, in the running game as if "
            "the player did; the game's actions see it on its next update (follow with play.step). Key "
            "names: \"Space\", \"Left\", \"A\", \"Enter\"... Example: {\"keys\": [{\"key\": \"Left\", "
            "\"down\": true}]}.",
        .category = "Input",
        .icon = "keyboard",
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
    });
    registry.add<&inputRead>({
        .name = "input.read",
        .title = strings::kCommandInputReadTitle,
        .description = strings::kCommandInputReadDescription,
        .agentSummary = "Returns the state of the running game's input actions after its last update: held, "
                        "pressed, released and value. Example: {\"actions\": [\"Drop\"]}.",
        .category = "Input",
        .icon = "sports_esports",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Play,
    });
}

} // namespace illusion::editor::detail
