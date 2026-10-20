#pragma once

/// @file
/// Records the items Dear ImGui submits in a frame (through its test engine hooks), so that tests and
/// automation can find widgets by their stable "###id" and act on them (see UiDriver).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;

namespace illusion::editor::ui
{

/// One item submitted in the last frame.
struct UiItem
{
    u32 id = 0;          ///< Dear ImGui's id of the item.
    Vec2 min{0.0f};      ///< Top-left corner of its rectangle, in window coordinates of the OS window.
    Vec2 max{0.0f};      ///< Bottom-right corner.
    std::string label;   ///< Its label as submitted, e.g. "Save###menu.file.save" ("" if it has none).
    u32 statusFlags = 0; ///< ImGuiItemStatusFlags reported with the label.

    /// The stable part of the label: the text after "###", or the whole label without one.
    [[nodiscard]] std::string_view stableId() const noexcept;

    /// The centre of the item's rectangle.
    [[nodiscard]] Vec2 center() const noexcept { return (min + max) * 0.5f; }
};

/// Collects the items of a context while it is attached. Attaching enables Dear ImGui's test engine hooks
/// for that context; detaching (or destroying the registry) disables them again. At most one registry can
/// be attached to a context. Recording allocates, so attach a registry only in tests and automation.
class UiItemRegistry : NonCopyable
{
public:
    /// Attaches the registry to @p context.
    explicit UiItemRegistry(ImGuiContext* context);

    /// Detaches the registry.
    ~UiItemRegistry();

    /// Forgets the items of the previous frame. Call before ImGui::NewFrame().
    void beginFrame();

    /// Items submitted since beginFrame(), in submission order.
    [[nodiscard]] const std::vector<UiItem>& items() const noexcept { return m_items; }

    /// The last item whose stable id (see UiItem::stableId()) is @p stableId, or nullopt.
    [[nodiscard]] std::optional<UiItem> find(std::string_view stableId) const;

    /// Called by the hooks. Not for direct use.
    void onItemAdd(u32 id, Vec2 min, Vec2 max);
    /// Called by the hooks. Not for direct use.
    void onItemInfo(u32 id, const char* label, u32 flags);

    /// The registry attached to @p context, or nullptr.
    [[nodiscard]] static UiItemRegistry* attachedTo(const ImGuiContext* context) noexcept;

private:
    ImGuiContext* m_context;
    std::vector<UiItem> m_items;
    u32 m_pendingId = 0;
    std::string m_pendingLabel;
    u32 m_pendingFlags = 0;
};

} // namespace illusion::editor::ui
