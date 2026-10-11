#pragma once

/// @file
/// Time measurement for frame loops: a monotonic clock, a stopwatch, per-frame timing and a fixed
/// timestep accumulator for simulation (physics, gameplay ticks).
///
/// FrameClock and FixedTimestep take time values as parameters instead of reading the clock
/// themselves, so they are deterministic and can be driven by recorded or simulated time.

#include <illusion/core/Base.hpp>

namespace illusion
{

/// Seconds since an unspecified, fixed point in the past. Monotonic and high resolution.
[[nodiscard]] f64 monotonicSeconds() noexcept;

/// Measures elapsed time since construction or the last restart().
class Stopwatch
{
public:
    /// Starts measuring.
    Stopwatch() noexcept;

    /// Seconds since construction or the last restart().
    [[nodiscard]] f64 elapsedSeconds() const noexcept;

    /// Restarts the measurement and returns the time elapsed before the restart.
    f64 restart() noexcept;

private:
    f64 m_start;
};

/// Timing information for one frame.
struct FrameTime
{
    f64 deltaSeconds = 0.0; ///< Time since the previous frame, clamped to the clock's maximum.
    f64 totalSeconds = 0.0; ///< Sum of all clamped deltas: game time, which does not jump over long stalls.
    u64 frameIndex = 0;     ///< 0 for the first frame, then incremented by one per tick.
};

/// Produces per-frame timing from monotonic timestamps.
class FrameClock
{
public:
    /// Creates a clock. Deltas larger than @p maxDeltaSeconds (for example after a breakpoint or while
    /// the window was dragged) are clamped so the simulation does not jump.
    explicit FrameClock(f64 maxDeltaSeconds = 0.25) noexcept;

    /// Starts a new frame at time @p nowSeconds (from monotonicSeconds()). The first tick has a delta of
    /// zero. Times that go backwards produce a delta of zero.
    const FrameTime& tick(f64 nowSeconds) noexcept;

    /// Timing of the current frame.
    [[nodiscard]] const FrameTime& current() const noexcept { return m_current; }

private:
    FrameTime m_current;
    f64 m_lastNow = 0.0;
    f64 m_maxDelta;
    bool m_started = false;
};

/// Accumulates frame time and converts it into a whole number of fixed-size simulation steps.
///
/// ```cpp
/// FixedTimestep physics(1.0 / 60.0);
/// const u32 steps = physics.advance(frame.deltaSeconds);
/// for (u32 i = 0; i < steps; ++i) { simulate(physics.stepSeconds()); }
/// render(physics.alpha()); // interpolate between the last two simulation states
/// ```
class FixedTimestep
{
public:
    /// Creates an accumulator for steps of @p stepSeconds (> 0). At most @p maxStepsPerFrame steps are
    /// returned per call; time beyond that is dropped to avoid a "spiral of death".
    explicit FixedTimestep(f64 stepSeconds, u32 maxStepsPerFrame = 8) noexcept;

    /// Adds @p deltaSeconds and returns how many steps to simulate now.
    [[nodiscard]] u32 advance(f64 deltaSeconds) noexcept;

    /// Fraction of a step left in the accumulator, in [0, 1). Use it to interpolate rendering.
    [[nodiscard]] f64 alpha() const noexcept { return m_accumulator / m_step; }

    /// Size of one step in seconds.
    [[nodiscard]] f64 stepSeconds() const noexcept { return m_step; }

    /// Total number of steps produced so far.
    [[nodiscard]] u64 totalSteps() const noexcept { return m_totalSteps; }

private:
    f64 m_step;
    f64 m_accumulator = 0.0;
    u64 m_totalSteps = 0;
    u32 m_maxSteps;
};

} // namespace illusion
