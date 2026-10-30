#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/SettingsFile.hpp>

namespace illusion
{

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

} // namespace illusion
