#include "CommandSupport.hpp"

#include <illusion/editor/core/JsonSchema.hpp>

#include <algorithm>

namespace illusion::editor::detail
{
namespace
{

struct Target
{
    Entity entity;
    ComponentId type = kInvalidComponentId;
};

Expected<Target, CommandError> resolveTarget(CommandContext& context, std::string_view entity,
                                             std::string_view type)
{
    Expected<Entity, CommandError> resolved = context.entity(entity, "/entity");
    if (!resolved)
    {
        return unexpected(std::move(resolved).error());
    }
    Expected<ComponentId, CommandError> component = context.componentType(type, "/type");
    if (!component)
    {
        return unexpected(std::move(component).error());
    }
    return Target{*resolved, *component};
}

Expected<ComponentTypesResult, CommandError> componentTypes(CommandContext& context,
                                                            const ComponentTypesParams& params)
{
    const ComponentRegistry& registry = context.world().registry();
    ComponentTypesResult result;
    for (ComponentId id = 0; id < registry.count(); ++id)
    {
        const TypeReflection* reflection = registry.reflection(id);
        if (reflection == nullptr)
        {
            continue;
        }
        const std::string_view category = reflection->meta.category;
        if (params.category && category != *params.category &&
            !(category.starts_with(*params.category) && category.size() > params.category->size() &&
              category[params.category->size()] == '/'))
        {
            continue;
        }
        ComponentTypeInfo info;
        info.name = std::string(reflection->name);
        info.displayName = displayNameOf(*reflection);
        info.category = std::string(category);
        info.icon = std::string(reflection->meta.icon);
        info.description = std::string(reflection->meta.description);
        info.tag = reflection->size == 0;
        info.runtime = reflection->meta.transient;
        result.types.push_back(std::move(info));
    }
    std::sort(result.types.begin(), result.types.end(),
              [](const ComponentTypeInfo& a, const ComponentTypeInfo& b) { return a.name < b.name; });
    return result;
}

Expected<ComponentSchemaResult, CommandError> componentSchema(CommandContext& context,
                                                              const ComponentTypeParams& params)
{
    Expected<ComponentId, CommandError> type = context.componentType(params.type, "/type");
    if (!type)
    {
        return unexpected(std::move(type).error());
    }
    const TypeReflection& reflection = *context.world().registry().reflection(*type);
    JsonMutDocument doc;
    JsonMutValue schema = doc.makeObject();
    schema.add("$schema", doc.makeString(kJsonSchemaDialect));
    const JsonMutValue body = writeTypeSchema(doc, reflection);
    std::string text;
    ComponentSchemaResult result;
    result.type = std::string(reflection.name);
    result.version = reflection.version;
    if (!body.write(text, JsonWriteOptions{.pretty = false}))
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot write the schema"));
    }
    // "$schema" first, then the members of the type schema.
    result.schema.text = "{\"$schema\":\"" + std::string(kJsonSchemaDialect) + "\"," + text.substr(1);
    return result;
}

Expected<ComponentAddResult, CommandError> componentAdd(CommandContext& context,
                                                        const ComponentAddParams& params)
{
    Expected<Target, CommandError> target = resolveTarget(context, params.entity, params.type);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    std::optional<JsonDocument> value;
    if (params.value)
    {
        Expected<JsonDocument, CommandError> parsed = parseParam(*params.value, "/value");
        if (!parsed)
        {
            return unexpected(std::move(parsed).error());
        }
        value = std::move(parsed).value();
        if (!value->root().isObject() && !value->root().isNull())
        {
            return unexpected(CommandError::invalid("/value", "must be an object of field values"));
        }
    }
    Expected<std::vector<std::string>, CommandError> added =
        context.edit().addComponent(target->entity, target->type, value ? value->root() : JsonValue());
    if (!added)
    {
        return unexpected(withPointerPrefix(std::move(added).error(), "/value"));
    }
    return ComponentAddResult{context.summary(target->entity), std::move(added).value()};
}

Expected<EntityResult, CommandError> componentRemove(CommandContext& context,
                                                     const ComponentRefParams& params)
{
    Expected<Target, CommandError> target = resolveTarget(context, params.entity, params.type);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> removed = context.edit().removeComponent(target->entity, target->type);
        !removed)
    {
        return unexpected(std::move(removed).error());
    }
    return EntityResult{context.summary(target->entity)};
}

Expected<ComponentGetResult, CommandError> componentGet(CommandContext& context,
                                                        const ComponentGetParams& params)
{
    Expected<Target, CommandError> target = resolveTarget(context, params.entity, params.type);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    const std::optional<std::string> value = context.edit().capture(target->entity, target->type);
    if (!value)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity has no " + params.type));
    }
    ComponentGetResult result;
    if (!params.path)
    {
        result.value.text = *value;
        return result;
    }
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(*value);
    const JsonValue field = parsed ? parsed->root().pointer("/" + *params.path) : JsonValue();
    if (params.path->empty() || field.isAbsent())
    {
        return unexpected(CommandError::invalid("/path", "is not a field of " + params.type));
    }
    result.value.text = field.dump();
    return result;
}

