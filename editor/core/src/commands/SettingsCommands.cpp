#include "../ReflectedObject.hpp"
#include "CommandSupport.hpp"

#include <illusion/ecs/EntityGuid.hpp>

#include <algorithm>

namespace illusion::editor::detail
{
namespace
{

CommandError unknownType(const SettingsCatalog& catalog, std::string_view type, SettingsScope scope)
{
    CommandError error =
        CommandError::make(ErrorKind::NotFound, "there is no " + std::string(settingsScopeName(scope)) +
                                                    " settings type '" + std::string(type) + "'");
    error.errors.push_back(ValueError{"/type", "is not a registered settings type"});
    error.suggestions = catalog.suggest(type, scope);
    return error;
}

// Reads the patch @p value (params member "/value") into @p object, which holds the current settings.
Expected<void, CommandError> applyPatch(const StructReflection& type, void* object, JsonValue value,
                                        bool replace, const EntityResolver* resolver)
{
    ValueReadOptions options;
    options.mode = replace ? JsonReadMode::Replace : JsonReadMode::Merge;
    options.entities = resolver;
    const ValueReadResult read = readValue(type, object, value, options);
    if (!read.ok())
    {
        CommandError error =
            CommandError::invalid("/value" + read.errors.front().pointer, read.errors.front().message);
        for (usize i = 1; i < read.errors.size(); ++i)
        {
            error.errors.push_back(ValueError{"/value" + read.errors[i].pointer, read.errors[i].message});
        }
        return unexpected(std::move(error));
    }
    if (!read.unknownKeys.empty())
    {
        CommandError error = CommandError::invalid("/value" + read.unknownKeys.front(), "is not a field");
        std::vector<std::string_view> fields;
        for (const FieldInfo& field : type.fields)
        {
            fields.push_back(field.name);
        }
        error.suggestions = closestMatches(std::string_view(read.unknownKeys.front()).substr(1), fields);
        return unexpected(std::move(error));
    }
    return {};
}

const PreservedValue* storedSetting(std::span<const PreservedValue> settings, std::string_view type)
{
    const auto it = std::find_if(settings.begin(), settings.end(),
                                 [&](const PreservedValue& setting) { return setting.type == type; });
    return it != settings.end() ? &*it : nullptr;
}

// Reads a stored scene setting into @p object; false if it cannot be read.
bool readStored(const StructReflection& type, const PreservedValue& stored, void* object,
                const EntityResolver& resolver)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(stored.value.text);
    if (!parsed)
    {
        return false;
    }
    ValueReadOptions options;
    options.mode = JsonReadMode::Replace;
    options.entities = &resolver;
    options.savedVersion = stored.version;
    return readValue(type, object, parsed->root(), options).ok();
}

Expected<SettingsGetResult, CommandError> sceneSettingsGet(CommandContext& context,
                                                           const SettingsGetParams& params)
{
    EditorContext& editor = context.editor();
    const std::span<const PreservedValue> stored = editor.sceneSettings();
    const GuidEntityResolver resolver(context.world());
    SettingsGetResult result;
    for (const SettingsType& type : editor.settings().types())
    {
        if (type.scope != SettingsScope::Scene || (params.type && *params.type != type.type->name))
        {
            continue;
        }
        const PreservedValue* value = storedSetting(stored, type.type->name);
        SettingsEntry entry;
        entry.type = std::string(type.type->name);
        entry.present = value != nullptr;
        entry.version = type.type->version; // the value is read (and migrated) into the current version
        ReflectedObject object(type.type);
        if (value != nullptr && !readStored(*type.type, *value, object.data(), resolver))
        {
            context.warn("the scene's " + entry.type + " cannot be read; it is shown as saved");
            entry.version = value->version.value_or(0);
            entry.value.text = value->value.text;
            result.settings.push_back(std::move(entry));
            continue;
        }
        Expected<std::string, CommandError> text = writeReflected(*type.type, object.data(), &resolver);
        if (!text)
        {
            return unexpected(std::move(text).error());
        }
        entry.value.text = std::move(text).value();
        result.settings.push_back(std::move(entry));
    }
    for (const PreservedValue& value : stored)
    {
        if (editor.settings().find(value.type, SettingsScope::Scene) != nullptr ||
            (params.type && *params.type != value.type))
        {
            continue;
        }
        result.settings.push_back(
            SettingsEntry{value.type, false, true, value.version.value_or(0), {}, value.value});
    }
    if (params.type && result.settings.empty())
    {
        return unexpected(unknownType(editor.settings(), *params.type, SettingsScope::Scene));
    }
    std::sort(result.settings.begin(), result.settings.end(),
              [](const SettingsEntry& a, const SettingsEntry& b) { return a.type < b.type; });
    return result;
}

Expected<SettingsSetResult, CommandError> sceneSettingsSet(CommandContext& context,
                                                           const SettingsSetParams& params)
{
    EditorContext& editor = context.editor();
    const SettingsType* type = editor.settings().find(params.type, SettingsScope::Scene);
    if (type == nullptr)
    {
        return unexpected(unknownType(editor.settings(), params.type, SettingsScope::Scene));
    }
    Expected<JsonDocument, CommandError> patch = parseParam(params.value, "/value");
    if (!patch)
    {
        return unexpected(std::move(patch).error());
    }
    const GuidEntityResolver resolver(context.world());
    ReflectedObject object(type->type);
    SettingsSetResult result;
    result.type = params.type;
    if (patch->root().isNull())
    {
        Expected<bool, CommandError> removed =
            context.edit().setSetting(params.type, std::nullopt, std::nullopt);
        if (!removed)
        {
            return unexpected(std::move(removed).error());
        }
        Expected<std::string, CommandError> defaults = writeReflected(*type->type, object.data(), &resolver);
        if (!defaults)
        {
            return unexpected(std::move(defaults).error());
        }
        result.changed = *removed;
        result.value.text = std::move(defaults).value();
        return result;
    }
    if (!patch->root().isObject())
    {
        return unexpected(CommandError::invalid("/value", "must be an object of field values, or null"));
    }
    const PreservedValue* stored = storedSetting(editor.sceneSettings(), params.type);
    if (stored != nullptr && !params.replace.value_or(false) &&
        !readStored(*type->type, *stored, object.data(), resolver))
    {
        context.warn("the scene's " + params.type + " could not be read; unset fields get their defaults");
    }
    if (Expected<void, CommandError> applied =
            applyPatch(*type->type, object.data(), patch->root(), params.replace.value_or(false), &resolver);
        !applied)
    {
        return unexpected(std::move(applied).error());
    }
    Expected<std::string, CommandError> text = writeReflected(*type->type, object.data(), &resolver);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    Expected<bool, CommandError> changed = context.edit().setSetting(params.type, *text, type->type->version);
    if (!changed)
    {
        return unexpected(std::move(changed).error());
    }
    result.changed = *changed;
    result.present = true;
    result.value.text = std::move(text).value();
    return result;
}

// Reads the project settings file of @p type into @p object: nullopt if there is no file.
Expected<std::optional<std::string>, CommandError> readProjectSettings(CommandContext& context,
                                                                       const SettingsType& type, void* object)
{
    std::string text;
    const FileError error = context.editor().files().readText(type.file, text);
    if (error == FileError::NotFound)
    {
        return std::optional<std::string>{};
    }
    if (error != FileError::None)
    {
        return unexpected(CommandError::make(
            ErrorKind::IoError, "cannot read " + type.file + " (" + std::string(fileErrorName(error)) + ")"));
    }
    if (Expected<void, std::string> read = readSettingsFile(*type.type, text, object); !read)
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed,
                               type.file + " " + read.error() + "; pass replace to overwrite it"));
    }
    return std::optional<std::string>(std::move(text));
}

