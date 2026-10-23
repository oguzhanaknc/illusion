#include <illusion/core/Json.hpp>
#include <illusion/editor/core/Operation.hpp>

#include <array>

namespace illusion::editor
{
namespace
{

std::string_view textOf(const std::array<char, kGuidTextLength>& guid) noexcept
{
    return {guid.data(), guid.size()};
}

void addOptional(JsonMutDocument& doc, JsonMutValue& object, std::string_view key,
                 const std::optional<std::string>& value)
{
    object.add(key, value ? doc.makeString(*value) : doc.makeNull());
}

Expected<std::optional<std::string>, std::string> readOptional(JsonValue object, std::string_view key)
{
    const JsonValue value = object.find(key);
    if (value.isNull())
    {
        return std::optional<std::string>{};
    }
    const std::optional<std::string_view> text = value.asString();
    if (!text)
    {
        return unexpected(std::string(key) + " must be a string or null");
    }
    return std::optional<std::string>(std::string(*text));
}

Expected<std::optional<u32>, std::string> readVersion(JsonValue object, std::string_view key)
{
    const JsonValue value = object.find(key);
    if (value.isNull() || value.isAbsent())
    {
        return std::optional<u32>{};
    }
    const std::optional<u64> number = value.asU64();
    if (!number || *number > ~u32{0})
    {
        return unexpected(std::string(key) + " must be a version number or null");
    }
    return std::optional<u32>(static_cast<u32>(*number));
}

Expected<EntityGuid, std::string> readGuid(JsonValue value)
{
    const std::optional<std::string_view> text = value.asString();
    const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
    if (!guid)
    {
        return unexpected(std::string("entity must be an id of 16 hex digits"));
    }
    return *guid;
}

} // namespace

Operation inverted(const Operation& operation)
{
    if (const auto* change = std::get_if<ComponentChange>(&operation))
    {
        return ComponentChange{change->entity, change->type, change->after, change->before};
    }
    if (const auto* lifetime = std::get_if<EntityLifetime>(&operation))
    {
        EntityLifetime reversed;
        reversed.spawn = !lifetime->spawn;
        reversed.entities = lifetime->entities;
        return reversed;
    }
    if (const auto* settings = std::get_if<SettingsChange>(&operation))
    {
        return SettingsChange{settings->type, settings->after, settings->before, settings->afterVersion,
                              settings->beforeVersion};
    }
    const auto& file = std::get<FileChange>(operation);
    FileChange reversed = file;
    if (file.kind == FileChange::Kind::Move)
    {
        reversed.path = file.target;
        reversed.target = file.path;
    }
    else
    {
        reversed.before = file.after;
        reversed.after = file.before;
    }
    return reversed;
}

usize operationBytes(const Operation& operation) noexcept
{
    usize bytes = sizeof(Operation);
    if (const auto* change = std::get_if<ComponentChange>(&operation))
    {
        bytes += change->type.size() + (change->before ? change->before->size() : 0) +
                 (change->after ? change->after->size() : 0);
    }
    else if (const auto* lifetime = std::get_if<EntityLifetime>(&operation))
    {
        for (const EntityRecord& record : lifetime->entities)
        {
            bytes += sizeof(EntityRecord) + record.components.size();
        }
    }
    else if (const auto* settings = std::get_if<SettingsChange>(&operation))
    {
        bytes += settings->type.size() + (settings->before ? settings->before->size() : 0) +
                 (settings->after ? settings->after->size() : 0);
    }
    else
    {
        const auto& file = std::get<FileChange>(operation);
        bytes += file.path.size() + file.target.size() + (file.before ? file.before->size() : 0) +
                 (file.after ? file.after->size() : 0);
    }
    return bytes;
}

JsonMutValue writeOperation(JsonMutDocument& doc, const Operation& operation)
{
    JsonMutValue object = doc.makeObject();
    if (const auto* change = std::get_if<ComponentChange>(&operation))
    {
        object.add("op", doc.makeString("component"));
        object.add("entity", doc.makeString(textOf(formatGuid(change->entity))));
        object.add("type", doc.makeString(change->type));
        addOptional(doc, object, "before", change->before);
        addOptional(doc, object, "after", change->after);
    }
    else if (const auto* lifetime = std::get_if<EntityLifetime>(&operation))
    {
        object.add("op", doc.makeString(lifetime->spawn ? "spawn" : "despawn"));
        JsonMutValue entities = object.addArray("entities");
        for (const EntityRecord& record : lifetime->entities)
        {
            JsonMutValue entry = entities.pushObject();
            entry.add("id", doc.makeString(textOf(formatGuid(record.guid))));
            entry.add("components", doc.makeString(record.components));
        }
    }
    else if (const auto* settings = std::get_if<SettingsChange>(&operation))
    {
        object.add("op", doc.makeString("settings"));
        object.add("type", doc.makeString(settings->type));
        addOptional(doc, object, "before", settings->before);
        addOptional(doc, object, "after", settings->after);
        object.add("beforeVersion",
                   settings->beforeVersion ? doc.makeU64(*settings->beforeVersion) : doc.makeNull());
        object.add("afterVersion",
                   settings->afterVersion ? doc.makeU64(*settings->afterVersion) : doc.makeNull());
    }
    else
    {
        const auto& file = std::get<FileChange>(operation);
        object.add("op", doc.makeString(file.kind == FileChange::Kind::Move ? "file_move" : "file_replace"));
        object.add("path", doc.makeString(file.path));
        if (file.kind == FileChange::Kind::Move)
        {
            object.add("target", doc.makeString(file.target));
        }
        else
        {
            addOptional(doc, object, "before", file.before);
            addOptional(doc, object, "after", file.after);
        }
    }
    return object;
}

Expected<Operation, std::string> readOperation(JsonValue json)
{
    const std::optional<std::string_view> op = json.find("op").asString();
    if (!op)
    {
        return unexpected(std::string("operation without \"op\""));
    }
    if (*op == "component")
    {
        ComponentChange change;
        Expected<EntityGuid, std::string> guid = readGuid(json.find("entity"));
        const std::optional<std::string_view> type = json.find("type").asString();
        auto before = readOptional(json, "before");
        auto after = readOptional(json, "after");
        if (!guid || !type || !before || !after)
        {
            return unexpected(std::string("malformed component operation"));
        }
        change.entity = *guid;
        change.type = std::string(*type);
        change.before = std::move(*before);
        change.after = std::move(*after);
        return Operation(std::move(change));
    }
    if (*op == "spawn" || *op == "despawn")
    {
        EntityLifetime lifetime;
        lifetime.spawn = *op == "spawn";
        const JsonValue entities = json.find("entities");
        if (!entities.isArray())
        {
            return unexpected(std::string("malformed entity operation"));
        }
        for (const JsonValue entry : entities.elements())
        {
            Expected<EntityGuid, std::string> guid = readGuid(entry.find("id"));
            const std::optional<std::string_view> components = entry.find("components").asString();
            if (!guid || !components)
            {
                return unexpected(std::string("malformed entity record"));
            }
            lifetime.entities.push_back(EntityRecord{*guid, std::string(*components)});
        }
        return Operation(std::move(lifetime));
    }
    if (*op == "file_move" || *op == "file_replace")
    {
        FileChange file;
        const std::optional<std::string_view> path = json.find("path").asString();
        if (!path)
        {
            return unexpected(std::string("malformed file operation"));
        }
        file.path = std::string(*path);
        if (*op == "file_move")
        {
            const std::optional<std::string_view> target = json.find("target").asString();
            if (!target)
            {
                return unexpected(std::string("malformed file operation"));
            }
            file.kind = FileChange::Kind::Move;
            file.target = std::string(*target);
        }
        else
        {
            auto before = readOptional(json, "before");
            auto after = readOptional(json, "after");
            if (!before || !after)
            {
                return unexpected(std::string("malformed file operation"));
            }
            file.before = std::move(*before);
            file.after = std::move(*after);
        }
        return Operation(std::move(file));
    }
    if (*op == "settings")
    {
        const std::optional<std::string_view> type = json.find("type").asString();
        auto before = readOptional(json, "before");
        auto after = readOptional(json, "after");
        auto beforeVersion = readVersion(json, "beforeVersion");
        auto afterVersion = readVersion(json, "afterVersion");
        if (!type || !before || !after || !beforeVersion || !afterVersion)
        {
            return unexpected(std::string("malformed settings operation"));
        }
        return Operation(SettingsChange{std::string(*type), std::move(*before), std::move(*after),
                                        *beforeVersion, *afterVersion});
    }
    return unexpected("unknown operation \"" + std::string(*op) + "\"");
}

} // namespace illusion::editor
