#include <illusion/core/Assert.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/SettingsCatalog.hpp>

#include <algorithm>

namespace illusion::editor
{
namespace
{

bool holdsEntities(const ValueType& type) noexcept
{
    switch (type.type)
    {
    case FieldType::Entity:
        return true;
    case FieldType::Struct:
        return std::any_of(type.structType->fields.begin(), type.structType->fields.end(),
                           [](const FieldInfo& field) { return holdsEntities(field); });
    case FieldType::FixedArray:
    case FieldType::Array:
    case FieldType::Optional:
        return holdsEntities(*type.element);
    default:
        return false;
    }
}

} // namespace

std::string_view settingsScopeName(SettingsScope scope) noexcept
{
    return scope == SettingsScope::Project ? "project" : "scene";
}

void SettingsCatalog::add(std::string_view type, SettingsScope scope, std::string_view file,
                          const StructRegistry& structs)
{
    const StructReflection* reflection = structs.reflection(structs.findByStableName(type));
    ILL_ASSERT(reflection != nullptr, "settings type {} is not a reflected struct", type);
    ILL_ASSERT(find(type, scope) == nullptr, "settings type {} is registered twice", type);
    if (scope == SettingsScope::Project)
    {
        ILL_ASSERT(file.starts_with("settings:/") && file.ends_with(".json") && file.size() > 15,
                   "project settings {} need a file \"settings:/<name>.json\", not \"{}\"", type, file);
        ILL_ASSERT(std::none_of(m_types.begin(), m_types.end(),
                                [&](const SettingsType& other) { return other.file == file; }),
                   "two project settings types use {}", file);
        ILL_ASSERT(std::none_of(reflection->fields.begin(), reflection->fields.end(),
                                [](const FieldInfo& field) { return holdsEntities(field); }),
                   "project settings {} cannot hold entity references", type);
    }
    else
    {
        ILL_ASSERT(file.empty(), "scene settings {} have no file", type);
    }
    SettingsType entry{reflection, scope, std::string(file)};
    const auto position = std::upper_bound(
        m_types.begin(), m_types.end(), entry, [](const SettingsType& a, const SettingsType& b) {
            return a.scope != b.scope ? a.scope < b.scope : a.type->name < b.type->name;
        });
    m_types.insert(position, std::move(entry));
}

const SettingsType* SettingsCatalog::find(std::string_view type, SettingsScope scope) const noexcept
{
    for (const SettingsType& entry : m_types)
    {
        if (entry.scope == scope && entry.type->name == type)
        {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<std::string> SettingsCatalog::suggest(std::string_view type, SettingsScope scope) const
{
    std::vector<std::string_view> names;
    for (const SettingsType& entry : m_types)
    {
        if (entry.scope == scope)
        {
            names.push_back(entry.type->name);
        }
    }
    return closestMatches(type, names);
}

Expected<std::string, std::string> writeSettingsFile(const StructReflection& type, const void* value)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kSettingsFormat));
    root.add("type", doc.makeString(type.name));
    root.add("version", doc.makeU64(type.version));
    Expected<JsonMutValue, ValueError> written = writeValue(doc, type, value);
    if (!written)
    {
        return unexpected("'" + written.error().pointer + "' " + written.error().message);
    }
    root.add("value", *written);
    doc.setRoot(root);
    std::string text;
    if (!doc.write(text))
    {
        return unexpected(std::string("cannot write the settings"));
    }
    text += '\n';
    return text;
}

Expected<void, std::string> readSettingsFile(const StructReflection& type, std::string_view json, void* value)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return unexpected(std::string("is not valid JSON"));
    }
    const JsonValue root = parsed->root();
    if (root.find("format").asString() != kSettingsFormat)
    {
        return unexpected("is not a settings file (\"format\" must be \"" + std::string(kSettingsFormat) +
                          "\")");
    }
    if (root.find("type").asString() != type.name)
    {
        return unexpected("holds other settings (\"type\" must be \"" + std::string(type.name) + "\")");
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > ~u32{0})
    {
        return unexpected(std::string("needs a \"version\" number"));
    }
    ValueReadOptions options;
    options.mode = JsonReadMode::Merge;
    options.savedVersion = static_cast<u32>(*version);
    const ValueReadResult read = readValue(type, value, root.find("value"), options);
    if (!read.ok())
    {
        return unexpected("'/value" + read.errors.front().pointer + "' " + read.errors.front().message);
    }
    return {};
}

} // namespace illusion::editor
