#include <illusion/core/Json.hpp>
#include <illusion/editor/mcp/McpCatalog.hpp>

#include <algorithm>

namespace illusion::editor::mcp
{
namespace
{

constexpr usize kMaxToolName = 64;

std::string compact(const JsonMutDocument& doc)
{
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return "{}";
    }
    return text;
}

std::string numberText(JsonValue value)
{
    if (const std::optional<i64> integer = value.asI64(); integer && value.isInteger())
    {
        return std::to_string(*integer);
    }
    return value.dump();
}

// A short description of the values @p schema accepts, e.g. "number[3]" or "\"push\" | \"pull\"".
std::string typeText(JsonValue schema, bool& nullable)
{
    if (const JsonValue values = schema.find("enum"); values.isArray())
    {
        std::string text;
        for (const JsonValue value : values.elements())
        {
            text += text.empty() ? "" : " | ";
            text += value.dump();
        }
        return text;
    }
    if (const JsonValue options = schema.find("anyOf"); options.isArray())
    {
        std::string text;
        for (const JsonValue option : options.elements())
        {
            if (option.find("type").asString() == "null")
            {
                nullable = true;
                continue;
            }
            bool inner = false;
            const std::string part = typeText(option, inner);
            nullable = nullable || inner;
            text += text.empty() ? "" : " or ";
            text += part;
        }
        return text.empty() ? std::string("null") : text;
    }
    const JsonValue type = schema.find("type");
    std::string text;
    if (type.isArray())
    {
        for (const JsonValue name : type.elements())
        {
            if (name.asString() == "null")
            {
                nullable = true;
                continue;
            }
            text += text.empty() ? "" : " or ";
            text += name.asString().value_or("any");
        }
    }
    else
    {
        text = std::string(type.asString().value_or("any"));
    }
    if (text == "array")
    {
        bool inner = false;
        const JsonValue items = schema.find("items");
        text = items.isAbsent() ? std::string("array") : typeText(items, inner) + "[]";
        const JsonValue minItems = schema.find("minItems");
        const JsonValue maxItems = schema.find("maxItems");
        if (minItems.isInteger() && minItems.asU64() == maxItems.asU64())
        {
            text.pop_back();
            text += numberText(minItems) + "]";
        }
    }
    else if (text == "string")
    {
        if (const std::optional<std::string_view> pattern = schema.find("pattern").asString())
        {
            text += " matching " + std::string(*pattern);
        }
    }
    const JsonValue minimum = schema.find("minimum");
    const JsonValue maximum = schema.find("maximum");
    if (minimum.isNumber() && maximum.isNumber())
    {
        text += " in [" + numberText(minimum) + ", " + numberText(maximum) + "]";
    }
    else if (minimum.isNumber())
    {
        text += " >= " + numberText(minimum);
    }
    else if (maximum.isNumber())
    {
        text += " <= " + numberText(maximum);
    }
    return text;
}

// Options every mutating command takes; documented once instead of per parameter.
bool isCommonOption(std::string_view name) noexcept
{
    return name == "dryRun" || name == "mergeKey" || name == "clientToken" || name == "expectRevision";
}

std::string toolJson(std::string_view name, std::string_view description, std::string_view inputSchema,
                     std::string_view outputSchema, bool readOnly, bool destructive, bool idempotent)
{
    JsonMutDocument doc;
    JsonMutValue tool = doc.makeObject();
    tool.add("name", doc.makeString(name));
    tool.add("description", doc.makeString(description));
    const Expected<JsonDocument, JsonError> input = JsonDocument::parse(inputSchema);
    if (input && input->root().isObject())
    {
        tool.add("inputSchema", doc.copy(input->root()));
    }
    else
    {
        JsonMutValue empty = tool.addObject("inputSchema");
        empty.add("type", doc.makeString("object"));
    }
    if (!outputSchema.empty())
    {
        const Expected<JsonDocument, JsonError> output = JsonDocument::parse(outputSchema);
        if (output && output->root().isObject() && output->root().find("type").asString() == "object")
        {
            tool.add("outputSchema", doc.copy(output->root()));
        }
    }
    JsonMutValue annotations = tool.addObject("annotations");
    annotations.add("readOnlyHint", doc.makeBool(readOnly));
    annotations.add("destructiveHint", doc.makeBool(destructive));
    annotations.add("idempotentHint", doc.makeBool(idempotent));
    annotations.add("openWorldHint", doc.makeBool(false));
    doc.setRoot(tool);
    return compact(doc);
}

constexpr std::string_view kCallSchema =
    R"({"type":"object","properties":{"method":{"type":"string","description":"Editor command, e.g. \"entity.create\"."},"params":{"type":"object","description":"Its params."}},"required":["method"],"additionalProperties":false})";

} // namespace

