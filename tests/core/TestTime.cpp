#include <illusion/core/Time.hpp>

#include <doctest/doctest.h>

using namespace illusion;

TEST_CASE("monotonicSeconds never goes backwards")
{
    f64 previous = monotonicSeconds();
    for (int i = 0; i < 1000; ++i)
    {
        const f64 now = monotonicSeconds();
        CHECK(now >= previous);
        previous = now;
    }
}

TEST_CASE("Stopwatch measures non-negative elapsed time and restarts")
{
    Stopwatch watch;
    CHECK(watch.elapsedSeconds() >= 0.0);
    const f64 elapsed = watch.restart();
    CHECK(elapsed >= 0.0);
    CHECK(watch.elapsedSeconds() >= 0.0);
}

TEST_CASE("FrameClock produces deltas, totals and frame indices")
{
    FrameClock clock(0.25);
    const FrameTime& first = clock.tick(10.0);
    CHECK(first.deltaSeconds == 0.0);
    CHECK(first.frameIndex == 0);

    clock.tick(10.016);
    CHECK(clock.current().deltaSeconds == doctest::Approx(0.016));
    CHECK(clock.current().frameIndex == 1);

    clock.tick(10.050);
    CHECK(clock.current().deltaSeconds == doctest::Approx(0.034));
    CHECK(clock.current().totalSeconds == doctest::Approx(0.050));
    CHECK(clock.current().frameIndex == 2);
}

TEST_CASE("FrameClock clamps long stalls and ignores time going backwards")
{
    FrameClock clock(0.1);
    clock.tick(0.0);
    clock.tick(5.0); // e.g. a breakpoint
    CHECK(clock.current().deltaSeconds == doctest::Approx(0.1));
    CHECK(clock.current().totalSeconds == doctest::Approx(0.1));

    clock.tick(4.0); // backwards
    CHECK(clock.current().deltaSeconds == 0.0);
    clock.tick(5.05); // measured from the latest time seen (5.0)
    CHECK(clock.current().deltaSeconds == doctest::Approx(0.05));
}

TEST_CASE("FixedTimestep converts frame time into whole steps and an interpolation factor")
{
    FixedTimestep step(0.01);
    CHECK(step.advance(0.025) == 2);
    CHECK(step.alpha() == doctest::Approx(0.5));
    CHECK(step.advance(0.005) == 1);
    CHECK(step.alpha() == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(step.advance(0.0) == 0);
    CHECK(step.totalSteps() == 3);
    CHECK(step.stepSeconds() == 0.01);
}

TEST_CASE("FixedTimestep limits steps per frame and drops the backlog")
{
    FixedTimestep step(0.01, 4);
    CHECK(step.advance(1.0) == 4);
    CHECK(step.alpha() < 1.0);
    CHECK(step.advance(0.0) == 0); // the backlog was dropped, not carried over
    CHECK(step.advance(-1.0) == 0);
}