Expected<ComponentChangeResult, CommandError> componentSet(CommandContext& context,
                                                           const ComponentSetParams& params)
{
    Expected<Target, CommandError> target = resolveTarget(context, params.entity, params.type);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    Expected<JsonDocument, CommandError> value = parseParam(params.value, "/value");
    if (!value)
    {
        return unexpected(std::move(value).error());
    }
    Expected<bool, CommandError> changed =
        context.edit().setComponent(target->entity, target->type, value->root());
    if (!changed)
    {
        return unexpected(withPointerPrefix(std::move(changed).error(), "/value"));
    }
    return ComponentChangeResult{context.summary(target->entity), *changed};
}

Expected<ComponentChangeResult, CommandError> componentSetField(CommandContext& context,
                                                                const ComponentSetFieldParams& params)
{
    Expected<Target, CommandError> target = resolveTarget(context, params.entity, params.type);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (params.path.empty())
    {
        return unexpected(CommandError::invalid("/path", "must name a field, e.g. \"position/x\""));
    }
    Expected<JsonDocument, CommandError> value = parseParam(params.value, "/value");
    if (!value)
    {
        return unexpected(std::move(value).error());
    }
    Expected<bool, CommandError> changed =
        context.edit().setField(target->entity, target->type, params.path, value->root());
    if (!changed)
    {
        return unexpected(withPointerPrefix(std::move(changed).error(), "/value"));
    }
    return ComponentChangeResult{context.summary(target->entity), *changed};
}

Expected<ComponentChangeResult, CommandError> componentReset(CommandContext& context,
                                                             const ComponentRefParams& params)
{
    Expected<Target, CommandError> target = resolveTarget(context, params.entity, params.type);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    Expected<bool, CommandError> changed = context.edit().resetComponent(target->entity, target->type);
    if (!changed)
    {
        return unexpected(std::move(changed).error());
    }
    return ComponentChangeResult{context.summary(target->entity), *changed};
}

} // namespace

void registerComponentCommands(CommandRegistry& registry)
{
    registry.add<&componentTypes>({
        .name = "component.types",
        .title = strings::kCommandComponentTypesTitle,
        .description = strings::kCommandComponentTypesDescription,
        .agentSummary =
            "Lists the reflected Component types with display names, categories, icons and whether "
            "they are tags or runtime state. Example: {\"category\": \"Physics\"}.",
        .category = "Component",
        .icon = "category",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&componentSchema>({
        .name = "component.schema",
        .title = strings::kCommandComponentSchemaTitle,
        .description = strings::kCommandComponentSchemaDescription,
        .agentSummary = "Returns the JSON Schema of a Component's value (fields, types, units as ranges). "
                        "Example: {\"type\": \"illusion.Transform\"}.",
        .category = "Component",
        .icon = "schema",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&componentAdd>({
        .name = "component.add",
        .title = strings::kCommandComponentAddTitle,
        .description = strings::kCommandComponentAddDescription,
        .agentSummary =
            "Adds a Component (and the ones it requires) with default values, then the given fields. "
            "Example: {\"entity\": \"/Game/Board\", \"type\": \"illusion.LocalTransform\", "
            "\"value\": {\"position\": [0, 1, 0]}}.",
        .category = "Component",
        .icon = "add",
        .flags = {.undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&componentRemove>({
        .name = "component.remove",
        .title = strings::kCommandComponentRemoveTitle,
        .description = strings::kCommandComponentRemoveDescription,
        .agentSummary = "Removes a Component unless another one requires it. Example: {\"entity\": "
                        "\"/Game/Board\", \"type\": \"illusion.Disabled\"}.",
        .category = "Component",
        .icon = "remove",
        .flags = {.destructive = true, .undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&componentGet>({
        .name = "component.get",
        .title = strings::kCommandComponentGetTitle,
        .description = strings::kCommandComponentGetDescription,
        .agentSummary = "Returns a Component's value, or one field with path. Example: {\"entity\": "
                        "\"/Game/Board\", \"type\": \"illusion.Transform\", \"path\": \"position/1\"}.",
        .category = "Component",
        .icon = "data_object",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&componentSet>({
        .name = "component.set",
        .title = strings::kCommandComponentSetTitle,
        .description = strings::kCommandComponentSetDescription,
        .agentSummary =
            "Merges the given fields into a Component; angles take radians or {\"deg\": n}, colours "
            "linear arrays or \"#rrggbb\". Use mergeKey for continuous edits. Example: {\"entity\": "
            "\"/Game/Board\", \"type\": \"illusion.Transform\", \"value\": {\"scale\": [2, 2, 2]}}.",
        .category = "Component",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&componentSetField>({
        .name = "component.set_field",
        .title = strings::kCommandComponentSetFieldTitle,
        .description = strings::kCommandComponentSetFieldDescription,
        .agentSummary = "Sets one field by path (fields and array indices joined by '/'). Example: "
                        "{\"entity\": \"/Game/Board\", \"type\": \"illusion.Transform\", \"path\": "
                        "\"position\", \"value\": [0, 2, 0]}.",
        .category = "Component",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&componentReset>({
        .name = "component.reset",
        .title = strings::kCommandComponentResetTitle,
        .description = strings::kCommandComponentResetDescription,
        .agentSummary =
            "Gives a Component its default value. Example: {\"entity\": \"/Game/Board\", \"type\": "
            "\"illusion.Transform\"}.",
        .category = "Component",
        .icon = "restart_alt",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail
