#pragma once

// Runs Jolt's simulation jobs on the engine JobSystem, so physics shares the worker threads with the
// rest of the engine instead of starting its own.

#include <illusion/core/JobSystem.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

namespace illusion::detail
{

class JoltJobSystem final : public JPH::JobSystemWithBarrier
{
public:
    JoltJobSystem(illusion::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers);
    ~JoltJobSystem() override;

    JoltJobSystem(const JoltJobSystem&) = delete;
    JoltJobSystem& operator=(const JoltJobSystem&) = delete;

    [[nodiscard]] int GetMaxConcurrency() const override;
    JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                        JPH::uint32 dependencies = 0) override;

protected:
    void QueueJob(Job* job) override;
    void QueueJobs(Job** jobs, JPH::uint count) override;
    void FreeJob(Job* job) override;

private:
    illusion::JobSystem& m_jobs;
    JobCounter m_pending; // engine jobs that still reference a Jolt job
    JPH::FixedSizeFreeList<Job> m_pool;
};

} // namespace illusion::detail
