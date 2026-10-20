#include <illusion/core/Assert.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/TextOutput.hpp>
#include <illusion/text/Utf8.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>

namespace illusion::text
{

namespace
{

constexpr bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

constexpr bool isNameStart(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

constexpr bool isNameChar(char c) noexcept
{
    return isNameStart(c) || (c >= '0' && c <= '9');
}

constexpr bool isKeywordChar(char c) noexcept
{
    return isNameChar(c) || c == '-';
}

constexpr usize kMaxArguments = 0xFFFF;

} // namespace

// --- Compiling --------------------------------------------------------------------------------------------

class MessageFormat::Compiler
{
public:
    Compiler(std::string_view pattern, MessageFormat& message) noexcept
        : m_pattern(pattern)
        , m_message(message)
    {
    }

    Expected<void, MessageError> run()
    {
        if (m_pattern.size() > 0x7FFFFFFFu)
        {
            return fail(0, "the pattern is too long");
        }
        if (const std::optional<usize> invalid = findInvalidUtf8(m_pattern))
        {
            return fail(*invalid, "the pattern is not valid UTF-8");
        }
        usize position = 0;
        if (!parseBody(position, false, 0, true))
        {
            return unexpected(std::move(*m_error));
        }
        sortArgumentNames();
        return {};
    }

private:
    Unexpected<MessageError> fail(usize offset, std::string message)
    {
        m_error = MessageError{static_cast<u32>(offset), std::move(message)};
        return unexpected(*m_error);
    }

    // Records the first error and returns false, so that parsing unwinds.
    bool error(usize offset, std::string message)
    {
        if (!m_error)
        {
            m_error = MessageError{static_cast<u32>(offset), std::move(message)};
        }
        return false;
    }

    void skipSpaces(usize& position) const noexcept
    {
        while (position < m_pattern.size() && isSpace(m_pattern[position]))
        {
            ++position;
        }
    }

    void flushLiteral(std::string& pending)
    {
        if (pending.empty())
        {
            return;
        }
        Op op;
        op.kind = Op::Kind::Literal;
        op.a = static_cast<u32>(m_message.m_literals.size());
        op.b = static_cast<u32>(pending.size());
        m_message.m_literals += pending;
        m_message.m_ops.push_back(op);
        pending.clear();
    }

    // Parses text and placeholders up to the end of the pattern (top level) or up to the '}' that closes
    // a sub-message, where it stops. `inPlural` makes '#' the plural number.
    bool parseBody(usize& position, bool inPlural, u32 depth, bool topLevel)
    {
        std::string pending;
        const usize size = m_pattern.size();
        while (position < size)
        {
            const char c = m_pattern[position];
            if (c == '\'')
            {
                if (position + 1 < size && m_pattern[position + 1] == '\'')
                {
                    pending += '\'';
                    position += 2;
                    continue;
                }
                const bool quotes = position + 1 < size &&
                                    (m_pattern[position + 1] == '{' || m_pattern[position + 1] == '}' ||
                                     (inPlural && m_pattern[position + 1] == '#'));
                if (!quotes)
                {
                    pending += '\'';
                    ++position;
                    continue;
                }
                const usize start = position++;
                for (;;)
                {
                    if (position >= size)
                    {
                        return error(start, "a quoted literal is not closed with an apostrophe");
                    }
                    if (m_pattern[position] == '\'')
                    {
                        if (position + 1 < size && m_pattern[position + 1] == '\'')
                        {
                            pending += '\'';
                            position += 2;
                            continue;
                        }
                        ++position;
                        break;
                    }
                    pending += m_pattern[position++];
                }
                continue;
            }
            if (c == '{')
            {
                flushLiteral(pending);
                if (!parsePlaceholder(position, inPlural, depth))
                {
                    return false;
                }
                continue;
            }
            if (c == '}')
            {
                if (topLevel)
                {
                    return error(position, "unmatched '}'");
                }
                flushLiteral(pending);
                return true;
            }
            if (c == '#' && inPlural)
            {
                flushLiteral(pending);
                Op op;
                op.kind = Op::Kind::Pound;
                m_message.m_ops.push_back(op);
                ++position;
                continue;
            }
            pending += c;
            ++position;
        }
        if (!topLevel)
        {
            return error(size, "a sub-message is not closed with '}'");
        }
        flushLiteral(pending);
        return true;
    }

