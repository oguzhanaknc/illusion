#include <illusion/editor/viewport/ResizePolicy.hpp>

namespace illusion::editor::viewport
{

UVec2 ResizePolicy::update(UVec2 requested, f64 now, bool dragging) noexcept
{
    const bool released = m_dragging && !dragging;
    m_dragging = dragging;
    if (requested.x == 0 || requested.y == 0)
    {
        return m_current;
    }
    if (m_current.x == 0 || m_current.y == 0)
    {
        m_current = requested;
        m_pending = requested;
        return m_current;
    }
    if (requested != m_pending)
    {
        m_pending = requested;
        m_changed = now;
    }
    if (m_pending != m_current && (released || now - m_changed >= kSettleSeconds))
    {
        m_current = m_pending;
    }
    return m_current;
}

void ResizePolicy::reset(UVec2 size) noexcept
{
    m_current = size;
    m_pending = size;
}

} // namespace illusion::editor::viewport
