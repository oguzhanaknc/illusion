#include "TestSupport.hpp"

#include <illusion/core/EventBus.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace illusion;

namespace eventbus_test
{
struct Ping
{
    int value;
};
struct Pong
{
    int value;
};
struct Message
{
    std::string text; // not trivially copyable: publish() only
};
} // namespace eventbus_test

using namespace eventbus_test;

TEST_CASE("publish delivers to every subscriber in subscription order")
{
    EventBus bus;
    std::vector<int> calls;
    Subscription a = bus.subscribe<Ping>([&](const Ping& p) { calls.push_back(p.value); });
    Subscription b = bus.subscribe<Ping>([&](const Ping& p) { calls.push_back(p.value * 10); });
    Subscription other = bus.subscribe<Pong>([&](const Pong&) { calls.push_back(-1); });

    bus.publish(Ping{3});
    CHECK(calls == std::vector<int>{3, 30});
    CHECK(bus.handlerCount<Ping>() == 2);
    CHECK(bus.handlerCount<Pong>() == 1);
}

TEST_CASE("publishing an event without subscribers does nothing")
{
    EventBus bus;
    bus.publish(Ping{1});
    bus.publish(Message{"hello"});
    CHECK(bus.handlerCount<Ping>() == 0);
}

TEST_CASE("non-trivial events can be published")
{
    EventBus bus;
    std::string received;
    Subscription sub = bus.subscribe<Message>([&](const Message& m) { received = m.text; });
    bus.publish(Message{"hello"});
    CHECK(received == "hello");
}

TEST_CASE("Subscription unsubscribes when destroyed, reset or moved over")
{
    EventBus bus;
    int count = 0;
    {
        Subscription sub = bus.subscribe<Ping>([&](const Ping&) { ++count; });
        CHECK(sub.isActive());
        bus.publish(Ping{0});
    }
    bus.publish(Ping{0});
    CHECK(count == 1);

    Subscription sub = bus.subscribe<Ping>([&](const Ping&) { ++count; });
    Subscription moved = std::move(sub);
    CHECK_FALSE(sub.isActive()); // NOLINT(bugprone-use-after-move): moved-from state is specified
    CHECK(moved.isActive());
    bus.publish(Ping{0});
    CHECK(count == 2);
    moved.reset();
    CHECK_FALSE(moved.isActive());
    bus.publish(Ping{0});
    CHECK(count == 2);

    Subscription first = bus.subscribe<Ping>([&](const Ping&) { count += 100; });
    Subscription second = bus.subscribe<Ping>([&](const Ping&) { count += 1000; });
    first = std::move(second); // unsubscribes the original "first" handler
    bus.publish(Ping{0});
    CHECK(count == 1002);
    CHECK(bus.handlerCount<Ping>() == 1);
}

TEST_CASE("handlers may unsubscribe themselves and others during dispatch")
{
    EventBus bus;
    std::vector<int> calls;
    Subscription second;
    Subscription first = bus.subscribe<Ping>([&](const Ping&) {
        calls.push_back(1);
        second.reset(); // must not be called in this dispatch
    });
    second = bus.subscribe<Ping>([&](const Ping&) { calls.push_back(2); });
    Subscription self;
    self = bus.subscribe<Ping>([&](const Ping&) {
        calls.push_back(3);
        self.reset(); // unsubscribing while running
    });

    bus.publish(Ping{0});
    CHECK(calls == std::vector<int>{1, 3});
    bus.publish(Ping{0});
    CHECK(calls == std::vector<int>{1, 3, 1});
    CHECK(bus.handlerCount<Ping>() == 1);
}

TEST_CASE("handlers subscribed during dispatch receive only later events")
{
    EventBus bus;
    std::vector<int> calls;
    std::vector<Subscription> added;
    Subscription spawner = bus.subscribe<Ping>([&](const Ping& p) {
        calls.push_back(p.value);
        if (added.size() < 50)
        {
            // Many subscriptions force storage growth while the dispatch loop runs.
            added.push_back(bus.subscribe<Ping>([&](const Ping& q) { calls.push_back(q.value + 1000); }));
        }
    });
    bus.publish(Ping{1});
    CHECK(calls == std::vector<int>{1});
    bus.publish(Ping{2});
    REQUIRE(calls.size() == 3);
    CHECK(calls[1] == 2);
    CHECK(calls[2] == 1002);
    added.clear();
}

TEST_CASE("nested publish of the same event type is delivered recursively")
{
    EventBus bus;
    int depth = 0;
    int maxDepth = 0;
    Subscription sub = bus.subscribe<Ping>([&](const Ping& p) {
        ++depth;
        maxDepth = std::max(maxDepth, depth);
        if (p.value > 0)
        {
            bus.publish(Ping{p.value - 1});
        }
        --depth;
    });
    bus.publish(Ping{3});
    CHECK(maxDepth == 4);
}

TEST_CASE("queued events are delivered in order by dispatchQueued")
{
    EventBus bus;
    std::vector<int> calls;
    Subscription ping = bus.subscribe<Ping>([&](const Ping& p) { calls.push_back(p.value); });
    Subscription pong = bus.subscribe<Pong>([&](const Pong& p) { calls.push_back(-p.value); });

    bus.enqueue(Ping{1});
    bus.enqueue(Pong{2});
    bus.enqueue(Ping{3});
    CHECK(bus.queuedCount() == 3);
    CHECK(calls.empty());

    bus.dispatchQueued();
    CHECK(calls == std::vector<int>{1, -2, 3});
    CHECK(bus.queuedCount() == 0);
    bus.dispatchQueued();
    CHECK(calls.size() == 3);
}

TEST_CASE("events enqueued during dispatchQueued wait for the next call")
{
    EventBus bus;
    std::vector<int> calls;
    Subscription sub = bus.subscribe<Ping>([&](const Ping& p) {
        calls.push_back(p.value);
        if (p.value < 3)
        {
            bus.enqueue(Ping{p.value + 1});
        }
    });
    bus.enqueue(Ping{1});
    bus.dispatchQueued();
    CHECK(calls == std::vector<int>{1});
    bus.dispatchQueued();
    bus.dispatchQueued();
    CHECK(calls == std::vector<int>{1, 2, 3});
    CHECK(bus.queuedCount() == 0);
}

TEST_CASE("many queued events keep their payloads")
{
    EventBus bus;
    long long sum = 0;
    Subscription sub = bus.subscribe<Ping>([&](const Ping& p) { sum += p.value; });
    for (int i = 0; i < 10000; ++i)
    {
        bus.enqueue(Ping{i});
    }
    bus.dispatchQueued();
    CHECK(sum == 10000LL * 9999LL / 2LL);
}