    std::optional<u16> internName(std::string_view name)
    {
        std::vector<std::string>& names = m_message.m_argumentNames;
        for (usize i = 0; i < names.size(); ++i)
        {
            if (names[i] == name)
            {
                return static_cast<u16>(i);
            }
        }
        if (names.size() >= kMaxArguments)
        {
            return std::nullopt;
        }
        names.emplace_back(name);
        return static_cast<u16>(names.size() - 1);
    }

    std::string_view readWhile(usize& position, bool (*accept)(char) noexcept) const noexcept
    {
        const usize start = position;
        while (position < m_pattern.size() && accept(m_pattern[position]))
        {
            ++position;
        }
        return m_pattern.substr(start, position - start);
    }

    // Parses a placeholder starting at '{'.
    bool parsePlaceholder(usize& position, bool inPlural, u32 depth)
    {
        const usize open = position++;
        skipSpaces(position);
        if (position >= m_pattern.size() || !isNameStart(m_pattern[position]))
        {
            return error(position, "expected an argument name");
        }
        const std::string_view name = readWhile(position, &isNameChar);
        const std::optional<u16> argument = internName(name);
        if (!argument)
        {
            return error(open, "too many arguments");
        }
        skipSpaces(position);
        if (position >= m_pattern.size())
        {
            return error(open, "a placeholder is not closed with '}'");
        }
        Op op;
        op.argument = *argument;
        if (m_pattern[position] == '}')
        {
            op.kind = Op::Kind::Argument;
            m_message.m_ops.push_back(op);
            ++position;
            return true;
        }
        if (m_pattern[position] != ',')
        {
            return error(position, "expected ',' or '}' after the argument name");
        }
        ++position;
        skipSpaces(position);
        const usize typeStart = position;
        const std::string_view type = readWhile(position, &isNameChar);
        skipSpaces(position);
        if (type == "number")
        {
            if (position < m_pattern.size() && m_pattern[position] == ',')
            {
                return error(position, "number styles are not supported");
            }
            if (position >= m_pattern.size() || m_pattern[position] != '}')
            {
                return error(position, "expected '}' after 'number'");
            }
            op.kind = Op::Kind::Number;
            m_message.m_ops.push_back(op);
            ++position;
            return true;
        }
        if (type != "plural" && type != "select")
        {
            return error(typeStart, std::format("unknown argument type '{}'", type));
        }
        if (position >= m_pattern.size() || m_pattern[position] != ',')
        {
            return error(position, std::format("expected ',' after '{}'", type));
        }
        ++position;
        if (depth + 1 > kMaxNesting)
        {
            return error(open, std::format("sub-messages are nested deeper than {} levels", kMaxNesting));
        }
        return parseChoice(position, open, type == "plural", inPlural, depth, op);
    }

