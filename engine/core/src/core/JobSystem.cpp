#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Thread.hpp>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace illusion
{
namespace
{

thread_local i32 t_workerIndex = -1;
std::atomic<i32 (*)() noexcept> g_workerIndexSource{nullptr};

// Number of failed polls before an idle worker goes to sleep.
constexpr u32 kSpinsBeforeSleep = 256;

usize roundUpToPowerOfTwo(usize value) noexcept
{
    usize result = 1;
    while (result < value)
    {
        result <<= 1;
    }
    return result;
}

/// Bounded multi-producer multi-consumer queue (Dmitry Vyukov's algorithm). Each slot carries a
/// sequence number that tells producers and consumers whether the slot is free or filled for their lap.
class JobQueue
{
public:
    explicit JobQueue(usize capacity)
        : m_slots(roundUpToPowerOfTwo(std::max<usize>(capacity, 2)))
        , m_mask(m_slots.size() - 1)
    {
        for (usize i = 0; i < m_slots.size(); ++i)
        {
            m_slots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool tryPush(Job& job, JobCounter* counter) noexcept
    {
        usize position = m_enqueue.load(std::memory_order_relaxed);
        for (;;)
        {
            Slot& slot = m_slots[position & m_mask];
            const usize sequence = slot.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position);
            if (diff == 0)
            {
                if (m_enqueue.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    slot.job = std::move(job);
                    slot.counter = counter;
                    slot.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
            {
                return false; // full
            }
            else
            {
                position = m_enqueue.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(Job& job, JobCounter*& counter) noexcept
    {
        usize position = m_dequeue.load(std::memory_order_relaxed);
        for (;;)
        {
            Slot& slot = m_slots[position & m_mask];
            const usize sequence = slot.sequence.load(std::memory_order_acquire);
            const auto diff =
                static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position + 1);
            if (diff == 0)
            {
                if (m_dequeue.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    job = std::move(slot.job);
                    counter = slot.counter;
                    slot.sequence.store(position + m_mask + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
            {
                return false; // empty
            }
            else
            {
                position = m_dequeue.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Slot
    {
        std::atomic<usize> sequence;
        Job job;
        JobCounter* counter = nullptr;
    };

    std::vector<Slot> m_slots;
    usize m_mask;
    alignas(kCacheLineSize) std::atomic<usize> m_enqueue{0};
    alignas(kCacheLineSize) std::atomic<usize> m_dequeue{0};
};

} // namespace

struct JobSystem::Impl
{
    explicit Impl(usize queueCapacity)
        : queue(queueCapacity)
    {
    }

    JobQueue queue;
    std::vector<std::thread> workers;

    // Approximate number of queued jobs; used only to decide whether sleeping workers must wake up.
    alignas(kCacheLineSize) std::atomic<i64> queued{0};
    alignas(kCacheLineSize) std::atomic<u32> sleeping{0};
    std::atomic<bool> stopping{false};
    std::mutex sleepMutex;
    std::condition_variable wakeUp;

    static void finish(JobCounter* counter) noexcept;

    // Queues @p job and wakes a worker; false if the queue is full (@p job is left untouched).
    bool tryQueue(Job& job, JobCounter* counter)
    {
        if (!queue.tryPush(job, counter))
        {
            return false;
        }
        queued.fetch_add(1, std::memory_order_seq_cst);
        wakeOne();
        return true;
    }

    bool runOne() noexcept
    {
        Job job;
        JobCounter* counter = nullptr;
        if (!queue.tryPop(job, counter))
        {
            return false;
        }
        queued.fetch_sub(1, std::memory_order_relaxed);
        {
            ILL_PROFILE_SCOPE_N("Job");
            job();
        }
        job.reset();
        finish(counter);
        return true;
    }

    void workerMain(i32 index)
    {
        t_workerIndex = index;
        char name[32];
        std::snprintf(name, sizeof(name), "Worker %d", index);
        setCurrentThreadName(name);
        ILL_PROFILE_THREAD(name);

        u32 spins = 0;
        for (;;)
        {
            if (runOne())
            {
                spins = 0;
                continue;
            }
            if (stopping.load(std::memory_order_acquire) && queued.load(std::memory_order_acquire) <= 0)
            {
                return;
            }
            if (++spins < kSpinsBeforeSleep)
            {
                cpuRelax();
                continue;
            }
            spins = 0;
            std::unique_lock lock(sleepMutex);
            sleeping.fetch_add(1, std::memory_order_seq_cst);
            wakeUp.wait(lock, [this] {
                return queued.load(std::memory_order_seq_cst) > 0 || stopping.load(std::memory_order_seq_cst);
            });
            sleeping.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void wakeOne()
    {
        // Pairs with the seq_cst increment of `sleeping` in workerMain: either the worker sees the new
        // job in its wait predicate, or we see it sleeping and notify it under the mutex.
        if (sleeping.load(std::memory_order_seq_cst) > 0)
        {
            {
                std::scoped_lock lock(sleepMutex);
            }
            wakeUp.notify_one();
        }
    }
};

void JobSystem::Impl::finish(JobCounter* counter) noexcept
{
    if (counter != nullptr)
    {
        counter->m_pending.fetch_sub(1, std::memory_order_acq_rel);
    }
}

JobSystem::JobSystem(const JobSystemDesc& desc)
    : m_impl(std::make_unique<Impl>(desc.queueCapacity))
{
    u32 count = desc.workerCount;
    if (count == 0)
    {
        const u32 hardware = hardwareThreadCount();
        count = hardware > 1 ? hardware - 1 : 1;
    }
    m_impl->workers.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        m_impl->workers.emplace_back([impl = m_impl.get(), i] { impl->workerMain(static_cast<i32>(i)); });
    }
}

JobSystem::~JobSystem()
{
    {
        std::scoped_lock lock(m_impl->sleepMutex);
        m_impl->stopping.store(true, std::memory_order_seq_cst);
    }
    m_impl->wakeUp.notify_all();
    for (std::thread& worker : m_impl->workers)
    {
        worker.join();
    }
    // Workers drain the queue before exiting, but a job submitted by a job that ran last could in
    // theory still be queued if every worker had already observed an empty queue. Run leftovers here.
    while (m_impl->runOne())
    {
    }
}

void JobSystem::submit(Job job, JobCounter* counter)
{
    ILL_ASSERT(static_cast<bool>(job), "submitting an empty job");
    if (counter != nullptr)
    {
        counter->m_pending.fetch_add(1, std::memory_order_acq_rel);
    }
    if (!m_impl->tryQueue(job, counter))
    {
        // Queue full: run inline. This keeps submit() non-blocking and allocation-free.
        job();
        Impl::finish(counter);
    }
}

bool JobSystem::trySubmit(Job job, JobCounter* counter)
{
    ILL_ASSERT(static_cast<bool>(job), "submitting an empty job");
    if (counter != nullptr)
    {
        counter->m_pending.fetch_add(1, std::memory_order_acq_rel);
    }
    if (!m_impl->tryQueue(job, counter))
    {
        Impl::finish(counter);
        return false;
    }
    return true;
}

void JobSystem::wait(JobCounter& counter)
{
    ILL_PROFILE_SCOPE();
    u32 spins = 0;
    while (!counter.isDone())
    {
        if (m_impl->runOne())
        {
            spins = 0;
            continue;
        }
        if (++spins < kSpinsBeforeSleep)
        {
            cpuRelax();
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

u32 JobSystem::workerCount() const noexcept
{
    return static_cast<u32>(m_impl->workers.size());
}

i32 JobSystem::currentWorkerIndex() noexcept
{
    if (i32(*source)() noexcept = g_workerIndexSource.load(std::memory_order_relaxed))
    {
        return source();
    }
    return t_workerIndex;
}

void detail::setWorkerIndexSource(i32 (*source)() noexcept) noexcept
{
    // The host's own function never forwards, so installing it into the host is harmless.
    g_workerIndexSource.store(source == &JobSystem::currentWorkerIndex ? nullptr : source,
                              std::memory_order_relaxed);
}

} // namespace illusion
