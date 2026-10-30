#include <illusion/core/Assert.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <array>
#include <format>
#include <vector>

namespace illusion
{
namespace
{

constexpr std::array kActionTypeNames{std::string_view("Button"), std::string_view("Axis"),
                                      std::string_view("Vector2")};

} // namespace

std::string_view actionTypeName(ActionType type) noexcept
{
    const auto index = static_cast<usize>(type);
    return index < kActionTypeNames.size() ? kActionTypeNames[index] : std::string_view("Button");
}

std::optional<ActionType> actionTypeFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kActionTypeNames.size(); ++i)
    {
        if (kActionTypeNames[i] == name)
        {
            return static_cast<ActionType>(i);
        }
    }
    return std::nullopt;
}

std::string writeInputActions(const InputActions& actions)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kInputActionsFormat));
    root.add("version", doc.makeU64(kInputActionsVersion));
    JsonMutValue list = doc.makeArray();
    for (ActionId id = 0; id < actions.actionCount(); ++id)
    {
        JsonMutValue action = doc.makeObject();
        action.add("name", doc.makeString(actions.actionName(id)));
        action.add("type", doc.makeString(actionTypeName(actions.actionType(id))));
        JsonMutValue bindings = doc.makeArray();
        for (const InputBinding& binding : actions.bindings(id))
        {
            bindings.push(doc.makeString(bindingToString(binding)));
        }
        action.add("bindings", bindings);
        list.push(action);
    }
    root.add("actions", list);
    doc.setRoot(root);
    std::string text;
    const Expected<void, JsonError> written = doc.write(text);
    ILL_CHECK(written.hasValue(), "cannot write the input actions");
    text += '\n';
    return text;
}

Expected<void, std::string> readInputActions(std::string_view json, InputActions& actions)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return unexpected(std::string("is not valid JSON"));
    }
    const JsonValue root = parsed->root();
    if (root.find("format").asString() != kInputActionsFormat)
    {
        return unexpected(
            std::format("is not an input actions asset (\"format\" must be \"{}\")", kInputActionsFormat));
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0)
    {
        return unexpected(std::string("needs a \"version\" number"));
    }
    if (*version > kInputActionsVersion)
    {
        return unexpected(std::format("has version {}, newer than {}", *version, kInputActionsVersion));
    }
    const JsonValue list = root.find("actions");
    if (!list.isArray())
    {
        return unexpected(std::string("'/actions' must be an array"));
    }

    InputActions read;
    std::vector<InputBinding> bindings;
    usize index = 0;
    for (const JsonValue action : list.elements())
    {
        const std::string pointer = std::format("/actions/{}", index++);
        const std::optional<std::string_view> name = action.find("name").asString();
        if (!name || !InputActions::isValidActionName(*name))
        {
            return unexpected(std::format("'{}/name' must be a valid action name", pointer));
        }
        if (read.findAction(*name))
        {
            return unexpected(std::format("'{}/name' repeats the action '{}'", pointer, *name));
        }
        const std::optional<ActionType> type =
            actionTypeFromName(action.find("type").asString().value_or(""));
        if (!type)
        {
            return unexpected(std::format("'{}/type' must be \"Button\", \"Axis\" or \"Vector2\"", pointer));
        }
        const JsonValue bindingList = action.find("bindings");
        if (!bindingList.isAbsent() && !bindingList.isArray())
        {
            return unexpected(std::format("'{}/bindings' must be an array", pointer));
        }
        bindings.clear();
        usize bindingIndex = 0;
        for (const JsonValue item : bindingList.elements())
        {
            const std::optional<InputBinding> binding = bindingFromString(item.asString().value_or(""));
            if (!binding)
            {
                return unexpected(std::format("'{}/bindings/{}' is not a binding", pointer, bindingIndex));
            }
            bindings.push_back(*binding);
            ++bindingIndex;
        }
        const ActionId id = read.addAction(*name, *type);
        read.setBindings(id, bindings);
    }
    actions = std::move(read);
    return {};
}

Expected<void, std::string> loadInputActions(const VirtualFileSystem& files, std::string_view virtualPath,
                                             InputActions& actions)
{
    std::string text;
    if (const FileError error = files.readText(virtualPath, text); error != FileError::None)
    {
        return unexpected(std::format("{}: cannot read the file ({})", virtualPath, fileErrorName(error)));
    }
    Expected<void, std::string> read = readInputActions(text, actions);
    if (!read)
    {
        return unexpected(std::format("{}: {}", virtualPath, read.error()));
    }
    return {};
}

} // namespace illusion