    // Parses the branches of a plural or select after its second ','.
    bool parseChoice(usize& position, usize open, bool plural, bool inPlural, u32 depth, Op op)
    {
        op.kind = plural ? Op::Kind::Plural : Op::Kind::Select;
        const usize opIndex = m_message.m_ops.size();
        m_message.m_ops.push_back(op);
        // Masks are listed in pattern order, so the slot is taken before nested plurals add theirs.
        const usize maskIndex = m_message.m_pluralMasks.size();
        if (plural)
        {
            m_message.m_pluralMasks.push_back(0);
        }

        std::vector<Branch> branches;
        bool hasOther = false;
        for (;;)
        {
            skipSpaces(position);
            if (position >= m_pattern.size())
            {
                return error(open,
                             plural ? "a plural is not closed with '}'" : "a select is not closed with '}'");
            }
            if (m_pattern[position] == '}')
            {
                ++position;
                break;
            }
            const usize selectorStart = position;
            Branch branch;
            if (plural && m_pattern[position] == '=')
            {
                ++position;
                const usize digitsStart = position;
                if (position < m_pattern.size() && m_pattern[position] == '-')
                {
                    ++position;
                }
                while (position < m_pattern.size() && m_pattern[position] >= '0' &&
                       m_pattern[position] <= '9')
                {
                    ++position;
                }
                const std::string_view digits = m_pattern.substr(digitsStart, position - digitsStart);
                i64 value = 0;
                const auto [end, failure] =
                    std::from_chars(digits.data(), digits.data() + digits.size(), value);
                if (digits.empty() || failure != std::errc() || end != digits.data() + digits.size())
                {
                    return error(selectorStart, "expected an integer after '='");
                }
                branch.kind = Branch::Kind::Exact;
                branch.exact = value;
            }
            else
            {
                const std::string_view keyword = readWhile(position, &isKeywordChar);
                if (keyword.empty())
                {
                    return error(position, "expected a selector");
                }
                if (plural)
                {
                    const std::optional<PluralCategory> category = pluralCategoryFromName(keyword);
                    if (!category)
                    {
                        return error(selectorStart,
                                     keyword == "offset"
                                         ? std::string("plural offsets are not supported")
                                         : std::format("'{}' is not a plural category", keyword));
                    }
                    branch.kind = Branch::Kind::Category;
                    branch.category = *category;
                    hasOther = hasOther || *category == PluralCategory::Other;
                }
                else
                {
                    branch.kind = Branch::Kind::Keyword;
                    branch.keyword = static_cast<u32>(m_message.m_literals.size());
                    branch.keywordLength = static_cast<u32>(keyword.size());
                    m_message.m_literals += keyword;
                    hasOther = hasOther || keyword == "other";
                }
            }
            if (isDuplicate(branches, branch))
            {
                return error(selectorStart,
                             std::format("duplicate selector '{}'",
                                         m_pattern.substr(selectorStart, position - selectorStart)));
            }
            skipSpaces(position);
            if (position >= m_pattern.size() || m_pattern[position] != '{')
            {
                return error(position, "expected '{' after the selector");
            }
            ++position;
            branch.begin = static_cast<u32>(m_message.m_ops.size());
            if (!parseBody(position, inPlural || plural, depth + 1, false))
            {
                return false;
            }
            ++position; // the closing '}'
            branch.end = static_cast<u32>(m_message.m_ops.size());
            branches.push_back(branch);
        }
        if (!hasOther)
        {
            return error(open,
                         plural ? "a plural needs an 'other' form" : "a select needs an 'other' branch");
        }

        Op& choice = m_message.m_ops[opIndex];
        choice.a = static_cast<u32>(m_message.m_branches.size());
        choice.b = static_cast<u32>(branches.size());
        choice.c = static_cast<u32>(m_message.m_ops.size());
        if (plural)
        {
            u8 mask = 0;
            for (const Branch& branch : branches)
            {
                if (branch.kind == Branch::Kind::Category)
                {
                    mask = static_cast<u8>(mask | (1u << static_cast<u32>(branch.category)));
                }
            }
            m_message.m_pluralMasks[maskIndex] = mask;
        }
        m_message.m_branches.insert(m_message.m_branches.end(), branches.begin(), branches.end());
        return true;
    }

    bool isDuplicate(const std::vector<Branch>& branches, const Branch& branch) const noexcept
    {
        const std::string_view literals = m_message.m_literals;
        for (const Branch& other : branches)
        {
            if (other.kind != branch.kind)
            {
                continue;
            }
            switch (branch.kind)
            {
            case Branch::Kind::Exact:
                if (other.exact == branch.exact)
                {
                    return true;
                }
                break;
            case Branch::Kind::Category:
                if (other.category == branch.category)
                {
                    return true;
                }
                break;
            case Branch::Kind::Keyword:
                if (literals.substr(other.keyword, other.keywordLength) ==
                    literals.substr(branch.keyword, branch.keywordLength))
                {
                    return true;
                }
                break;
            }
        }
        return false;
    }

