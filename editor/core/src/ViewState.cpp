#include <illusion/core/Assert.hpp>
#include <illusion/editor/core/ViewState.hpp>

#include <algorithm>

namespace illusion::editor
{
namespace
{

bool lessGuid(EntityGuid a, EntityGuid b) noexcept
{
    return a.value < b.value;
}

bool contains(const std::vector<EntityGuid>& set, EntityGuid guid) noexcept
{
    return std::binary_search(set.begin(), set.end(), guid, lessGuid);
}

bool assign(std::vector<EntityGuid>& set, EntityGuid guid, bool value)
{
    const auto it = std::lower_bound(set.begin(), set.end(), guid, lessGuid);
    const bool present = it != set.end() && *it == guid;
    if (present == value || guid.isNull())
    {
        return false;
    }
    if (value)
    {
        set.insert(it, guid);
    }
    else
    {
        set.erase(it);
    }
    return true;
}

} // namespace

bool ViewState::hidden(EntityGuid guid) const noexcept
{
    return contains(m_hidden, guid);
}

bool ViewState::locked(EntityGuid guid) const noexcept
{
    return contains(m_locked, guid);
}

bool ViewState::setHidden(EntityGuid guid, bool value)
{
    if (!assign(m_hidden, guid, value))
    {
        return false;
    }
    ++m_version;
    return true;
}

bool ViewState::setLocked(EntityGuid guid, bool value)
{
    if (!assign(m_locked, guid, value))
    {
        return false;
    }
    ++m_version;
    return true;
}

std::optional<ViewportCamera> ViewState::bookmark(u32 slot) const noexcept
{
    ILL_ASSERT(slot >= 1 && slot <= kCameraBookmarkSlots, "bookmark slots are 1 to 9");
    return m_bookmarks[slot - 1];
}

void ViewState::setBookmark(u32 slot, const ViewportCamera& camera) noexcept
{
    ILL_ASSERT(slot >= 1 && slot <= kCameraBookmarkSlots, "bookmark slots are 1 to 9");
    m_bookmarks[slot - 1] = camera;
    ++m_version;
}

void ViewState::clear() noexcept
{
    const bool bookmarks =
        std::any_of(m_bookmarks.begin(), m_bookmarks.end(),
                    [](const std::optional<ViewportCamera>& slot) { return slot.has_value(); });
    if (!m_hidden.empty() || !m_locked.empty() || bookmarks)
    {
        m_hidden.clear();
        m_locked.clear();
        m_bookmarks.fill(std::nullopt);
        ++m_version;
    }
}

} // namespace illusion::editor
