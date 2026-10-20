#include <illusion/core/Profiler.hpp>
#include <illusion/text/FuzzyMatcher.hpp>
#include <illusion/text/SearchFold.hpp>
#include <illusion/text/Utf8.hpp>

#include <algorithm>
#include <utility>

namespace illusion::text
{

namespace
{

// Scores modelled on fzf: a match is worth 16, gaps cost 3 to open and 1 per further character, and
// matches at word starts, camel-case humps and in runs earn bonuses. The first pattern character's bonus
// counts twice, so that a match at a word start ranks above one inside a word.
constexpr i32 kMatch = 16;
constexpr i32 kGapStart = -3;
constexpr i32 kGapExtension = -1;
constexpr i32 kBonusBoundary = 8;
constexpr i32 kBonusNonWord = 8;
constexpr i32 kBonusCamel = 7;
constexpr i32 kBonusConsecutive = -(kGapStart + kGapExtension);
constexpr i32 kFirstCharMultiplier = 2;
// Unmatched characters before the first match cost like a gap, but at most this much.
constexpr i32 kMaxLeadingPenalty = -6;
constexpr i32 kNone = -(1 << 28);

enum class CharClass : u8
{
    NonWord,
    Lower,
    Upper,
    Digit,
};

CharClass classify(char32_t c) noexcept
{
    if (c >= U'a' && c <= U'z')
    {
        return CharClass::Lower;
    }
    if (c >= U'A' && c <= U'Z')
    {
        return CharClass::Upper;
    }
    if (c >= U'0' && c <= U'9')
    {
        return CharClass::Digit;
    }
    // Other scripts count as lowercase letters; ASCII punctuation, spaces and controls separate words.
    return c < 0x80 ? CharClass::NonWord : CharClass::Lower;
}

i8 bonusAt(CharClass previous, CharClass current) noexcept
{
    if (current == CharClass::NonWord)
    {
        return static_cast<i8>(kBonusNonWord);
    }
    if (previous == CharClass::NonWord)
    {
        return static_cast<i8>(kBonusBoundary);
    }
    if ((previous == CharClass::Lower && current == CharClass::Upper) ||
        (previous != CharClass::Digit && current == CharClass::Digit))
    {
        return static_cast<i8>(kBonusCamel);
    }
    return 0;
}

i32 leadingPenalty(usize position) noexcept
{
    if (position == 0)
    {
        return 0;
    }
    const i32 gap = kGapStart + kGapExtension * static_cast<i32>(std::min<usize>(position - 1, 64));
    return std::max(gap, kMaxLeadingPenalty);
}

} // namespace

FuzzyMatcher::FuzzyMatcher(std::string_view pattern)
{
    for (const char32_t c : Utf8View(pattern))
    {
        if (c == U' ')
        {
            continue;
        }
        const FoldedCodePoint folded = searchFold(c);
        for (u32 i = 0; i < folded.count && m_patternLength < kMaxPatternLength; ++i)
        {
            m_pattern[m_patternLength++] = folded.codePoints[i];
        }
    }
}

FuzzyMatcher::~FuzzyMatcher() = default;
FuzzyMatcher::FuzzyMatcher(FuzzyMatcher&&) noexcept = default;
FuzzyMatcher& FuzzyMatcher::operator=(FuzzyMatcher&&) noexcept = default;

void FuzzyMatcher::reserve(usize length)
{
    if (length <= m_capacity)
    {
        return;
    }
    const usize capacity = std::max(length, m_capacity * 2);
    m_text = std::make_unique<char32_t[]>(capacity);
    m_bonus = std::make_unique<i8[]>(capacity);
    m_rows = std::make_unique<i32[]>(capacity * 2);
    m_capacity = capacity;
}

std::optional<i32> FuzzyMatcher::score(std::string_view candidate)
{
    ILL_PROFILE_SCOPE();
    if (m_patternLength == 0)
    {
        return 0;
    }
    // A code point takes at least one byte and folds to at most two code points.
    reserve(candidate.size() * 2);

    usize length = 0;
    CharClass previous = CharClass::NonWord; // the start of the candidate is a word boundary
    for (usize offset = 0; offset < candidate.size();)
    {
        const Utf8Decoded decoded = decodeUtf8(candidate, offset);
        offset += decoded.length;
        const CharClass current = classify(decoded.codePoint);
        const FoldedCodePoint folded = searchFold(decoded.codePoint);
        m_text[length] = folded.codePoints[0];
        m_bonus[length] = bonusAt(previous, current);
        ++length;
        if (folded.count == 2)
        {
            m_text[length] = folded.codePoints[1];
            m_bonus[length] = 0;
            ++length;
        }
        previous = current;
    }

    // Cheap rejection: the pattern must be a subsequence.
    usize matched = 0;
    for (usize j = 0; j < length && matched < m_patternLength; ++j)
    {
        if (m_text[j] == m_pattern[matched])
        {
            ++matched;
        }
    }
    if (matched < m_patternLength)
    {
        return std::nullopt;
    }

    // rows[j]: best score with the current pattern character matched exactly at candidate position j.
    i32* above = m_rows.get();
    i32* row = m_rows.get() + length;
    for (usize j = 0; j < length; ++j)
    {
        row[j] = m_text[j] == m_pattern[0] ? kMatch + kFirstCharMultiplier * m_bonus[j] + leadingPenalty(j)
                                           : kNone;
    }
    for (usize i = 1; i < m_patternLength; ++i)
    {
        std::swap(above, row);
        i32 bestGap = kNone; // best score of a previous match followed by a gap ending before j
        for (usize j = 0; j < length; ++j)
        {
            if (j >= 2)
            {
                const i32 extended = bestGap == kNone ? kNone : bestGap + kGapExtension;
                const i32 opened = above[j - 2] == kNone ? kNone : above[j - 2] + kGapStart;
                bestGap = std::max(extended, opened);
            }
            i32 value = kNone;
            if (m_text[j] == m_pattern[i])
            {
                if (j >= 1 && above[j - 1] != kNone)
                {
                    value = above[j - 1] + kMatch + std::max<i32>(m_bonus[j], kBonusConsecutive);
                }
                if (bestGap != kNone)
                {
                    value = std::max(value, bestGap + kMatch + m_bonus[j]);
                }
            }
            row[j] = value;
        }
    }
    const i32 best = *std::max_element(row, row + length);
    return best == kNone ? std::nullopt : std::optional<i32>(best);
}

} // namespace illusion::text
