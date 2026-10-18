#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/text/LocaleFile.hpp>
#include <illusion/text/Utf8.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <format>

namespace illusion::text
{

namespace
{

Unexpected<LocaleError> fail(std::string message)
{
    return unexpected(LocaleError{0, 0, std::move(message)});
}

std::optional<u64> parseSourceHash(std::string_view text) noexcept
{
    if (text.size() != 16)
    {
        return std::nullopt;
    }
    for (const char c : text)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return std::nullopt;
        }
    }
    u64 value = 0;
    (void)std::from_chars(text.data(), text.data() + text.size(), value, 16); // the digits were checked above
    return value;
}

constexpr std::array<std::string_view, 10> kTopLevelFields = {"format",  "version", "locale",   "displayName",
                                                              "plural",  "number",  "fallback", "fonts",
                                                              "strings", "comment"};

bool isKnownField(std::string_view name) noexcept
{
    for (const std::string_view field : kTopLevelFields)
    {
        if (field == name)
        {
            return true;
        }
    }
    return false;
}

Expected<std::string_view, LocaleError> requiredString(JsonValue root, std::string_view field)
{
    const std::optional<std::string_view> value = root.find(field).asString();
    if (!value)
    {
        return fail(std::format("\"{}\" must be a string", field));
    }
    return *value;
}

Expected<void, LocaleError> parseEntry(std::string_view key, JsonValue value, LocaleEntry& entry)
{
    entry.key = key;
    if (const std::optional<std::string_view> text = value.asString())
    {
        entry.text = *text;
        return {};
    }
    if (!value.isObject())
    {
        return fail(std::format("string \"{}\" must be a string or an object", key));
    }
    for (const JsonMember member : value.members())
    {
        if (member.key == "text")
        {
            const std::optional<std::string_view> text = member.value.asString();
            if (!text)
            {
                return fail(std::format("\"text\" of string \"{}\" must be a string", key));
            }
            entry.text = *text;
        }
        else if (member.key == "comment")
        {
            const std::optional<std::string_view> comment = member.value.asString();
            if (!comment)
            {
                return fail(std::format("\"comment\" of string \"{}\" must be a string", key));
            }
            entry.comment = *comment;
        }
        else if (member.key == "source")
        {
            const std::optional<u64> hash = parseSourceHash(member.value.asString().value_or(""));
            if (!hash)
            {
                return fail(std::format("\"source\" of string \"{}\" must be 16 lowercase hex digits", key));
            }
            entry.source = hash;
        }
        else
        {
            return fail(std::format("string \"{}\" has an unknown field \"{}\"", key, member.key));
        }
    }
    if (value.find("text").isAbsent())
    {
        return fail(std::format("string \"{}\" has no \"text\"", key));
    }
    return {};
}

} // namespace

bool isValidTextKey(std::string_view key) noexcept
{
    usize position = 0;
    if (key.empty() || !(key[0] >= 'a' && key[0] <= 'z'))
    {
        return false;
    }
    // The first part: [a-z][a-z0-9_]*
    while (position < key.size() && key[position] != '.')
    {
        const char c = key[position];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        {
            return false;
        }
        ++position;
    }
    usize parts = 1;
    // Further parts: \.[A-Za-z0-9_]+
    while (position < key.size())
    {
        ++position; // the '.'
        const usize start = position;
        while (position < key.size() && key[position] != '.')
        {
            const char c = key[position];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            {
                return false;
            }
            ++position;
        }
        if (position == start)
        {
            return false;
        }
        ++parts;
    }
    return parts >= 2;
}

u64 sourceHash(std::string_view english) noexcept
{
    return xxhash64(std::as_bytes(std::span<const char>(english.data(), english.size())));
}

std::string sourceHashText(u64 hash)
{
    return std::format("{:016x}", hash);
}

