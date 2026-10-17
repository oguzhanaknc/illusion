#include <illusion/core/JobSystem.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

using namespace illusion;

TEST_CASE("JobSystem runs every submitted job before wait returns")
{
    JobSystem jobs(JobSystemDesc{4, 1024});
    CHECK(jobs.workerCount() == 4);

    std::atomic<int> sum{0};
    JobCounter counter;
    for (int i = 1; i <= 1000; ++i)
    {
        jobs.submit([&sum, i] { sum.fetch_add(i, std::memory_order_relaxed); }, &counter);
    }
    jobs.wait(counter);
    CHECK(counter.isDone());
    CHECK(sum.load() == 500500);
}

TEST_CASE("JobSystem runs jobs inline when the queue is full")
{
    JobSystem jobs(JobSystemDesc{1, 2});
    std::atomic<int> count{0};
    JobCounter counter;
    for (int i = 0; i < 10000; ++i)
    {
        jobs.submit([&count] { count.fetch_add(1, std::memory_order_relaxed); }, &counter);
    }
    jobs.wait(counter);
    CHECK(count.load() == 10000);
}

TEST_CASE("trySubmit refuses jobs when the queue is full instead of running them")
{
    JobSystem jobs(JobSystemDesc{1, 2});
    // Occupy the only worker so that queued jobs stay in the queue.
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    JobCounter blocker;
    jobs.submit(
        [&started, &release] {
            started.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        },
        &blocker);
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    std::atomic<int> count{0};
    JobCounter counter;
    const auto increment = [&count] {
        count.fetch_add(1, std::memory_order_relaxed);
    };
    CHECK(jobs.trySubmit(increment, &counter));
    CHECK(jobs.trySubmit(increment, &counter));
    CHECK_FALSE(jobs.trySubmit(increment, &counter));
    CHECK_FALSE(jobs.trySubmit(increment));
    CHECK(counter.pending() == 2);
    CHECK(count.load() == 0);

    release.store(true, std::memory_order_release);
    jobs.wait(blocker);
    jobs.wait(counter);
    CHECK(count.load() == 2);
}

TEST_CASE("Jobs can submit and wait for nested jobs")
{
    JobSystem jobs(JobSystemDesc{3, 256});
    std::atomic<int> leaves{0};
    JobCounter outer;
    for (int i = 0; i < 32; ++i)
    {
        jobs.submit(
            [&jobs, &leaves] {
                JobCounter inner;
                for (int j = 0; j < 32; ++j)
                {
                    jobs.submit([&leaves] { leaves.fetch_add(1, std::memory_order_relaxed); }, &inner);
                }
                jobs.wait(inner);
            },
            &outer);
    }
    jobs.wait(outer);
    CHECK(leaves.load() == 32 * 32);
}

TEST_CASE("Jobs without a counter still run before the JobSystem is destroyed")
{
    std::atomic<int> count{0};
    {
        JobSystem jobs(JobSystemDesc{2, 64});
        for (int i = 0; i < 500; ++i)
        {
            jobs.submit([&count] { count.fetch_add(1, std::memory_order_relaxed); });
        }
    }
    CHECK(count.load() == 500);
}

TEST_CASE("parallelFor covers the range exactly once")
{
    JobSystem jobs(JobSystemDesc{4, 512});
    for (const u32 count : {0u, 1u, 7u, 64u, 1000u, 100003u})
    {
        for (const u32 batch : {1u, 3u, 64u, 5000u})
        {
            std::vector<std::atomic<u8>> hits(count);
            jobs.parallelFor(count, batch, [&hits](u32 begin, u32 end) {
                for (u32 i = begin; i < end; ++i)
                {
                    hits[i].fetch_add(1, std::memory_order_relaxed);
                }
            });
            bool allOnce = true;
            for (const auto& hit : hits)
            {
                allOnce = allOnce && hit.load() == 1;
            }
            CHECK_MESSAGE(allOnce, "count=", count, " batch=", batch);
        }
    }
}

TEST_CASE("Workers report their index; other threads report -1")
{
    CHECK(JobSystem::currentWorkerIndex() == -1);
    JobSystem jobs(JobSystemDesc{3, 64});
    std::mutex mutex;
    std::set<i32> seen;
    JobCounter counter;
    for (int i = 0; i < 200; ++i)
    {
        jobs.submit(
            [&] {
                const i32 index = JobSystem::currentWorkerIndex();
                std::scoped_lock lock(mutex);
                seen.insert(index);
            },
            &counter);
    }
    jobs.wait(counter);
    for (const i32 index : seen)
    {
        // -1 is allowed: the main thread helps while waiting.
        CHECK(index >= -1);
        CHECK(index < 3);
    }
}

TEST_CASE("JobSystem stress: many producers")
{
    JobSystem jobs(JobSystemDesc{4, 128});
    std::atomic<u64> total{0};
    JobCounter producers;
    for (int p = 0; p < 16; ++p)
    {
        jobs.submit(
            [&jobs, &total] {
                JobCounter children;
                for (u64 i = 0; i < 1000; ++i)
                {
                    jobs.submit([&total, i] { total.fetch_add(i, std::memory_order_relaxed); }, &children);
                }
                jobs.wait(children);
            },
            &producers);
    }
    jobs.wait(producers);
    CHECK(total.load() == 16ull * (999ull * 1000ull / 2ull));
}
