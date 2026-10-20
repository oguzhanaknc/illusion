#pragma once

/// @file
/// A subset of ICU MessageFormat for UI text: arguments, numbers, plurals and selects, compiled once into a
/// list of operations and formatted without parsing or throwing.
///
/// ```cpp
/// auto message = text::MessageFormat::compile("{count, plural, =0 {No entities} one {# entity} other {#
/// entities}}"); if (!message) { ILL_LOG_ERROR("Text", "{}", message.error().message); } std::string out;
/// text::TextOutput output(out);
/// (void)message->format({{"count", 3}}, text::MessageContext{}, output);   // "3 entities"
/// ```
///
/// Supported syntax:
/// - `{name}`: an argument (numbers use the locale's separators);
/// - `{name, number}`: a number;
/// - `{name, plural, =0 {...} one {...} other {...}}`: plural forms, where `#` is the number;
/// - `{name, select, male {...} female {...} other {...}}`: a choice by string value;
/// - apostrophes quote: `''` is one apostrophe, and `'{'`, `'}'` and (in plural forms) `'#'` are literal.
///
/// Sub-messages nest up to kMaxNesting levels. Argument names are ASCII identifiers.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/text/NumberFormat.hpp>
#include <illusion/text/PluralRules.hpp>

#include <concepts>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::text
{

class TextOutput;

/// A named value passed to MessageFormat::format(): an integer, a real number or a string.
class MessageArgument
{
public:
    /// The kind of value.
    enum class Kind : u8
    {
        Integer, ///< An integer (i64).
        Real,    ///< A real number (f64).
        String,  ///< A string.
    };

    /// An integer argument. Unsigned values above the range of i64 become real numbers.
    template <std::integral T>
        requires(!std::same_as<T, bool> && !std::same_as<T, char>)
    constexpr MessageArgument(std::string_view name, T value) noexcept
        : m_name(name)
    {
        if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(i64))
        {
            if (value > static_cast<T>(INT64_MAX))
            {
                m_kind = Kind::Real;
                m_real = static_cast<f64>(value);
                return;
            }
        }
        m_kind = Kind::Integer;
        m_integer = static_cast<i64>(value);
    }

    /// A real number argument.
    template <std::floating_point T>
    constexpr MessageArgument(std::string_view name, T value) noexcept
        : m_name(name)
        , m_kind(Kind::Real)
        , m_real(static_cast<f64>(value))
    {
    }

    /// A string argument; the string must outlive the format() call.
    constexpr MessageArgument(std::string_view name, std::string_view value) noexcept
        : m_name(name)
        , m_kind(Kind::String)
        , m_string(value)
    {
    }

    /// A string argument from a null-terminated string.
    constexpr MessageArgument(std::string_view name, const char* value) noexcept
        : MessageArgument(name, std::string_view(value))
    {
    }

    /// A string argument from a std::string, which must outlive the format() call.
    MessageArgument(std::string_view name, const std::string& value) noexcept
        : MessageArgument(name, std::string_view(value))
    {
    }

    /// The argument's name.
    [[nodiscard]] constexpr std::string_view name() const noexcept { return m_name; }
    /// The kind of value.
    [[nodiscard]] constexpr Kind kind() const noexcept { return m_kind; }
    /// The integer (valid for Kind::Integer).
    [[nodiscard]] constexpr i64 integer() const noexcept { return m_integer; }
    /// The real number (valid for Kind::Real).
    [[nodiscard]] constexpr f64 real() const noexcept { return m_real; }
    /// The string (valid for Kind::String).
    [[nodiscard]] constexpr std::string_view string() const noexcept { return m_string; }

private:
    std::string_view m_name;
    Kind m_kind = Kind::Integer;
    i64 m_integer = 0;
    f64 m_real = 0.0;
    std::string_view m_string;
};

/// Why a pattern does not compile, or what went wrong while formatting.
struct MessageError
{
    u32 offset = 0;      ///< Byte offset in the pattern (0 for format-time problems).
    std::string message; ///< Description in English.
};

/// Locale data used while formatting.
struct MessageContext
{
    PluralRule plural = PluralRule::English; ///< Plural rule of the language.
    NumberFormat number;                     ///< Number separators.
};

/// A compiled message. Copyable and movable; formatting is const and thread-safe.
class MessageFormat
{
public:
    /// Deepest nesting of plural and select sub-messages.
    static constexpr u32 kMaxNesting = 8;

    /// An empty message (formats to "").
    MessageFormat() = default;

    /// Compiles @p pattern (UTF-8). Returns the message, or the offset and description of the first
    /// problem. Never asserts, whatever the input.
    [[nodiscard]] static Expected<MessageFormat, MessageError> compile(std::string_view pattern);

    /// Appends the message to @p out with @p arguments. Formatting always writes the whole message: a
    /// missing argument is written as `{name}`, a string where a number is expected is written as it is
    /// (and selects plural form "other"), and the first such problem is returned. Does not allocate unless
    /// @p out does or a problem is reported.
    Expected<void, MessageError> format(std::span<const MessageArgument> arguments,
                                        const MessageContext& context, TextOutput& out) const;

    /// @copydoc format(std::span<const MessageArgument>, const MessageContext&, TextOutput&) const
    Expected<void, MessageError> format(std::initializer_list<MessageArgument> arguments,
                                        const MessageContext& context, TextOutput& out) const
    {
        return format(std::span<const MessageArgument>(arguments.begin(), arguments.size()), context, out);
    }

    /// Names of the arguments the message uses, sorted and without duplicates.
    [[nodiscard]] std::span<const std::string> argumentNames() const noexcept { return m_argumentNames; }

    /// For every plural argument, in pattern order: a bit mask of the categories it has a branch for (bit
    /// `1 << PluralCategory`). Exact `=N` branches are not included.
    [[nodiscard]] std::span<const u8> pluralCategoryMasks() const noexcept { return m_pluralMasks; }

    /// The message written back as a pattern in canonical form (equal for equivalent patterns).
    [[nodiscard]] std::string toPattern() const;

    /// A pseudo-localized copy for testing layouts: literal text gets accented letters, every sub-message
    /// is padded by about 35% with '~', and the message is wrapped in brackets. Arguments, numbers and
    /// plural structure stay intact.
    [[nodiscard]] MessageFormat pseudoLocalized() const;

private:
    // An operation of a compiled message.
    struct Op
    {
        enum class Kind : u8
        {
            Literal,  // text m_literals[a, a + b)
            Argument, // argument `argument`
            Number,   // argument `argument` as a number
            Pound,    // the number of the enclosing plural
            Plural,   // branches [a, a + b) of m_branches; the op after the nested bodies is c
            Select,   // as Plural
        };
        Kind kind = Kind::Literal;
        u16 argument = 0;
        u32 a = 0;
        u32 b = 0;
        u32 c = 0;
    };

    // A branch of a plural or select.
    struct Branch
    {
        enum class Kind : u8
        {
            Exact,    // =value
            Category, // a plural category
            Keyword,  // a select keyword m_literals[keyword, keyword + keywordLength)
        };
        Kind kind = Kind::Keyword;
        PluralCategory category = PluralCategory::Other;
        i64 exact = 0;
        u32 keyword = 0;
        u32 keywordLength = 0;
        u32 begin = 0; // first op of the body
        u32 end = 0;   // op after the body
    };

    class Compiler;
    class Formatter;
    class Writer;

    std::string m_literals; // unquoted literal text and select keywords
    std::vector<Op> m_ops;
    std::vector<Branch> m_branches;
    std::vector<std::string> m_argumentNames;
    std::vector<u8> m_pluralMasks;
};

} // namespace illusion::text
