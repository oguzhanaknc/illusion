#pragma once

/// @file
/// Fuzzy matching for command palettes and search boxes: the pattern's characters must appear in the
/// candidate in order, and matches at word starts, in runs and early in the candidate score higher.
///
/// ```cpp
/// text::FuzzyMatcher matcher("sv sc");
/// if (const auto score = matcher.score("Save Scene")) { ... }   // higher is better
/// ```
///
/// Both strings are compared after searchFold(), so case and diacritics are ignored. Spaces in the
/// pattern are ignored as well; they only let the user type word starts.

#include <illusion/core/Base.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace illusion::text
{

/// Scores candidates against one pattern. Create one matcher per query and reuse it for every candidate.
/// Not thread-safe (it keeps scratch memory, so scoring does not allocate once that has grown).
class FuzzyMatcher
{
public:
    /// Longest pattern, in code points after folding; longer patterns are cut.
    static constexpr usize kMaxPatternLength = 64;

    /// Prepares @p pattern (UTF-8). An empty pattern matches every candidate with score 0.
    explicit FuzzyMatcher(std::string_view pattern);
    ~FuzzyMatcher();

    FuzzyMatcher(FuzzyMatcher&&) noexcept;                 ///< Movable.
    FuzzyMatcher& operator=(FuzzyMatcher&&) noexcept;      ///< Movable.
    FuzzyMatcher(const FuzzyMatcher&) = delete;            ///< Not copyable.
    FuzzyMatcher& operator=(const FuzzyMatcher&) = delete; ///< Not copyable.

    /// Score of @p candidate (UTF-8), or nullopt if the pattern is not a subsequence of it. Scores are
    /// only comparable for the same pattern; sort equal scores by candidate length or name.
    [[nodiscard]] std::optional<i32> score(std::string_view candidate);

    /// Number of code points in the folded pattern (spaces excluded).
    [[nodiscard]] usize patternLength() const noexcept { return m_patternLength; }

private:
    void reserve(usize length);

    char32_t m_pattern[kMaxPatternLength] = {};
    usize m_patternLength = 0;
    std::unique_ptr<char32_t[]> m_text; // folded candidate
    std::unique_ptr<i8[]> m_bonus;      // bonus of a match at each position
    std::unique_ptr<i32[]> m_rows;      // two rows of scores
    usize m_capacity = 0;
};

} // namespace illusion::text
