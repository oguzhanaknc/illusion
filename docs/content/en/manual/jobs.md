# Job System

The Job System runs small units of work (**Jobs**) on a fixed pool of worker threads.

Header: `#include <illusion/core/JobSystem.hpp>`

## Creating the Job System

```cpp
illusion::JobSystem jobs;                                // hardware threads - 1 workers
illusion::JobSystem small(illusion::JobSystemDesc{2, 1024}); // 2 workers, 1024 queue slots
```

The engine creates one Job System at start-up; you normally use that instance rather than creating
your own.

## Submitting and waiting

```cpp
illusion::JobCounter counter;
for (int i = 0; i < 64; ++i)
{
    jobs.submit([i] { processTile(i); }, &counter);
}
jobs.wait(counter); // runs queued Jobs on this thread while waiting
```

A `JobCounter` counts unfinished Jobs. `wait()` does not block idly: the waiting thread executes queued
Jobs until the counter reaches zero. Jobs may therefore submit and wait for other Jobs without
deadlocking.

## Parallel loops

`parallelFor()` splits a range into batches and returns when every batch is done:

```cpp
std::vector<float> values(100000, 1.0f);
jobs.parallelFor(static_cast<illusion::u32>(values.size()), 4096,
                 [&values](illusion::u32 begin, illusion::u32 end) {
                     for (illusion::u32 i = begin; i < end; ++i)
                     {
                         values[i] *= 2.0f;
                     }
                 });
```

Choose a batch size that gives each Job at least a few microseconds of work.

## Rules

- **Jobs never allocate.** A Job is stored inline in a 64-byte slot of a lock-free queue. Captures are
  limited to 48 bytes; capture a pointer to a larger structure instead.
- **Jobs must not throw.** An exception escaping a Job terminates the process.
- **Jobs must not block** on anything other than `JobSystem::wait()`. Do not hold a mutex across a wait
  and do not perform blocking I/O in a Job; use a dedicated thread for that.
- When the queue is full, `submit()` runs the Job immediately on the calling thread.
- A `JobCounter` must outlive every Job submitted with it.

`JobSystem::currentWorkerIndex()` returns the index of the calling worker (or -1 on other threads),
which is useful for per-thread scratch memory.