Expected<SettingsGetResult, CommandError> projectSettingsGet(CommandContext& context,
                                                             const SettingsGetParams& params)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    SettingsGetResult result;
    for (const SettingsType& type : context.editor().settings().types())
    {
        if (type.scope != SettingsScope::Project || (params.type && *params.type != type.type->name))
        {
            continue;
        }
        ReflectedObject object(type.type);
        SettingsEntry entry;
        entry.type = std::string(type.type->name);
        entry.version = type.type->version;
        entry.file = type.file;
        Expected<std::optional<std::string>, CommandError> file =
            readProjectSettings(context, type, object.data());
        if (!file)
        {
            context.warn(file.error().message);
            entry.present = true;
            ReflectedObject defaults(type.type);
            Expected<std::string, CommandError> text = writeReflected(*type.type, defaults.data(), nullptr);
            if (!text)
            {
                return unexpected(std::move(text).error());
            }
            entry.value.text = std::move(text).value();
            result.settings.push_back(std::move(entry));
            continue;
        }
        entry.present = file->has_value();
        Expected<std::string, CommandError> text = writeReflected(*type.type, object.data(), nullptr);
        if (!text)
        {
            return unexpected(std::move(text).error());
        }
        entry.value.text = std::move(text).value();
        result.settings.push_back(std::move(entry));
    }
    if (params.type && result.settings.empty())
    {
        return unexpected(unknownType(context.editor().settings(), *params.type, SettingsScope::Project));
    }
    return result;
}