Expected<LocaleFile, LocaleError> LocaleFile::parse(std::string_view json)
{
    auto parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        const JsonError& error = parsed.error();
        return unexpected(LocaleError{error.line, error.column, error.message});
    }
    const JsonValue root = parsed->root();
    if (!root.isObject())
    {
        return fail("a locale file must be a JSON object");
    }
    for (const JsonMember member : root.members())
    {
        if (!isKnownField(member.key))
        {
            return fail(std::format("unknown field \"{}\"", member.key));
        }
    }
    if (root.find("format").asString() != std::optional<std::string_view>("illusion.locale"))
    {
        return fail("\"format\" must be \"illusion.locale\"");
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0)
    {
        return fail("\"version\" must be a positive integer");
    }
    if (*version > kVersion)
    {
        return fail(std::format("version {} is newer than this build supports ({})", *version, kVersion));
    }

    LocaleFile file;
    const auto localeTag = requiredString(root, "locale");
    if (!localeTag)
    {
        return unexpected(localeTag.error());
    }
    const std::optional<LocaleId> locale = LocaleId::parse(*localeTag);
    if (!locale)
    {
        return fail(std::format("\"{}\" is not a valid locale", *localeTag));
    }
    file.locale = *locale;

    const auto displayName = requiredString(root, "displayName");
    if (!displayName)
    {
        return unexpected(displayName.error());
    }
    file.displayName = *displayName;

    const auto plural = requiredString(root, "plural");
    if (!plural)
    {
        return unexpected(plural.error());
    }
    const std::optional<PluralRule> rule = pluralRuleForLanguage(*plural);
    if (!rule)
    {
        return fail(std::format("\"{}\" has no built-in plural rule", *plural));
    }
    file.plural = *rule;
    file.pluralName = *plural;

    if (const JsonValue number = root.find("number"); !number.isAbsent())
    {
        const std::optional<std::string_view> decimal = number.find("decimal").asString();
        const std::optional<std::string_view> group = number.find("group").asString();
        if (!number.isObject() || number.size() != 2 || !decimal || !group)
        {
            return fail("\"number\" must be an object with the strings \"decimal\" and \"group\"");
        }
        if (decimal->empty() || decimal->size() > NumberFormat::kMaxSeparatorBytes ||
            group->size() > NumberFormat::kMaxSeparatorBytes)
        {
            return fail(
                std::format("number separators must have 1 to {} bytes (the group separator may be empty)",
                            NumberFormat::kMaxSeparatorBytes));
        }
        file.number = NumberFormat(*decimal, *group);
    }

    if (const JsonValue fallback = root.find("fallback"); !fallback.isAbsent())
    {
        const std::optional<LocaleId> fallbackLocale = LocaleId::parse(fallback.asString().value_or(""));
        if (!fallbackLocale)
        {
            return fail("\"fallback\" must be a valid locale");
        }
        file.fallback = fallbackLocale;
    }

    if (const JsonValue fonts = root.find("fonts"); !fonts.isAbsent())
    {
        if (!fonts.isArray())
        {
            return fail("\"fonts\" must be an array of strings");
        }
        for (const JsonValue font : fonts.elements())
        {
            const std::optional<std::string_view> name = font.asString();
            if (!name || name->empty())
            {
                return fail("\"fonts\" must be an array of strings");
            }
            file.fonts.emplace_back(*name);
        }
    }

    const JsonValue strings = root.find("strings");
    if (!strings.isObject())
    {
        return fail("\"strings\" must be an object");
    }
    file.entries.reserve(strings.size());
    for (const JsonMember member : strings.members())
    {
        if (!isValidTextKey(member.key))
        {
            return fail(std::format(
                "\"{}\" is not a valid key (lowercase dot-separated names, at least two parts)", member.key));
        }
        LocaleEntry entry;
        if (auto valid = parseEntry(member.key, member.value, entry); !valid)
        {
            return unexpected(std::move(valid).error());
        }
        file.entries.push_back(std::move(entry));
    }
    return file;
}

} // namespace illusion::text