const CommandSpec* Catalog::find(std::string_view method) const noexcept
{
    const auto it = std::find_if(commands.begin(), commands.end(),
                                 [&](const CommandSpec& command) { return command.name == method; });
    return it == commands.end() ? nullptr : &*it;
}

Expected<Catalog, std::string> catalogFromOpenRpc(std::string_view document)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(document);
    if (!parsed)
    {
        return unexpected("the OpenRPC document is not JSON: " + parsed.error().message);
    }
    const JsonValue methods = parsed->root().find("methods");
    if (!methods.isArray())
    {
        return unexpected(std::string("the OpenRPC document has no methods"));
    }
    Catalog catalog;
    for (const JsonValue method : methods.elements())
    {
        const JsonValue extension = method.find("x-illusion");
        const std::optional<std::string_view> name = method.find("name").asString();
        if (!name || extension.find("server").asBool().value_or(false) ||
            extension.find("version").isAbsent())
        {
            continue;
        }
        CommandSpec command;
        command.name = std::string(*name);
        command.description = std::string(method.find("description").asString().value_or(""));
        command.category = std::string(extension.find("category").asString().value_or(""));
        command.permission = std::string(extension.find("permission").asString().value_or(""));
        command.version = static_cast<u32>(extension.find("version").asU64().value_or(1));
        const JsonValue flags = extension.find("flags");
        command.readOnly = flags.find("readOnly").asBool().value_or(false);
        command.destructive = flags.find("destructive").asBool().value_or(false);
        command.idempotent = flags.find("idempotent").asBool().value_or(false);
        command.async = flags.find("async").asBool().value_or(false);

        // The params are content descriptors; the tool takes them as one object.
        JsonMutDocument doc;
        JsonMutValue schema = doc.makeObject();
        schema.add("type", doc.makeString("object"));
        JsonMutValue properties = schema.addObject("properties");
        JsonMutValue required = doc.makeArray();
        for (const JsonValue param : method.find("params").elements())
        {
            const std::optional<std::string_view> paramName = param.find("name").asString();
            if (!paramName)
            {
                continue;
            }
            properties.add(*paramName, doc.copy(param.find("schema")));
            if (param.find("required").asBool().value_or(false))
            {
                required.push(doc.makeString(*paramName));
            }
        }
        if (required.size() != 0)
        {
            schema.add("required", required);
        }
        schema.add("additionalProperties", doc.makeBool(false));
        doc.setRoot(schema);
        command.inputSchema = compact(doc);
        const JsonValue result = method.find("result").find("schema");
        command.outputSchema = result.isObject() ? result.dump() : std::string();
        catalog.commands.push_back(std::move(command));
    }
    return catalog;
}

std::string toolName(std::string_view method)
{
    std::string name;
    for (const char c : method.substr(0, kMaxToolName))
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        name += valid ? c : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : '_';
    }
    return name;
}

bool isValidToolName(std::string_view name) noexcept
{
    return !name.empty() && name.size() <= kMaxToolName && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

Expected<ToolSelection, std::string> ToolSelection::parse(std::string_view text)
{
    ToolSelection selection;
    if (text == "all")
    {
        return selection;
    }
    if (text.ends_with(','))
    {
        return unexpected("--tools takes all, core or a comma-separated list of domains, not '" +
                          std::string(text) + "'");
    }
    while (!text.empty())
    {
        const usize comma = text.find(',');
        const std::string_view domain = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        if (domain == "core")
        {
            for (const std::string_view core : kCoreDomains)
            {
                selection.m_domains.emplace_back(core);
            }
            continue;
        }
        if (domain.empty() || domain == "all" || !std::all_of(domain.begin(), domain.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || c == '_';
            }))
        {
            return unexpected("--tools takes all, core or a comma-separated list of domains, not '" +
                              std::string(domain) + "'");
        }
        selection.m_domains.emplace_back(domain);
    }
    if (selection.m_domains.empty())
    {
        return unexpected(std::string("--tools needs at least one domain"));
    }
    return selection;
}

