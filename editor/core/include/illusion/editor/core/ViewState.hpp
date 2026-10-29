#pragma once

/// @file
/// The user's view state of the scene: entities hidden in viewports (the Outliner's eye) and entities locked
/// against picking (its lock). It belongs to the person at the editor, not to the scene: it is not saved
/// in scene files, not undoable, and changed through the `view.*` commands. It also keeps the camera
/// bookmarks of `camera.bookmark_set` and `camera.bookmark_go`.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace illusion::editor
{

/// Number of camera bookmark slots (1 to 9, Ctrl+1 to Ctrl+9 in a viewport).
inline constexpr u32 kCameraBookmarkSlots = 9;

/// Sets of hidden and locked entities by EntityGuid, and the camera bookmarks.
class ViewState
{
public:
    /// True if @p guid is hidden in viewports.
    [[nodiscard]] bool hidden(EntityGuid guid) const noexcept;

    /// True if @p guid cannot be picked in viewports.
    [[nodiscard]] bool locked(EntityGuid guid) const noexcept;

    /// Hides or shows @p guid. Returns true if that changed anything.
    bool setHidden(EntityGuid guid, bool value);

    /// Locks or unlocks @p guid. Returns true if that changed anything.
    bool setLocked(EntityGuid guid, bool value);

    /// The hidden entities, sorted.
    [[nodiscard]] std::span<const EntityGuid> hiddenEntities() const noexcept { return m_hidden; }

    /// The locked entities, sorted.
    [[nodiscard]] std::span<const EntityGuid> lockedEntities() const noexcept { return m_locked; }

    /// The camera stored in bookmark @p slot (1 to kCameraBookmarkSlots), or std::nullopt.
    [[nodiscard]] std::optional<ViewportCamera> bookmark(u32 slot) const noexcept;

    /// Stores @p camera in bookmark @p slot (1 to kCameraBookmarkSlots).
    void setBookmark(u32 slot, const ViewportCamera& camera) noexcept;

    /// Shows and unlocks everything and forgets the bookmarks (a scene was replaced).
    void clear() noexcept;

    /// Grows whenever the state changes (for caches of panels).
    [[nodiscard]] u64 version() const noexcept { return m_version; }

private:
    std::vector<EntityGuid> m_hidden;
    std::vector<EntityGuid> m_locked;
    std::array<std::optional<ViewportCamera>, kCameraBookmarkSlots> m_bookmarks{};
    u64 m_version = 0;
};

} // namespace illusion::editor
