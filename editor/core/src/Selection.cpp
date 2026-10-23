#include <illusion/editor/core/SceneDocument.hpp>
#include <illusion/editor/core/Selection.hpp>

#include <algorithm>

namespace illusion::editor
{

bool Selection::contains(EntityGuid guid) const noexcept
{
    return std::find(m_items.begin(), m_items.end(), guid) != m_items.end();
}

void Selection::set(std::span<const EntityGuid> guids)
{
    std::vector<EntityGuid> items;
    items.reserve(guids.size());
    for (const EntityGuid guid : guids)
    {
        if (!guid.isNull() && std::find(items.begin(), items.end(), guid) == items.end())
        {
            items.push_back(guid);
        }
    }
    if (items != m_items)
    {
        m_items = std::move(items);
        ++m_version;
    }
}

void Selection::add(EntityGuid guid)
{
    if (guid.isNull() || (!m_items.empty() && m_items.back() == guid))
    {
        return;
    }
    std::erase(m_items, guid);
    m_items.push_back(guid);
    ++m_version;
}

bool Selection::remove(EntityGuid guid)
{
    if (std::erase(m_items, guid) == 0)
    {
        return false;
    }
    ++m_version;
    return true;
}

void Selection::clear()
{
    if (!m_items.empty())
    {
        m_items.clear();
        ++m_version;
    }
}

std::string_view SceneDocument::displayName() const noexcept
{
    if (m_path.empty())
    {
        return "Untitled";
    }
    std::string_view name = m_path;
    if (const usize slash = name.find_last_of('/'); slash != std::string_view::npos)
    {
        name.remove_prefix(slash + 1);
    }
    if (const usize dot = name.find_last_of('.'); dot != std::string_view::npos && dot > 0)
    {
        name = name.substr(0, dot);
    }
    return name;
}

} // namespace illusion::editor
