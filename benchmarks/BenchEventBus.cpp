#include "Budget.hpp"

#include <illusion/core/EventBus.hpp>

using namespace illusion;

namespace bench_events
{
struct Tick
{
    u32 value;
};
} // namespace bench_events

using namespace bench_events;

ILL_BENCHMARK("events/publish", 250.0)
{
    EventBus bus;
    u64 sum = 0;
    Subscription a = bus.subscribe<Tick>([&sum](const Tick& t) { sum += t.value; });
    Subscription b = bus.subscribe<Tick>([&sum](const Tick& t) { sum += t.value * 2u; });
    Subscription c = bus.subscribe<Tick>([&sum](const Tick& t) { sum += t.value * 3u; });
    Subscription d = bus.subscribe<Tick>([&sum](const Tick& t) { sum += t.value * 4u; });
    constexpr u32 kEvents = 10000;
    bench.batch(kEvents).unit("event").run("publish to 4 handlers", [&] {
        for (u32 i = 0; i < kEvents; ++i)
        {
            bus.publish(Tick{i});
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

ILL_BENCHMARK("events/queued", 200.0)
{
    EventBus bus;
    u64 sum = 0;
    Subscription a = bus.subscribe<Tick>([&sum](const Tick& t) { sum += t.value; });
    constexpr u32 kEvents = 10000;
    bench.batch(kEvents).unit("event").run("enqueue + dispatchQueued, 1 handler", [&] {
        for (u32 i = 0; i < kEvents; ++i)
        {
            bus.enqueue(Tick{i});
        }
        bus.dispatchQueued();
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}
