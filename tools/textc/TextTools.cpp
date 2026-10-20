#include "TextTools.hpp"

#include <illusion/text/MessageFormat.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <span>

namespace illusion::text::tools
{

namespace
{

void add(CheckReport& report, Severity severity, std::string_view file, std::string_view key, std::string message)
{
    report.diagnostics.push_back(Diagnostic{severity, std::string(file), std::string(key), std::move(message)});
}

std::string joined(std::span<const std::string> names)
{
    std::string text;
    for (const std::string& name : names)
    {
        text += text.empty() ? "" : ", ";
        text += name;
    }
    return text.empty() ? std::string("none") : text;
}

std::string categoryList(u8 mask)
{
    std::string text;
    for (usize i = 0; i < kPluralCategoryCount; ++i)
    {
        if ((mask & (1u << i)) != 0)
        {
            text += text.empty() ? "" : ", ";
            text += pluralCategoryName(static_cast<PluralCategory>(i));
        }
    }
    return text;
}

// Checks what every locale file must satisfy: sorted keys, patterns that compile, and every plural with all
// the forms of the file's plural rule. Returns the compiled messages by key.
std::map<std::string, MessageFormat, std::less<>> checkFile(const LocaleFile& locale, std::string_view file,
                                                            CheckReport& report)
{
    for (usize i = 1; i < locale.entries.size(); ++i)
    {
        if (locale.entries[i].key < locale.entries[i - 1].key)
        {
            add(report, Severity::Error, file, locale.entries[i].key,
                std::format("keys are not sorted: it comes after \"{}\"", locale.entries[i - 1].key));
            break;
        }
    }

    u8 required = 0;
    for (const PluralCategory category : pluralCategories(locale.plural))
    {
        required = static_cast<u8>(required | (1u << static_cast<u32>(category)));
    }

    std::map<std::string, MessageFormat, std::less<>> messages;
    for (const LocaleEntry& entry : locale.entries)
    {
        auto compiled = MessageFormat::compile(entry.text);
        if (!compiled)
        {
            add(report, Severity::Error, file, entry.key,
                std::format("the pattern does not parse at offset {}: {}", compiled.error().offset,
                            compiled.error().message));
            continue;
        }
        for (const u8 mask : compiled->pluralCategoryMasks())
        {
            if (const u8 missing = static_cast<u8>(required & ~mask); missing != 0)
            {
                add(report, Severity::Error, file, entry.key,
                    std::format("a plural lacks the forms {} that plural rule \"{}\" needs", categoryList(missing),
                                locale.pluralName));
            }
        }
        messages.emplace(entry.key, std::move(compiled).value());
    }
    return messages;
}

bool isIdentifier(std::string_view name) noexcept
{
    if (name.empty() || !((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_'))
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool isNamespace(std::string_view name) noexcept
{
    usize start = 0;
    for (;;)
    {
        const usize separator = name.find("::", start);
        if (!isIdentifier(name.substr(start, separator == std::string_view::npos ? std::string_view::npos
                                                                                   : separator - start)))
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            return true;
        }
        start = separator + 2;
    }
}

// A C++ string literal holding @p text. Control characters use three-digit octal escapes, which, unlike hex
// escapes, cannot swallow the characters that follow.
std::string cppLiteral(std::string_view text)
{
    std::string literal = "\"";
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        switch (c)
        {
        case '\\':
            literal += "\\\\";
            break;
        case '"':
            literal += "\\\"";
            break;
        case '\n':
            literal += "\\n";
            break;
        case '\t':
            literal += "\\t";
            break;
        case '\r':
            literal += "\\r";
            break;
        default:
            if (byte < 0x20 || byte == 0x7F)
            {
                literal += std::format("\\{:03o}", byte);
            }
            else
            {
                literal += c;
            }
            break;
        }
    }
    literal += '"';
    return literal;
}

// @p text on one comment line.
std::string commentText(std::string_view text)
{
    std::string comment;
    for (const char c : text)
    {
        if (c == '\n')
        {
            comment += "\\n";
        }
        else if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
        {
            comment += c;
        }
    }
    return comment;
}

} // namespace

usize CheckReport::errorCount() const noexcept
{
    return static_cast<usize>(std::count_if(diagnostics.begin(), diagnostics.end(),
                                            [](const Diagnostic& d) { return d.severity == Severity::Error; }));
}

usize CheckReport::warningCount() const noexcept
{
    return diagnostics.size() - errorCount();
}

std::string CheckReport::format() const
{
    std::string text;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        text += diagnostic.file;
        text += diagnostic.severity == Severity::Error ? ": error: " : ": warning: ";
        if (!diagnostic.key.empty())
        {
            text += diagnostic.key;
            text += ": ";
        }
        text += diagnostic.message;
        text += '\n';
    }
    return text;
}

std::optional<LocaleFile> checkEnglish(std::string_view json, std::string_view file, CheckReport& report)
{
    auto parsed = LocaleFile::parse(json);
    if (!parsed)
    {
        const LocaleError& error = parsed.error();
        add(report, Severity::Error, file, "",
            error.line != 0 ? std::format("{}:{}: {}", error.line, error.column, error.message) : error.message);
        return std::nullopt;
    }
    if (parsed->locale.tag() != "en")
    {
        add(report, Severity::Error, file, "",
            std::format("the English source must have locale \"en\", not \"{}\"", parsed->locale.tag()));
    }
    (void)checkFile(*parsed, file, report);
    return std::move(parsed).value();
}

void checkTranslation(const LocaleFile& english, std::string_view json, std::string_view file,
                      const CheckOptions& options, CheckReport& report)
{
    auto parsed = LocaleFile::parse(json);
    if (!parsed)
    {
        const LocaleError& error = parsed.error();
        add(report, Severity::Error, file, "",
            error.line != 0 ? std::format("{}:{}: {}", error.line, error.column, error.message) : error.message);
        return;
    }
    const LocaleFile& translation = *parsed;
    const Severity soft = options.strict ? Severity::Error : Severity::Warning;
    const auto translated = checkFile(translation, file, report);

    std::map<std::string_view, const LocaleEntry*> englishEntries;
    for (const LocaleEntry& entry : english.entries)
    {
        englishEntries.emplace(entry.key, &entry);
    }
    std::set<std::string_view> present;
    for (const LocaleEntry& entry : translation.entries)
    {
        present.insert(entry.key);
        const auto source = englishEntries.find(entry.key);
        if (source == englishEntries.end())
        {
            add(report, Severity::Error, file, entry.key, "the key is not in the English source");
            continue;
        }
        const LocaleEntry& englishEntry = *source->second;
        const auto message = translated.find(entry.key);
        const auto englishMessage = MessageFormat::compile(englishEntry.text);
        if (message != translated.end() && englishMessage &&
            !std::ranges::equal(message->second.argumentNames(), englishMessage->argumentNames()))
        {
            add(report, Severity::Error, file, entry.key,
                std::format("the arguments ({}) differ from the English ones ({})",
                            joined(message->second.argumentNames()), joined(englishMessage->argumentNames())));
        }
        if (!entry.source)
        {
            add(report, soft, file, entry.key, "no \"source\" hash records which English text was translated");
        }
        else if (*entry.source != sourceHash(englishEntry.text))
        {
            add(report, soft, file, entry.key,
                std::format("stale translation: the English text changed (source hash {})",
                            sourceHashText(sourceHash(englishEntry.text))));
        }
    }
    for (const LocaleEntry& entry : english.entries)
    {
        if (!present.contains(entry.key))
        {
            add(report, soft, file, entry.key, "missing translation (English is shown)");
        }
    }
}

std::string constantName(std::string_view key)
{
    std::string name = "k";
    const usize firstDot = key.find('.');
    bool upper = true;
    for (usize i = firstDot == std::string_view::npos ? key.size() : firstDot + 1; i < key.size(); ++i)
    {
        const char c = key[i];
        if (c == '.' || c == '_')
        {
            upper = true;
            continue;
        }
        name += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        upper = false;
    }
    return name;
}

Expected<GeneratedTable, std::string> generateStringTable(std::string_view english, const GenerateOptions& options)
{
    CheckReport report;
    const std::optional<LocaleFile> file = checkEnglish(english, options.sourceName, report);
    if (report.errorCount() != 0 || !file)
    {
        return unexpected(report.format());
    }
    if (file->entries.empty())
    {
        return unexpected(std::format("{}: error: the table has no strings\n", options.sourceName));
    }
    if (!isIdentifier(options.table) || options.table.front() < 'a' || options.table.front() > 'z')
    {
        return unexpected(std::format("'{}' is not a valid table name (a lowercase identifier)", options.table));
    }
    if (!isNamespace(options.nameSpace))
    {
        return unexpected(std::format("'{}' is not a valid C++ namespace", options.nameSpace));
    }

    std::map<std::string, std::string> constants; // constant name -> key
    for (const LocaleEntry& entry : file->entries)
    {
        const std::string name = constantName(entry.key);
        if (const auto [it, inserted] = constants.emplace(name, entry.key); !inserted)
        {
            return unexpected(std::format("{}: error: \"{}\" and \"{}\" both become the constant {}\n",
                                          options.sourceName, it->second, entry.key, name));
        }
    }

    GeneratedTable generated;
    const std::string banner =
        std::format("// Generated by illusion_textc from {}. Do not edit.\n", options.sourceName);

    std::string& header = generated.header;
    header += banner;
    header += "#pragma once\n\n#include <illusion/text/StringTable.hpp>\n\n";
    header += std::format("namespace {}\n{{\n\n", options.nameSpace);
    header += std::format("/// The \"{}\" string table: {} keys with their English text.\n", options.table,
                          file->entries.size());
    header += "extern const ::illusion::text::StringTableData kTable;\n";
    for (usize i = 0; i < file->entries.size(); ++i)
    {
        const LocaleEntry& entry = file->entries[i];
        header += '\n';
        header += std::format("/// `{}`: \"{}\"", entry.key, commentText(entry.text));
        header += entry.comment.empty() ? std::string("\n") : std::format(" ({})\n", commentText(entry.comment));
        header += std::format("inline constexpr ::illusion::text::TextKey {}{{&kTable, {}}};\n",
                              constantName(entry.key), i);
    }
    header += std::format("\n}} // namespace {}\n", options.nameSpace);

    std::string& source = generated.source;
    source += banner;
    source += std::format("#include \"{}\"\n\n#include <string_view>\n\n", options.headerInclude);
    source += std::format("namespace {}\n{{\n\nnamespace\n{{\n\nconstexpr std::string_view kKeys[] = {{\n",
                          options.nameSpace);
    for (const LocaleEntry& entry : file->entries)
    {
        source += std::format("    {},\n", cppLiteral(entry.key));
    }
    source += "};\n\nconstexpr std::string_view kEnglish[] = {\n";
    for (const LocaleEntry& entry : file->entries)
    {
        source += std::format("    {},\n", cppLiteral(entry.text));
    }
    source += "};\n\n} // namespace\n\n";
    source += std::format("const ::illusion::text::StringTableData kTable{{\"{}\", kKeys, kEnglish}};\n",
                          options.table);
    source += std::format("\n}} // namespace {}\n", options.nameSpace);
    return generated;
}

} // namespace illusion::text::tools
