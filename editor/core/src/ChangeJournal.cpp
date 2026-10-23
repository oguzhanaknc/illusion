#include <illusion/core/Hash.hpp>
#include <illusion/editor/core/ChangeJournal.hpp>

#include <algorithm>

namespace illusion::editor
{

std::string_view changeKindName(ChangeKind kind) noexcept
{
    switch (kind)
    {
    case ChangeKind::Created:
        return "created";
    case ChangeKind::Destroyed:
        return "destroyed";
    case ChangeKind::Changed:
        return "changed";
    case ChangeKind::Settings:
        return "settings";
    }
    return "changed";
}

void ChangeJournal::record(EntityGuid entity, ChangeKind kind, std::string_view component)
{
    // The hash finds candidates; equal hashes are compared exactly.
    const u64 key = hashCombine(hashCombine(entity.value, static_cast<u64>(kind)), fnv1a64(component));
    if (!m_pendingKeys.insert(key).second &&
        std::any_of(m_pending.begin(), m_pending.end(), [&](const EntityChange& change) {
            return change.entity == entity && change.kind == kind && change.component == component;
        }))
    {
        return;
    }
    m_pending.push_back(EntityChange{0, entity, kind, std::string(component)});
}

u64 ChangeJournal::endRevision()
{
    if (m_pending.empty())
    {
        return m_revision;
    }
    ++m_revision;
    for (EntityChange& change : m_pending)
    {
        change.revision = m_revision;
        m_changes.push_back(std::move(change));
    }
    m_pending.clear();
    m_pendingKeys.clear();
    while (m_changes.size() > m_capacity)
    {
        // Revisions are dropped whole, so a client never sees part of one.
        const u64 dropped = m_changes.front().revision;
        while (!m_changes.empty() && m_changes.front().revision == dropped)
        {
            m_changes.pop_front();
        }
        m_oldest = dropped;
    }
    return m_revision;
}

bool ChangeJournal::changesSince(u64 revision, std::vector<EntityChange>& out) const
{
    if (revision < m_oldest || revision > m_revision)
    {
        return false;
    }
    const auto first =
        std::partition_point(m_changes.begin(), m_changes.end(),
                             [revision](const EntityChange& change) { return change.revision <= revision; });
    out.insert(out.end(), first, m_changes.end());
    return true;
}

void ChangeJournal::reset()
{
    m_changes.clear();
    m_pending.clear();
    m_pendingKeys.clear();
    ++m_revision;
    m_oldest = m_revision;
}

u32 EventHub::subscribe(std::string topic, Listener listener)
{
    const u32 id = m_nextId++;
    m_subscriptions.push_back(Subscription{id, std::move(topic), std::move(listener)});
    return id;
}

bool EventHub::unsubscribe(u32 id)
{
    return std::erase_if(m_subscriptions,
                         [id](const Subscription& subscription) { return subscription.id == id; }) > 0;
}

void EventHub::post(std::string_view topic, std::string payload)
{
    for (EditorEvent& event : m_queue)
    {
        if (event.topic == topic)
        {
            event.payload = std::move(payload);
            return;
        }
    }
    m_queue.push_back(EditorEvent{std::string(topic), std::move(payload)});
}

void EventHub::flush()
{
    // Listeners may post events; those go out with the next flush.
    std::swap(m_delivering, m_queue);
    m_queue.clear();
    for (const EditorEvent& event : m_delivering)
    {
        for (usize i = 0; i < m_subscriptions.size(); ++i)
        {
            if (m_subscriptions[i].topic == "*" || m_subscriptions[i].topic == event.topic)
            {
                const Listener listener = m_subscriptions[i].listener; // the listener may unsubscribe
                listener(event);
            }
        }
    }
    m_delivering.clear();
}

} // namespace illusion::editor
