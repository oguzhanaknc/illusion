#pragma once

/// @file
/// UTF-8 validation, decoding, encoding and iteration by code point.
///
/// ```cpp
/// if (!text::isValidUtf8(name)) { ... }
/// for (const char32_t c : text::Utf8View(name)) { ... }   // invalid bytes decode as U+FFFD
/// std::string out;
/// text::appendUtf8(out, U'ş');
/// ```
///
/// Valid UTF-8 here is exactly what the JSON reader accepts: no overlong forms, no surrogates (U+D800 to
/// U+DFFF) and nothing above U+10FFFF.

#include <illusion/core/Base.hpp>

#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace illusion::text
{

/// The replacement character, decoded in place of invalid bytes.
inline constexpr char32_t kReplacementCharacter = U'�';

/// A decoded code point and the number of bytes it took.
struct Utf8Decoded
{
    char32_t codePoint = kReplacementCharacter; ///< The code point, or U+FFFD for invalid input.
    u32 length = 0;                             ///< Bytes consumed: 1 to 4 (1 for an invalid byte).
    bool valid = false;                         ///< False if the bytes were not a valid sequence.
};

/// Decodes the code point that starts at byte @p offset of @p text. An invalid or truncated sequence
/// decodes as U+FFFD with length 1, so that iteration always advances. @p offset must be less than the
/// size of @p text.
[[nodiscard]] Utf8Decoded decodeUtf8(std::string_view text, usize offset) noexcept;

/// Byte offset of the first invalid sequence in @p text, or nullopt if @p text is valid UTF-8.
[[nodiscard]] std::optional<usize> findInvalidUtf8(std::string_view text) noexcept;

/// True if @p text is valid UTF-8.
[[nodiscard]] inline bool isValidUtf8(std::string_view text) noexcept
{
    return !findInvalidUtf8(text).has_value();
}

/// Writes @p codePoint as UTF-8 to @p out and returns the number of bytes (1 to 4), or 0 for a surrogate or
/// a value above U+10FFFF (nothing is written then).
u32 encodeUtf8(char32_t codePoint, char (&out)[4]) noexcept;

/// Appends @p codePoint as UTF-8 to @p out. Surrogates and values above U+10FFFF append U+FFFD.
void appendUtf8(std::string& out, char32_t codePoint);

/// Number of code points in @p text (an invalid byte counts as one).
[[nodiscard]] usize utf8Length(std::string_view text) noexcept;

/// Iterates the code points of a UTF-8 string. Invalid bytes decode as U+FFFD, one per byte. The view does
/// not own the text.
class Utf8View
{
public:
    /// Forward iterator over code points.
    class Iterator
    {
    public:
        using value_type = char32_t;                        ///< Element type.
        using difference_type = std::ptrdiff_t;             ///< Distance type.
        using iterator_concept = std::forward_iterator_tag; ///< C++20 iterator concept.
        using iterator_category = std::input_iterator_tag;  ///< Legacy category (values are computed).

        /// Creates an end iterator for an empty text.
        Iterator() noexcept = default;

        /// The current code point.
        [[nodiscard]] char32_t operator*() const noexcept { return m_current.codePoint; }

        /// Byte offset of the current code point in the text.
        [[nodiscard]] usize offset() const noexcept { return m_offset; }

        /// Advances to the next code point.
        Iterator& operator++() noexcept
        {
            m_offset += m_current.length;
            decode();
            return *this;
        }

        /// Advances to the next code point and returns the previous position.
        Iterator operator++(int) noexcept
        {
            Iterator previous = *this;
            ++*this;
            return previous;
        }

        /// Compares positions.
        [[nodiscard]] friend bool operator==(const Iterator& a, const Iterator& b) noexcept
        {
            return a.m_offset == b.m_offset;
        }

    private:
        friend class Utf8View;
        Iterator(std::string_view text, usize offset) noexcept
            : m_text(text)
            , m_offset(offset)
        {
            decode();
        }

        void decode() noexcept
        {
            m_current = m_offset < m_text.size() ? decodeUtf8(m_text, m_offset) : Utf8Decoded{};
        }

        std::string_view m_text;
        usize m_offset = 0;
        Utf8Decoded m_current;
    };

    /// Creates a view of @p text.
    constexpr explicit Utf8View(std::string_view text) noexcept
        : m_text(text)
    {
    }

    /// First code point.
    [[nodiscard]] Iterator begin() const noexcept { return Iterator(m_text, 0); }
    /// Past the last code point.
    [[nodiscard]] Iterator end() const noexcept { return Iterator(m_text, m_text.size()); }

private:
    std::string_view m_text;
};

} // namespace illusion::text
