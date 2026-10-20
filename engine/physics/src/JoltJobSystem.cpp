#include "JoltJobSystem.hpp"

#include <illusion/core/Assert.hpp>

namespace illusion::detail
{

JoltJobSystem::JoltJobSystem(illusion::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers)
    : JobSystemWithBarrier(maxBarriers)
    , m_jobs(jobs)
{
    m_pool.Init(maxJobs, maxJobs);
}

JoltJobSystem::~JoltJobSystem()
{
    // Engine jobs may still hold a reference to a finished Jolt job; let them release it first.
    m_jobs.wait(m_pending);
}

void JoltJobSystem::waitForEngineJobs()
{
    m_jobs.wait(m_pending);
}

int JoltJobSystem::GetMaxConcurrency() const
{
    // The thread that waits for a barrier executes jobs too.
    return static_cast<int>(m_jobs.workerCount()) + 1;
}

JPH::JobSystem::JobHandle JoltJobSystem::CreateJob(const char* name, JPH::ColorArg color,
                                                   const JobFunction& function, JPH::uint32 dependencies)
{
    const JPH::uint32 index = m_pool.ConstructObject(name, color, this, function, dependencies);
    ILL_CHECK(index != decltype(m_pool)::cInvalidObjectIndex, "out of physics jobs");
    Job* job = &m_pool.Get(index);
    JobHandle handle(job);
    if (dependencies == 0)
    {
        QueueJob(job);
    }
    return handle;
}

void JoltJobSystem::QueueJob(Job* job)
{
    // Execute() runs a job at most once, so the barrier's waiting thread and a worker may both try.
    // Jolt queues jobs from inside other jobs, so a job must never run here: nesting jobs on one thread
    // breaks Jolt's per-thread body access checks. When the engine queue is full the job is left to the
    // barrier's waiting thread, which executes every job that is ready (all of ours belong to the
    // barrier of PhysicsSystem::Update) and wakes up whenever one of them finishes.
    job->AddRef();
    if (!m_jobs.trySubmit(
            [job] {
                job->Execute();
                job->Release();
            },
            &m_pending))
    {
        job->Release();
    }
}

void JoltJobSystem::QueueJobs(Job** jobs, JPH::uint count)
{
    for (JPH::uint i = 0; i < count; ++i)
    {
        QueueJob(jobs[i]);
    }
}

void JoltJobSystem::FreeJob(Job* job)
{
    m_pool.DestructObject(job);
}

} // namespace illusion::detail