bool ToolSelection::includes(std::string_view method) const noexcept
{
    if (m_domains.empty())
    {
        return true;
    }
    const std::string_view domain = method.substr(0, method.find('.'));
    return std::find(m_domains.begin(), m_domains.end(), domain) != m_domains.end();
}

std::string toolDescription(const CommandSpec& command)
{
    std::string text = command.description;
    const Expected<JsonDocument, JsonError> schema = JsonDocument::parse(command.inputSchema);
    if (schema)
    {
        const JsonValue required = schema->root().find("required");
        std::string params;
        bool common = false;
        bool world = false;
        for (const JsonMember member : schema->root().find("properties").members())
        {
            if (isCommonOption(member.key))
            {
                common = true;
                continue;
            }
            if (member.key == "world")
            {
                world = true;
                continue;
            }
            bool isRequired = false;
            for (const JsonValue name : required.elements())
            {
                isRequired = isRequired || name.asString() == member.key;
            }
            bool nullable = false;
            const std::string type = typeText(member.value, nullable);
            params += "\n- " + std::string(member.key) + " (" + type +
                      (isRequired ? ", required" : ", optional") + ")";
            if (const std::optional<std::string_view> about = member.value.find("description").asString())
            {
                params += ": " + std::string(*about);
            }
        }
        if (!params.empty())
        {
            text += "\n\nParameters:" + params;
        }
        if (common)
        {
            text += "\n\nAlso takes dryRun (validate only), mergeKey, clientToken and expectRevision.";
        }
        if (world)
        {
            text +=
                "\n\nWhile a play session runs it acts on the play World; pass world: \"edit\" for the edit "
                "World.";
        }
    }
    text += "\n\nPermission: " + command.permission + ".";
    if (command.async)
    {
        text += " Long-running: reports progress and can be cancelled.";
    }
    return text;
}

std::vector<ToolDefinition> generateTools(const Catalog& catalog, const ToolSelection& selection)
{
    std::vector<ToolDefinition> tools;
    bool batchSelected = false;
    for (const CommandSpec& command : catalog.commands)
    {
        if (!selection.includes(command.name))
        {
            continue;
        }
        std::string name = toolName(command.name);
        // Names are unique: a clash (two methods that differ only in punctuation) keeps the first.
        if (name.empty() || name == kCallTool || name == kBatchTool || findTool(tools, name) != nullptr)
        {
            continue;
        }
        batchSelected = batchSelected || command.name == "editor.batch";
        std::string json = toolJson(name, toolDescription(command), command.inputSchema, command.outputSchema,
                                    command.readOnly, command.destructive, command.idempotent);
        tools.push_back(ToolDefinition{std::move(name), command.name, std::move(json)});
    }
    if (!catalog.commands.empty())
    {
        tools.push_back(ToolDefinition{
            std::string(kCallTool),
            {},
            toolJson(
                kCallTool,
                "Runs any editor command by its method name, including commands not offered as tools. Use "
                "editor_list_commands and editor_describe (or illusion://commands) to find them. Example: "
                "{\"method\": \"scene.info\", \"params\": {}}.",
                kCallSchema, {}, false, false, false)});
    }
    if (!batchSelected)
    {
        if (const CommandSpec* batch = catalog.find("editor.batch"))
        {
            tools.push_back(ToolDefinition{std::string(kBatchTool), batch->name,
                                           toolJson(kBatchTool, toolDescription(*batch), batch->inputSchema,
                                                    batch->outputSchema, false, false, false)});
        }
    }
    return tools;
}

const ToolDefinition* findTool(std::span<const ToolDefinition> tools, std::string_view name) noexcept
{
    const auto it = std::find_if(tools.begin(), tools.end(),
                                 [&](const ToolDefinition& tool) { return tool.name == name; });
    return it == tools.end() ? nullptr : &*it;
}

} // namespace illusion::editor::mcp
