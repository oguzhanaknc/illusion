#pragma once

/// @file
/// The editor's selection: an ordered set of entity ids.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/EntityGuid.hpp>

#include <span>
#include <vector>

namespace illusion::editor
{

/// The selected entities, by id, in the order they were selected (the last one is the primary). Changing
/// it is not an undo step; every undo step stores the selection before and after it and restores it.
class Selection
{
public:
    /// The selected ids, oldest first.
    [[nodiscard]] std::span<const EntityGuid> items() const noexcept { return m_items; }

    /// Number of selected entities.
    [[nodiscard]] usize size() const noexcept { return m_items.size(); }

    /// True if nothing is selected.
    [[nodiscard]] bool empty() const noexcept { return m_items.empty(); }

    /// The most recently selected entity, or the null id.
    [[nodiscard]] EntityGuid primary() const noexcept
    {
        return m_items.empty() ? EntityGuid{} : m_items.back();
    }

    /// True if @p guid is selected.
    [[nodiscard]] bool contains(EntityGuid guid) const noexcept;

    /// Replaces the selection with @p guids (duplicates and null ids are dropped).
    void set(std::span<const EntityGuid> guids);

    /// Adds @p guid at the end, or moves it there if it is selected already.
    void add(EntityGuid guid);

    /// Removes @p guid; returns false if it was not selected.
    bool remove(EntityGuid guid);

    /// Deselects everything.
    void clear();

    /// Increases whenever the selection changes; for caches and change events.
    [[nodiscard]] u64 version() const noexcept { return m_version; }

private:
    std::vector<EntityGuid> m_items;
    u64 m_version = 0;
};

} // namespace illusion::editor
