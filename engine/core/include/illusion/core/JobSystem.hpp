#pragma once

/// @file
/// Job system: a fixed pool of worker threads executing small, allocation-free jobs.
///
/// Jobs are InplaceFunction<void()> objects stored by value in a bounded lock-free MPMC queue, so
/// submitting a job never allocates. Completion is tracked with a JobCounter; JobSystem::wait()
/// executes queued jobs on the waiting thread instead of blocking, so jobs may submit and wait on
/// other jobs without deadlocking.
///
/// Rules for jobs:
/// - Jobs must not throw. An exception escaping a job terminates the process.
/// - A job must not block on anything other than JobSystem::wait() (no mutex held across waits,
///   no sleeping on I/O). Long blocking I/O belongs on dedicated threads.
/// - Captures are limited to Job's inline capacity (48 bytes); capture a pointer to larger data.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/InplaceFunction.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <type_traits>

namespace illusion
{

/// A unit of work. Captures must fit in 48 bytes.
using Job = InplaceFunction<void(), 48>;

/// Counts unfinished jobs. Pass it to JobSystem::submit() and wait for it with JobSystem::wait().
/// A counter must outlive every job submitted with it.
class JobCounter : NonCopyable
{
public:
    JobCounter() = default;

    /// Returns true when every job submitted with this counter has finished.
    [[nodiscard]] bool isDone() const noexcept { return m_pending.load(std::memory_order_acquire) == 0; }

    /// Number of jobs that have been submitted with this counter and have not finished yet.
    [[nodiscard]] u32 pending() const noexcept { return m_pending.load(std::memory_order_acquire); }

private:
    friend class JobSystem;
    std::atomic<u32> m_pending{0};
};

/// Creation parameters for JobSystem.
struct JobSystemDesc
{
    /// Number of worker threads. 0 selects hardwareThreadCount() - 1 (at least 1), leaving one
    /// hardware thread for the main thread.
    u32 workerCount = 0;

    /// Maximum number of queued jobs (rounded up to a power of two). When the queue is full, submit()
    /// runs the job immediately on the calling thread.
    u32 queueCapacity = 4096;
};

/// Fixed pool of worker threads. Thread-safe: any thread, including workers, may submit and wait.
class JobSystem : NonCopyable
{
public:
    /// Starts the worker threads.
    explicit JobSystem(const JobSystemDesc& desc = {});

    /// Finishes every queued job, then stops and joins the workers.
    ~JobSystem();

    /// Queues @p job. If @p counter is not null it is incremented now and decremented when the job
    /// has finished.
    void submit(Job job, JobCounter* counter = nullptr);

    /// Returns when @p counter reaches zero. The calling thread executes queued jobs while waiting.
    void wait(JobCounter& counter);

    /// Calls `fn(begin, end)` for consecutive ranges of at most @p batchSize items covering
    /// [0, @p count) and returns when all ranges are done. @p fn is invoked concurrently from several
    /// threads and must be safe to call that way. The calling thread processes the first range.
    template <class F>
    void parallelFor(u32 count, u32 batchSize, F&& fn)
    {
        static_assert(std::is_invocable_v<F&, u32, u32>,
                      "parallelFor callable must accept (u32 begin, u32 end)");
        ILL_ASSERT(batchSize > 0, "parallelFor batchSize must be > 0");
        if (count == 0)
        {
            return;
        }
        auto* function = std::addressof(fn);
        JobCounter counter;
        const u32 firstEnd = std::min(batchSize, count);
        for (u32 begin = firstEnd; begin < count;)
        {
            const u32 end = begin + std::min(batchSize, count - begin);
            submit([function, begin, end] { (*function)(begin, end); }, &counter);
            begin = end;
        }
        fn(0u, firstEnd);
        wait(counter);
    }

    /// Number of worker threads (not counting threads that call wait()).
    [[nodiscard]] u32 workerCount() const noexcept;

    /// Index of the calling worker thread in [0, workerCount()), or -1 if the caller is not a worker.
    [[nodiscard]] static i32 currentWorkerIndex() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
