#include <illusion/core/EventBus.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Thread.hpp>

#include <algorithm>
#include <cstddef>
#include <unordered_map>

namespace illusion
{

struct EventBus::Impl
{
    struct Entry
    {
        u64 id;
        Handler handler;
        bool active;
    };

    struct Channel
    {
        std::vector<Entry> entries;
        // Subscriptions made while this channel is dispatching. Appending to `entries` then could
        // reallocate it under the running handler.
        std::vector<Entry> pending;
        u32 dispatchDepth = 0;
        bool hasInactive = false;
    };

    struct Record
    {
        u64 eventType;
        u32 offset;
        u32 size;
    };

    struct Queue
    {
        std::vector<std::byte> data;
        std::vector<Record> records;
    };

    std::unordered_map<u64, Channel> channels;
    Queue queues[2];
    u32 writeQueue = 0;
    bool dispatchingQueued = false;
    u64 nextId = 1;
    u32 ownerThread = currentThreadIndex();

    void assertOwner() const noexcept
    {
        ILL_ASSERT(currentThreadIndex() == ownerThread, "EventBus used from more than one thread");
    }

    static void compact(Channel& channel)
    {
        if (channel.hasInactive)
        {
            std::erase_if(channel.entries, [](const Entry& entry) { return !entry.active; });
            channel.hasInactive = false;
        }
        for (Entry& entry : channel.pending)
        {
            channel.entries.push_back(std::move(entry));
        }
        channel.pending.clear();
    }
};

EventBus::EventBus()
    : m_impl(std::make_unique<Impl>())
{
}

EventBus::~EventBus()
{
    for (const auto& [type, channel] : m_impl->channels)
    {
        const auto active = std::count_if(channel.entries.begin(), channel.entries.end(),
                                          [](const Impl::Entry& entry) { return entry.active; });
        ILL_ASSERT(active == 0 && channel.pending.empty(),
                   "EventBus destroyed while subscriptions are alive");
    }
}

u64 EventBus::addHandler(u64 eventType, Handler handler)
{
    m_impl->assertOwner();
    Impl::Channel& channel = m_impl->channels[eventType];
    const u64 id = m_impl->nextId++;
    Impl::Entry entry{id, std::move(handler), true};
    if (channel.dispatchDepth > 0)
    {
        channel.pending.push_back(std::move(entry));
    }
    else
    {
        channel.entries.push_back(std::move(entry));
    }
    return id;
}

void EventBus::removeHandler(u64 eventType, u64 id) noexcept
{
    m_impl->assertOwner();
    const auto it = m_impl->channels.find(eventType);
    if (it == m_impl->channels.end())
    {
        return;
    }
    Impl::Channel& channel = it->second;

    const auto pending = std::find_if(channel.pending.begin(), channel.pending.end(),
                                      [id](const Impl::Entry& entry) { return entry.id == id; });
    if (pending != channel.pending.end())
    {
        channel.pending.erase(pending);
        return;
    }

    const auto entry = std::find_if(channel.entries.begin(), channel.entries.end(),
                                    [id](const Impl::Entry& e) { return e.id == id; });
    if (entry == channel.entries.end())
    {
        return;
    }
    if (channel.dispatchDepth > 0)
    {
        // The handler may be running right now (it may be unsubscribing itself): only deactivate it.
        entry->active = false;
        channel.hasInactive = true;
    }
    else
    {
        channel.entries.erase(entry);
    }
}

void EventBus::dispatch(u64 eventType, const void* event)
{
    m_impl->assertOwner();
    const auto it = m_impl->channels.find(eventType);
    if (it == m_impl->channels.end())
    {
        return;
    }
    Impl::Channel& channel = it->second;

    struct DepthGuard
    {
        Impl::Channel& channel;
        explicit DepthGuard(Impl::Channel& c) noexcept
            : channel(c)
        {
            ++channel.dispatchDepth;
        }
        ~DepthGuard()
        {
            if (--channel.dispatchDepth == 0)
            {
                Impl::compact(channel);
            }
        }
    } guard(channel);

    // `entries` is never resized while dispatchDepth > 0, so indices and references stay valid.
    const usize count = channel.entries.size();
    for (usize i = 0; i < count; ++i)
    {
        Impl::Entry& entry = channel.entries[i];
        if (entry.active)
        {
            entry.handler(event);
        }
    }
}

void* EventBus::allocateQueued(u64 eventType, u32 size)
{
    m_impl->assertOwner();
    Impl::Queue& queue = m_impl->queues[m_impl->writeQueue];
    const usize offset = alignUp(queue.data.size(), kQueueAlignment);
    ILL_CHECK(offset + size <= 0xFFFFFFFFu, "EventBus queue overflow");
    queue.data.resize(offset + size);
    queue.records.push_back(Impl::Record{eventType, static_cast<u32>(offset), size});
    return queue.data.data() + offset;
}

void EventBus::dispatchQueued()
{
    ILL_PROFILE_SCOPE();
    m_impl->assertOwner();
    ILL_ASSERT(!m_impl->dispatchingQueued, "dispatchQueued() called from an event handler");
    if (m_impl->dispatchingQueued)
    {
        return;
    }
    m_impl->dispatchingQueued = true;
    const u32 readIndex = m_impl->writeQueue;
    m_impl->writeQueue ^= 1u;
    Impl::Queue& queue = m_impl->queues[readIndex];

    // std::vector<std::byte> storage comes from operator new, which aligns to at least 16 bytes.
    for (const Impl::Record& record : queue.records)
    {
        dispatch(record.eventType, queue.data.data() + record.offset);
    }
    queue.records.clear();
    queue.data.clear();
    m_impl->dispatchingQueued = false;
}

u32 EventBus::queuedCount() const noexcept
{
    return static_cast<u32>(m_impl->queues[m_impl->writeQueue].records.size());
}

u32 EventBus::handlerCount(u64 eventType) const noexcept
{
    const auto it = m_impl->channels.find(eventType);
    if (it == m_impl->channels.end())
    {
        return 0;
    }
    const Impl::Channel& channel = it->second;
    const auto active = std::count_if(channel.entries.begin(), channel.entries.end(),
                                      [](const Impl::Entry& entry) { return entry.active; });
    return static_cast<u32>(active) + static_cast<u32>(channel.pending.size());
}

} // namespace illusion