    // Sorts the argument names and renumbers the operations that refer to them.
    void sortArgumentNames()
    {
        std::vector<std::string>& names = m_message.m_argumentNames;
        std::vector<u16> order(names.size());
        for (usize i = 0; i < order.size(); ++i)
        {
            order[i] = static_cast<u16>(i);
        }
        std::sort(order.begin(), order.end(), [&](u16 a, u16 b) { return names[a] < names[b]; });
        std::vector<u16> renumbered(names.size());
        std::vector<std::string> sorted;
        sorted.reserve(names.size());
        for (usize i = 0; i < order.size(); ++i)
        {
            renumbered[order[i]] = static_cast<u16>(i);
            sorted.push_back(std::move(names[order[i]]));
        }
        names = std::move(sorted);
        for (Op& op : m_message.m_ops)
        {
            if (op.kind != Op::Kind::Literal && op.kind != Op::Kind::Pound)
            {
                op.argument = renumbered[op.argument];
            }
        }
    }

    std::string_view m_pattern;
    MessageFormat& m_message;
    std::optional<MessageError> m_error;
};

Expected<MessageFormat, MessageError> MessageFormat::compile(std::string_view pattern)
{
    MessageFormat message;
    if (auto compiled = Compiler(pattern, message).run(); !compiled)
    {
        return unexpected(std::move(compiled).error());
    }
    return message;
}

// --- Formatting -------------------------------------------------------------------------------------------

class MessageFormat::Formatter
{
public:
    Formatter(const MessageFormat& message, std::span<const MessageArgument> arguments,
              const MessageContext& context, TextOutput& out) noexcept
        : m_message(message)
        , m_arguments(arguments)
        , m_context(context)
        , m_out(out)
    {
    }

