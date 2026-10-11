#pragma once

/// @file
/// Typed publish/subscribe event bus.
///
/// ```cpp
/// EventBus bus;
/// Subscription sub = bus.subscribe<WindowResizedEvent>([](const WindowResizedEvent& e) { ... });
/// bus.publish(WindowResizedEvent{...});   // handlers run now, in subscription order
/// bus.enqueue(WindowResizedEvent{...});   // stored ...
/// bus.dispatchQueued();                   // ... and delivered here, in enqueue order
/// ```
///
/// Events are plain structs identified by their type (see typeHash()). Queued events must be trivially
/// copyable. A Subscription unsubscribes automatically when destroyed. Handlers may publish, enqueue,
/// subscribe and unsubscribe while being dispatched: new subscribers receive events from the next
/// publish on, and unsubscribed handlers are not called again.
///
/// The bus is not thread-safe; use it from one thread (normally the main thread). Publishing and
/// dispatching do not allocate once each event type has been seen and the queue has grown to its
/// steady-state size.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/InplaceFunction.hpp>
#include <illusion/core/TypeName.hpp>

#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace illusion
{

class EventBus;

/// Keeps a handler registered while alive. Move-only; destroying or reset()ting it unsubscribes.
class Subscription
{
public:
    /// Creates an empty subscription.
    Subscription() noexcept = default;

    /// Takes over @p other's registration.
    Subscription(Subscription&& other) noexcept
        : m_bus(std::exchange(other.m_bus, nullptr))
        , m_eventType(other.m_eventType)
        , m_id(other.m_id)
    {
    }

    /// Unsubscribes the current handler and takes over @p other's registration.
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_bus = std::exchange(other.m_bus, nullptr);
            m_eventType = other.m_eventType;
            m_id = other.m_id;
        }
        return *this;
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /// Unsubscribes.
    ~Subscription() { reset(); }

    /// Unsubscribes now. Safe to call on an empty subscription.
    void reset() noexcept;

    /// Returns true while the handler is registered.
    [[nodiscard]] bool isActive() const noexcept { return m_bus != nullptr; }

private:
    friend class EventBus;
    Subscription(EventBus* bus, u64 eventType, u64 id) noexcept
        : m_bus(bus)
        , m_eventType(eventType)
        , m_id(id)
    {
    }

    EventBus* m_bus = nullptr;
    u64 m_eventType = 0;
    u64 m_id = 0;
};

/// Synchronous and queued event delivery. See the file documentation.
class EventBus : NonCopyable
{
public:
    /// Type-erased handler. Captures must fit in 56 bytes.
    using Handler = InplaceFunction<void(const void*), 56>;

    EventBus();
    /// All Subscription objects must be destroyed or reset before the bus.
    ~EventBus();

    /// Registers @p handler (callable as `handler(const E&)`) for events of type @p E.
    template <class E, class F>
    [[nodiscard]] Subscription subscribe(F&& handler)
    {
        static_assert(std::is_invocable_v<std::remove_cvref_t<F>&, const E&>,
                      "handler must be callable with (const E&)");
        static_assert(sizeof(std::remove_cvref_t<F>) <= 56, "handler captures more than 56 bytes");
        Handler erased = [fn = std::forward<F>(handler)](const void* event) mutable {
            fn(*static_cast<const E*>(event));
        };
        const u64 id = addHandler(typeHash<E>(), std::move(erased));
        return Subscription(this, typeHash<E>(), id);
    }

    /// Calls every handler subscribed to @p E with @p event, in subscription order, before returning.
    template <class E>
    void publish(const E& event)
    {
        dispatch(typeHash<E>(), &event);
    }

    /// Stores a copy of @p event for the next dispatchQueued() call.
    template <class E>
    void enqueue(const E& event)
    {
        static_assert(std::is_trivially_copyable_v<E> && std::is_trivially_destructible_v<E>,
                      "queued events must be trivially copyable");
        static_assert(alignof(E) <= kQueueAlignment, "queued event is over-aligned");
        void* storage = allocateQueued(typeHash<E>(), static_cast<u32>(sizeof(E)));
        std::memcpy(storage, &event, sizeof(E));
    }

    /// Delivers every event queued before this call, in enqueue order. Events enqueued by handlers
    /// during the call are delivered by the next call.
    void dispatchQueued();

    /// Number of queued events waiting for dispatchQueued().
    [[nodiscard]] u32 queuedCount() const noexcept;

    /// Number of active handlers for @p E.
    template <class E>
    [[nodiscard]] u32 handlerCount() const noexcept
    {
        return handlerCount(typeHash<E>());
    }

private:
    friend class Subscription;
    static constexpr usize kQueueAlignment = 16;

    u64 addHandler(u64 eventType, Handler handler);
    void removeHandler(u64 eventType, u64 id) noexcept;
    void dispatch(u64 eventType, const void* event);
    void* allocateQueued(u64 eventType, u32 size);
    [[nodiscard]] u32 handlerCount(u64 eventType) const noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

inline void Subscription::reset() noexcept
{
    if (m_bus != nullptr)
    {
        m_bus->removeHandler(m_eventType, m_id);
        m_bus = nullptr;
    }
}

} // namespace illusion
