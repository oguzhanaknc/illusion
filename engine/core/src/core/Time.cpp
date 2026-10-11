#include <illusion/core/Assert.hpp>
#include <illusion/core/Time.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace illusion
{

f64 monotonicSeconds() noexcept
{
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point epoch = Clock::now();
    return std::chrono::duration<f64>(Clock::now() - epoch).count();
}

// --- Stopwatch ----------------------------------------------------------------------------------------

Stopwatch::Stopwatch() noexcept
    : m_start(monotonicSeconds())
{
}

f64 Stopwatch::elapsedSeconds() const noexcept
{
    return monotonicSeconds() - m_start;
}

f64 Stopwatch::restart() noexcept
{
    const f64 now = monotonicSeconds();
    const f64 elapsed = now - m_start;
    m_start = now;
    return elapsed;
}

// --- FrameClock ---------------------------------------------------------------------------------------

FrameClock::FrameClock(f64 maxDeltaSeconds) noexcept
    : m_maxDelta(maxDeltaSeconds)
{
    ILL_ASSERT(maxDeltaSeconds > 0.0, "maxDeltaSeconds must be positive");
}

const FrameTime& FrameClock::tick(f64 nowSeconds) noexcept
{
    if (!m_started)
    {
        m_started = true;
        m_lastNow = nowSeconds;
        m_current = FrameTime{};
        return m_current;
    }
    const f64 delta = std::clamp(nowSeconds - m_lastNow, 0.0, m_maxDelta);
    m_lastNow = std::max(m_lastNow, nowSeconds);
    m_current.deltaSeconds = delta;
    m_current.totalSeconds += delta;
    ++m_current.frameIndex;
    return m_current;
}

// --- FixedTimestep ------------------------------------------------------------------------------------

FixedTimestep::FixedTimestep(f64 stepSeconds, u32 maxStepsPerFrame) noexcept
    : m_step(stepSeconds)
    , m_maxSteps(maxStepsPerFrame)
{
    ILL_ASSERT(stepSeconds > 0.0, "stepSeconds must be positive");
    ILL_ASSERT(maxStepsPerFrame > 0, "maxStepsPerFrame must be positive");
}

u32 FixedTimestep::advance(f64 deltaSeconds) noexcept
{
    m_accumulator += std::max(deltaSeconds, 0.0);
    u32 steps = 0;
    while (m_accumulator >= m_step && steps < m_maxSteps)
    {
        m_accumulator -= m_step;
        ++steps;
    }
    if (steps == m_maxSteps && m_accumulator >= m_step)
    {
        // Too far behind: drop whole steps we cannot afford, keep the fractional remainder.
        m_accumulator = std::fmod(m_accumulator, m_step);
    }
    m_totalSteps += steps;
    return steps;
}

} // namespace illusion