Expected<SettingsSetResult, CommandError> projectSettingsSet(CommandContext& context,
                                                             const SettingsSetParams& params)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    const SettingsType* type = context.editor().settings().find(params.type, SettingsScope::Project);
    if (type == nullptr)
    {
        return unexpected(unknownType(context.editor().settings(), params.type, SettingsScope::Project));
    }
    Expected<JsonDocument, CommandError> patch = parseParam(params.value, "/value");
    if (!patch)
    {
        return unexpected(std::move(patch).error());
    }
    ReflectedObject object(type->type);
    SettingsSetResult result;
    result.type = params.type;
    if (patch->root().isNull())
    {
        if (context.editor().files().exists(type->file))
        {
            if (Expected<void, CommandError> deleted = context.edit().deleteFile(type->file); !deleted)
            {
                return unexpected(std::move(deleted).error());
            }
            result.changed = true;
        }
        Expected<std::string, CommandError> defaults = writeReflected(*type->type, object.data(), nullptr);
        if (!defaults)
        {
            return unexpected(std::move(defaults).error());
        }
        result.value.text = std::move(defaults).value();
        return result;
    }
    if (!patch->root().isObject())
    {
        return unexpected(CommandError::invalid("/value", "must be an object of field values, or null"));
    }
    std::optional<std::string> before;
    if (!params.replace.value_or(false))
    {
        Expected<std::optional<std::string>, CommandError> file =
            readProjectSettings(context, *type, object.data());
        if (!file)
        {
            return unexpected(std::move(file).error());
        }
        before = std::move(file).value();
    }
    else
    {
        std::string text;
        if (context.editor().files().readText(type->file, text) == FileError::None)
        {
            before = std::move(text);
        }
    }
    if (Expected<void, CommandError> applied =
            applyPatch(*type->type, object.data(), patch->root(), params.replace.value_or(false), nullptr);
        !applied)
    {
        return unexpected(std::move(applied).error());
    }
    Expected<std::string, std::string> file = writeSettingsFile(*type->type, object.data());
    if (!file)
    {
        return unexpected(CommandError::invalid("/value", "cannot be saved: " + file.error()));
    }
    if (before != *file)
    {
        if (Expected<bool, CommandError> written = context.edit().writeFile(type->file, *file); !written)
        {
            return unexpected(std::move(written).error());
        }
        result.changed = true;
    }
    Expected<std::string, CommandError> text = writeReflected(*type->type, object.data(), nullptr);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    result.present = true;
    result.value.text = std::move(text).value();
    return result;
}

} // namespace

void registerSettingsCommands(CommandRegistry& registry)
{
    registry.add<&sceneSettingsGet>({
        .name = "scene.settings_get",
        .title = strings::kCommandSceneSettingsGetTitle,
        .description = strings::kCommandSceneSettingsGetDescription,
        .agentSummary =
            "Returns the settings of the open scene: every registered scene settings type with all "
            "its fields (defaults when the scene does not set it), and unknown ones as saved. "
            "Example: {\"type\": \"game.Difficulty\"}.",
        .category = "Scene",
        .icon = "settings",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&sceneSettingsSet>({
        .name = "scene.settings_set",
        .title = strings::kCommandSceneSettingsSetTitle,
        .description = strings::kCommandSceneSettingsSetDescription,
        .agentSummary =
            "Merges fields into one settings struct of the open scene (replace resets the others; "
            "null removes it). Example: {\"type\": \"game.Difficulty\", \"value\": {\"enemies\": 12}}.",
        .category = "Scene",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&projectSettingsGet>({
        .name = "project.settings_get",
        .title = strings::kCommandProjectSettingsGetTitle,
        .description = strings::kCommandProjectSettingsGetDescription,
        .agentSummary = "Returns the project settings: every registered project settings type with its file "
                        "under settings: and all its fields. Example: {}.",
        .category = "Project",
        .icon = "settings",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&projectSettingsSet>({
        .name = "project.settings_set",
        .title = strings::kCommandProjectSettingsSetTitle,
        .description = strings::kCommandProjectSettingsSetDescription,
        .agentSummary =
            "Merges fields into one project settings struct and writes its file (replace resets the "
            "others; null deletes the file). Example: {\"type\": \"game.Controls\", \"value\": "
            "{\"invertY\": true}}.",
        .category = "Project",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
}

} // namespace illusion::editor::detail
