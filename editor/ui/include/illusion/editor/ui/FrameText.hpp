#pragma once

/// @file
/// Per-frame UI text: localized labels with stable ids, built in an arena that is reset every frame, so
/// drawing text never allocates.
///
/// Every interactive item gets a label of the form "<localized text>###<stable id>": Dear ImGui hashes
/// only the part after "###", so the item keeps its id, its saved state and its docking position when the
/// language changes, and tests and agents find it by the same id in every language.
///
/// ```cpp
/// text.reset();                       // once per frame, before any UI code
/// if (ImGui::Button(text.label(strings::kMenuSave, "menu.file.save")))   // "Save###menu.file.save"
///     save();
/// ImGui::TextUnformatted(text.format(strings::kStatusSelected, {{"count", selected}}));
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/StringTable.hpp>

#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace illusion
{
class LinearAllocator;
} // namespace illusion

namespace illusion::text
{
class Localization;
} // namespace illusion::text

namespace illusion::editor::ui
{

struct Icon;

/// Builds zero-terminated UI strings that stay valid until the next reset(). When a frame needs more than
/// the capacity, a larger arena is added (allocating once) and kept from the next reset() on. Not
/// thread-safe; use it on the UI thread.
class FrameText : NonCopyable
{
public:
    /// Arguments of a message.
    using Arguments = std::span<const text::MessageArgument>;

    /// Formats with @p localization (which must outlive this object), starting with @p capacity bytes.
    explicit FrameText(const text::Localization& localization, usize capacity = 64 * 1024);
    ~FrameText();

    /// Frees the text of the previous frame. Every pointer returned before becomes invalid.
    void reset() noexcept;

    /// @p key formatted in the active language.
    [[nodiscard]] const char* format(text::TextKey key, Arguments arguments = {});

    /// @copydoc format(text::TextKey, Arguments)
    [[nodiscard]] const char* format(text::TextKey key,
                                     std::initializer_list<text::MessageArgument> arguments)
    {
        return format(key, Arguments(arguments.begin(), arguments.size()));
    }

    /// "<text of @p key>###<@p id>".
    [[nodiscard]] const char* label(text::TextKey key, std::string_view id, Arguments arguments = {});

    /// @copydoc label(text::TextKey, std::string_view, Arguments)
    [[nodiscard]] const char* label(text::TextKey key, std::string_view id,
                                    std::initializer_list<text::MessageArgument> arguments)
    {
        return label(key, id, Arguments(arguments.begin(), arguments.size()));
    }

    /// "<icon> <text of @p key>###<@p id>".
    [[nodiscard]] const char* iconLabel(const Icon& icon, text::TextKey key, std::string_view id,
                                        Arguments arguments = {});

    /// "<icon>###<@p id>", for buttons that show only an icon.
    [[nodiscard]] const char* iconOnly(const Icon& icon, std::string_view id);

    /// "<@p text>###<@p id>" for text that is not translated (names of entities, files, numbers).
    [[nodiscard]] const char* verbatim(std::string_view text, std::string_view id);

    /// "###<@p id>": an item without visible text.
    [[nodiscard]] const char* hidden(std::string_view id);

    /// A zero-terminated copy of @p text.
    [[nodiscard]] const char* copy(std::string_view text);

    /// "<@p a><@p b>", e.g. a parent id and a suffix.
    [[nodiscard]] const char* concat(std::string_view a, std::string_view b);

    /// The localization used for formatting.
    [[nodiscard]] const text::Localization& localization() const noexcept { return m_localization; }

    /// Bytes used since the last reset().
    [[nodiscard]] usize used() const noexcept;

private:
    struct Parts
    {
        const Icon* icon = nullptr;
        const text::TextKey* key = nullptr;
        Arguments arguments;
        std::string_view text;
        std::string_view suffix;
        std::string_view id;
        bool hasId = false;
    };

    const char* build(const Parts& parts);

    const text::Localization& m_localization;
    std::unique_ptr<LinearAllocator> m_arena;
    std::vector<std::unique_ptr<LinearAllocator>> m_retired;
};

} // namespace illusion::editor::ui
