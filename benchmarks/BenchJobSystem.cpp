#include "Budget.hpp"

#include <illusion/core/JobSystem.hpp>

#include <atomic>
#include <vector>

using namespace illusion;

ILL_BENCHMARK("jobs/submit-wait", 1500.0)
{
    JobSystem jobs;
    constexpr u32 kJobs = 10000;
    std::atomic<u32> sink{0};
    bench.batch(kJobs).unit("job").run("submit + wait (empty jobs)", [&] {
        JobCounter counter;
        for (u32 i = 0; i < kJobs; ++i)
        {
            jobs.submit([&sink] { sink.fetch_add(1, std::memory_order_relaxed); }, &counter);
        }
        jobs.wait(counter);
    });
}

ILL_BENCHMARK("jobs/parallel-for", 2.0)
{
    JobSystem jobs;
    constexpr u32 kItems = 1u << 20;
    std::vector<float> data(kItems, 1.0f);
    bench.batch(kItems).unit("item").run("parallelFor x*0.5+1, batch 4096", [&] {
        jobs.parallelFor(kItems, 4096, [&data](u32 begin, u32 end) {
            for (u32 i = begin; i < end; ++i)
            {
                data[i] = data[i] * 0.5f + 1.0f;
            }
        });
        ankerl::nanobench::doNotOptimizeAway(data.data());
    });
}