    Expected<void, MessageError> run()
    {
        formatRange(0, static_cast<u32>(m_message.m_ops.size()), nullptr, 0);
        if (m_error)
        {
            return unexpected(std::move(*m_error));
        }
        return {};
    }

private:
    const MessageArgument* find(u16 argument) const noexcept
    {
        const std::string_view name = m_message.m_argumentNames[argument];
        for (const MessageArgument& candidate : m_arguments)
        {
            if (candidate.name() == name)
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    void problem(std::string_view what, u16 argument)
    {
        if (!m_error)
        {
            m_error =
                MessageError{0, std::format("argument '{}' {}", m_message.m_argumentNames[argument], what)};
        }
    }

    void writePlaceholder(u16 argument)
    {
        m_out.push('{');
        m_out.append(m_message.m_argumentNames[argument]);
        m_out.push('}');
    }

    void writeValue(const MessageArgument& value)
    {
        switch (value.kind())
        {
        case MessageArgument::Kind::Integer:
            formatNumber(value.integer(), m_context.number, m_out);
            break;
        case MessageArgument::Kind::Real:
            formatNumber(value.real(), m_context.number, m_out);
            break;
        case MessageArgument::Kind::String:
            m_out.append(value.string());
            break;
        }
    }

    const Branch* choosePlural(const Op& op, const MessageArgument* value) const noexcept
    {
        const std::span<const Branch> branches(m_message.m_branches.data() + op.a, op.b);
        PluralCategory category = PluralCategory::Other;
        if (value != nullptr && value->kind() != MessageArgument::Kind::String)
        {
            const bool integer = value->kind() == MessageArgument::Kind::Integer;
            for (const Branch& branch : branches)
            {
                if (branch.kind == Branch::Kind::Exact &&
                    (integer ? value->integer() == branch.exact
                             : value->real() == static_cast<f64>(branch.exact)))
                {
                    return &branch;
                }
            }
            category =
                pluralCategory(m_context.plural, integer ? PluralOperands::fromInteger(value->integer())
                                                         : PluralOperands::fromNumber(value->real()));
        }
        const Branch* other = nullptr;
        for (const Branch& branch : branches)
        {
            if (branch.kind == Branch::Kind::Category)
            {
                if (branch.category == category)
                {
                    return &branch;
                }
                if (branch.category == PluralCategory::Other)
                {
                    other = &branch;
                }
            }
        }
        return other;
    }

    const Branch* chooseSelect(const Op& op, const MessageArgument* value) const noexcept
    {
        const std::string_view literals = m_message.m_literals;
        const std::span<const Branch> branches(m_message.m_branches.data() + op.a, op.b);
        const Branch* other = nullptr;
        for (const Branch& branch : branches)
        {
            const std::string_view keyword = literals.substr(branch.keyword, branch.keywordLength);
            if (value != nullptr && value->kind() == MessageArgument::Kind::String &&
                keyword == value->string())
            {
                return &branch;
            }
            if (keyword == "other")
            {
                other = &branch;
            }
        }
        return other;
    }

    // `plural` is the argument of the innermost plural (nullptr outside plurals), `pluralName` its name.
    void formatRange(u32 begin, u32 end, const MessageArgument* plural, u16 pluralName)
    {
        const std::string_view literals = m_message.m_literals;
        for (u32 i = begin; i < end; ++i)
        {
            const Op& op = m_message.m_ops[i];
            switch (op.kind)
            {
            case Op::Kind::Literal:
                m_out.append(literals.substr(op.a, op.b));
                break;
            case Op::Kind::Argument:
            case Op::Kind::Number:
                if (const MessageArgument* value = find(op.argument))
                {
                    if (op.kind == Op::Kind::Number && value->kind() == MessageArgument::Kind::String)
                    {
                        problem("is not a number", op.argument);
                    }
                    writeValue(*value);
                }
                else
                {
                    problem("is missing", op.argument);
                    writePlaceholder(op.argument);
                }
                break;
            case Op::Kind::Pound:
                if (plural != nullptr)
                {
                    writeValue(*plural);
                }
                else
                {
                    writePlaceholder(pluralName);
                }
                break;
            case Op::Kind::Plural:
            case Op::Kind::Select:
            {
                const MessageArgument* value = find(op.argument);
                const bool isPlural = op.kind == Op::Kind::Plural;
                if (value == nullptr)
                {
                    problem("is missing", op.argument);
                }
                else if (isPlural && value->kind() == MessageArgument::Kind::String)
                {
                    problem("is not a number", op.argument);
                }
                else if (!isPlural && value->kind() != MessageArgument::Kind::String)
                {
                    problem("is not a string", op.argument);
                }
                const Branch* branch = isPlural ? choosePlural(op, value) : chooseSelect(op, value);
                ILL_ASSERT(branch != nullptr, "a compiled choice has no 'other' branch");
                if (isPlural)
                {
                    formatRange(branch->begin, branch->end, value, op.argument);
                }
                else
                {
                    formatRange(branch->begin, branch->end, plural, pluralName);
                }
                i = op.c - 1; // skip the nested bodies
                break;
            }
            }
        }
    }

    const MessageFormat& m_message;
    std::span<const MessageArgument> m_arguments;
    const MessageContext& m_context;
    TextOutput& m_out;
    std::optional<MessageError> m_error;
};

Expected<void, MessageError> MessageFormat::format(std::span<const MessageArgument> arguments,
                                                   const MessageContext& context, TextOutput& out) const
{
    ILL_PROFILE_SCOPE();
    return Formatter(*this, arguments, context, out).run();
}

// --- Writing patterns -------------------------------------------------------------------------------------

namespace
{

// Accented forms for pseudo-localization, from Latin-1 Supplement and Latin Extended-A only, so that every
// editor font can show them. Letters without such a form stay as they are.
constexpr std::array<std::string_view, 26> kPseudoLower = {
    "\xC3\xA1", "b",        "\xC3\xA7", "\xC4\x8F", "\xC3\xA9", "f",        "\xC4\x9F",
    "\xC4\xA5", "\xC3\xAD", "\xC4\xB5", "\xC4\xB7", "\xC4\xBE", "m",        "\xC3\xB1",
    "\xC3\xB6", "p",        "q",        "\xC5\x99", "\xC5\xA1", "\xC5\xA5", "\xC3\xBC",
    "v",        "\xC5\xB5", "x",        "\xC3\xBD", "\xC5\xBE"};
constexpr std::array<std::string_view, 26> kPseudoUpper = {
    "\xC3\x85", "B",        "\xC3\x87", "\xC4\x8E", "\xC3\x89", "F",        "\xC4\x9E",
    "\xC4\xA4", "\xC3\x8D", "\xC4\xB4", "\xC4\xB6", "\xC4\xBD", "M",        "\xC3\x91",
    "\xC3\x96", "P",        "Q",        "\xC5\x98", "\xC5\xA0", "\xC5\xA4", "\xC3\x9C",
    "V",        "\xC5\xB4", "X",        "\xC3\x9D", "\xC5\xBD"};

} // namespace

class MessageFormat::Writer
{
public:
    Writer(const MessageFormat& message, bool pseudo) noexcept
        : m_message(message)
        , m_pseudo(pseudo)
    {
    }

    std::string run()
    {
        std::string out;
        if (m_pseudo)
        {
            out += '[';
        }
        writeBody(out, 0, static_cast<u32>(m_message.m_ops.size()), false);
        if (m_pseudo)
        {
            out += ']';
        }
        return out;
    }

private:
    void writeLiteral(std::string& out, std::string_view text, bool inPlural) const
    {
        for (const char c : text)
        {
            if (c == '\'')
            {
                out += "''";
            }
            else if (c == '{' || c == '}' || (inPlural && c == '#'))
            {
                out += '\'';
                out += c;
                out += '\'';
            }
            else if (m_pseudo && c >= 'a' && c <= 'z')
            {
                out += kPseudoLower[static_cast<usize>(c - 'a')];
            }
            else if (m_pseudo && c >= 'A' && c <= 'Z')
            {
                out += kPseudoUpper[static_cast<usize>(c - 'A')];
            }
            else
            {
                out += c;
            }
        }
    }

    void writeBody(std::string& out, u32 begin, u32 end, bool inPlural) const
    {
        const std::string_view literals = m_message.m_literals;
        usize literalCodePoints = 0;
        for (u32 i = begin; i < end; ++i)
        {
            const Op& op = m_message.m_ops[i];
            const std::string_view name = op.kind == Op::Kind::Literal || op.kind == Op::Kind::Pound
                                              ? std::string_view()
                                              : m_message.m_argumentNames[op.argument];
            switch (op.kind)
            {
            case Op::Kind::Literal:
                writeLiteral(out, literals.substr(op.a, op.b), inPlural);
                literalCodePoints += utf8Length(literals.substr(op.a, op.b));
                break;
            case Op::Kind::Argument:
                out += '{';
                out += name;
                out += '}';
                break;
            case Op::Kind::Number:
                out += '{';
                out += name;
                out += ", number}";
                break;
            case Op::Kind::Pound:
                out += '#';
                break;
            case Op::Kind::Plural:
            case Op::Kind::Select:
            {
                const bool plural = op.kind == Op::Kind::Plural;
                out += '{';
                out += name;
                out += plural ? ", plural," : ", select,";
                for (u32 b = op.a; b < op.a + op.b; ++b)
                {
                    const Branch& branch = m_message.m_branches[b];
                    out += ' ';
                    switch (branch.kind)
                    {
                    case Branch::Kind::Exact:
                        out += '=';
                        out += std::to_string(branch.exact);
                        break;
                    case Branch::Kind::Category:
                        out += pluralCategoryName(branch.category);
                        break;
                    case Branch::Kind::Keyword:
                        out += literals.substr(branch.keyword, branch.keywordLength);
                        break;
                    }
                    out += " {";
                    writeBody(out, branch.begin, branch.end, inPlural || plural);
                    out += '}';
                }
                out += '}';
                i = op.c - 1;
                break;
            }
            }
        }
        if (m_pseudo)
        {
            // About 35%, the expansion that translations need on average.
            out.append((literalCodePoints * 35 + 50) / 100, '~');
        }
    }

    const MessageFormat& m_message;
    bool m_pseudo;
};

std::string MessageFormat::toPattern() const
{
    return Writer(*this, false).run();
}

MessageFormat MessageFormat::pseudoLocalized() const
{
    auto pseudo = compile(Writer(*this, true).run());
    ILL_ASSERT(pseudo.hasValue(), "a pseudo-localized pattern did not compile: {}", pseudo.error().message);
    return pseudo.hasValue() ? std::move(pseudo).value() : *this;
}

} // namespace illusion::text
