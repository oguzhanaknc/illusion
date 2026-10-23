#pragma once

/// @file
/// Change tracking of the edited scene by revision, and the editor's event hub.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/EntityGuid.hpp>

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace illusion::editor
{

/// What happened to an entity in a revision.
enum class ChangeKind : u8
{
    Created,   ///< The entity appeared.
    Destroyed, ///< The entity disappeared.
    Changed,   ///< One of its Components was added, removed or set.
};

/// Stable lower-case name of @p kind, e.g. "created".
[[nodiscard]] std::string_view changeKindName(ChangeKind kind) noexcept;

/// One change of one entity.
struct EntityChange
{
    u64 revision = 0;                      ///< Revision that made it.
    EntityGuid entity;                     ///< The entity.
    ChangeKind kind = ChangeKind::Changed; ///< What happened.
    std::string component;                 ///< Changed: stable name of the Component ("" otherwise).
};

/// Revisions of the edited scene and the entity changes of each one.
///
/// WorldEditor records changes as operations apply; EditorContext ends a revision after each committed
/// transaction, undo and redo. Clients that remember a revision ask for the changes since then; when the
/// journal has dropped them (it keeps the newest `capacity` changes), they read the scene again.
class ChangeJournal
{
public:
    /// A journal that keeps the newest @p capacity changes.
    explicit ChangeJournal(usize capacity = 65536) noexcept
        : m_capacity(capacity)
    {
    }

    /// Records a change for the next revision; exact repeats within a revision are dropped.
    void record(EntityGuid entity, ChangeKind kind, std::string_view component = {});

    /// True if changes were recorded since the last endRevision().
    [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }

    /// Drops the changes recorded since the last endRevision() (a rolled-back transaction).
    void discardPending() noexcept
    {
        m_pending.clear();
        m_pendingKeys.clear();
    }

    /// Ends the revision: if changes are pending, the revision number increases and they get it.
    /// Returns the current revision.
    u64 endRevision();

    /// The current revision (0 before any change).
    [[nodiscard]] u64 revision() const noexcept { return m_revision; }

    /// Appends the changes after @p revision to @p out, oldest first. Returns false if some of them were
    /// dropped (or forgotten by reset()), in which case @p out is unchanged.
    [[nodiscard]] bool changesSince(u64 revision, std::vector<EntityChange>& out) const;

    /// Forgets all changes (a new scene was opened); the revision keeps counting and increases by one.
    void reset();

private:
    std::deque<EntityChange> m_changes;
    std::vector<EntityChange> m_pending;
    std::unordered_set<u64> m_pendingKeys; // hashes of the pending changes, to drop repeats
    usize m_capacity;
    u64 m_revision = 0;
    u64 m_oldest = 0; // changes after this revision are complete
};

/// An event for subscribers: a topic such as "scene.changed" and a JSON payload.
struct EditorEvent
{
    std::string topic;   ///< Topic.
    std::string payload; ///< Compact JSON object.
};

/// Delivers editor events once per frame. Posting the same topic again in a frame replaces the payload, so
/// subscribers see each topic at most once per flush().
class EventHub
{
public:
    /// Receives events.
    using Listener = std::function<void(const EditorEvent& event)>;

    /// Calls @p listener for events of @p topic ("*" for all). Returns an id for unsubscribe().
    u32 subscribe(std::string topic, Listener listener);

    /// Removes a subscription; returns false if @p id is unknown.
    bool unsubscribe(u32 id);

    /// Queues an event for the next flush().
    void post(std::string_view topic, std::string payload);

    /// Delivers the queued events in the order their topics were first posted.
    void flush();

    /// Number of queued events.
    [[nodiscard]] usize pending() const noexcept { return m_queue.size(); }

private:
    struct Subscription
    {
        u32 id = 0;
        std::string topic;
        Listener listener;
    };

    std::vector<Subscription> m_subscriptions;
    std::vector<EditorEvent> m_queue;
    std::vector<EditorEvent> m_delivering;
    u32 m_nextId = 1;
};

} // namespace illusion::editor
