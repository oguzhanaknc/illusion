#pragma once

/// @file
/// Destination of formatted text: a std::string, or memory from a LinearAllocator so that per-frame UI
/// text never touches the heap.
///
/// ```cpp
/// LinearAllocator& frame = frameArena();
/// text::TextOutput out(frame);
/// localization.format(text::kOutlinerSelected, {{"count", selected}}, out);
/// ui::label(out.view());
/// ```

#include <illusion/core/Base.hpp>

#include <string>
#include <string_view>

namespace illusion
{
class LinearAllocator;
} // namespace illusion

namespace illusion::text
{

/// Collects formatted text. Writing into a std::string appends to it; writing into an arena builds the
/// text in one block that grows in place while it is the arena's newest allocation (otherwise it moves,
/// and the old bytes stay used until the arena is reset). Not thread-safe; cheap to create.
class TextOutput
{
public:
    /// Appends to @p out, which must outlive this output.
    explicit TextOutput(std::string& out) noexcept;

    /// Builds the text in memory from @p arena, which must outlive the text. A full arena stops the
    /// output (see failed()).
    explicit TextOutput(LinearAllocator& arena) noexcept;

    TextOutput(const TextOutput&) = delete;            ///< Not copyable.
    TextOutput& operator=(const TextOutput&) = delete; ///< Not copyable.

    /// Appends @p text. Ignored after the arena ran out.
    void append(std::string_view text);

    /// Appends one byte.
    void push(char c) { append(std::string_view(&c, 1)); }

    /// The text written through this output (for a std::string: the part appended since construction).
    [[nodiscard]] std::string_view view() const noexcept;

    /// Bytes written through this output.
    [[nodiscard]] usize size() const noexcept;

    /// True if the arena ran out; the text is then cut short (possibly inside a UTF-8 character).
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

private:
    std::string* m_string = nullptr;
    usize m_start = 0;
    LinearAllocator* m_arena = nullptr;
    char* m_data = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
    bool m_failed = false;
};

} // namespace illusion::text
