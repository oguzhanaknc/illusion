# 0005. Job System with a bounded lock-free queue and helping waits

- Status: Accepted
- Date: 2026-10-10

## Context

All engine subsystems need to spread work across cores without per-frame allocations and without
deadlocks when jobs wait on other jobs. Options considered:

- `std::async`: allocates and gives no control over threads.
- Fiber-based job systems: fast waits, but complex, platform-specific and hard to debug.
- Thread pools with a shared queue or work-stealing deques.

## Decision

- A fixed pool of worker threads (default: hardware threads - 1).
- Jobs are `InplaceFunction<void(), 48>` objects stored by value in a bounded multi-producer
  multi-consumer queue (the algorithm by Dmitry Vyukov). Submitting never allocates; when the queue is
  full the job runs inline on the submitting thread.
- Completion is tracked with `JobCounter`. `wait()` executes queued jobs while the counter is non-zero,
  so jobs can submit and wait on child jobs without deadlocking.
- Idle workers spin briefly, then sleep on a condition variable. Submitters only take the lock when a
  worker is actually sleeping.

## Consequences

- Simple, portable, TSan-clean and allocation-free. Benchmarks guard submit/wait cost and `parallelFor`
  throughput.
- One shared queue can become a contention point at high core counts. If benchmarks show it, switch to
  per-worker work-stealing deques behind the same API.
- A waiting thread may pick up an unrelated long job, which can delay the waiter. Keep jobs short and
  never block inside jobs.
- No job priorities yet; they will be added when the renderer and asset streaming need them.
