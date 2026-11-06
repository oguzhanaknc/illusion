#include "TestSupport.hpp"

#include <illusion/core/FrameProfiler.hpp>

#include <doctest/doctest.h>

using namespace illusion;

namespace
{

// A clock the tests move by hand: every reading advances it by one millisecond, so durations count the
// readings taken inside a scope.
f64 g_now = 0.0;

f64 fakeClock() noexcept
{
    const f64 now = g_now;
    g_now += 0.001;
    return now;
}

// A clock that advances 0.7 microseconds per reading.
f64 g_tinyNow = 0.0;

f64 tinyClock() noexcept
{
    const f64 now = g_tinyNow;
    g_tinyNow += 0.0000007;
    return now;
}

FrameProfiler makeProfiler(FrameProfilerDesc desc = {})
{
    g_now = 0.0;
    return FrameProfiler(desc, &fakeClock);
}

} // namespace

TEST_CASE("a frame records its scopes in the order they began, with depths and durations")
{
    FrameProfiler profiler = makeProfiler();
    profiler.beginFrame(); // t = 0
    {
        FrameProfiler::Scope update(profiler, "update"); // t = 1
        {
            FrameProfiler::Scope physics(profiler, "physics"); // t = 2, ends at 3
        }
        g_now += 0.010;
    } // ends at 14
    profiler.beginScope("render"); // t = 15
    profiler.endScope();           // 16
    profiler.endFrame();           // 17

    REQUIRE(profiler.frameCount() == 1);
    const ProfileFrame frame = profiler.frame(0);
    CHECK(frame.index == 0);
    CHECK(frame.durationMs == doctest::Approx(17.0));
    CHECK_FALSE(frame.truncated);
    REQUIRE(frame.scopes.size() == 3);
    CHECK(frame.scopes[0].name == "update");
    CHECK(frame.scopes[0].depth == 0);
    CHECK(frame.scopes[0].startMs == doctest::Approx(1.0));
    CHECK(frame.scopes[0].durationMs == doctest::Approx(13.0));
    CHECK(frame.scopes[1].name == "physics");
    CHECK(frame.scopes[1].depth == 1);
    CHECK(frame.scopes[1].durationMs == doctest::Approx(1.0));
    CHECK(frame.scopes[2].name == "render");
    CHECK(frame.scopes[2].depth == 0);
    // The same name in another place is another place.
    CHECK(frame.scopes[0].path != frame.scopes[1].path);
}

TEST_CASE("the ring keeps the newest frames and a frame that was not ended is discarded")
{
    FrameProfiler profiler = makeProfiler({.frames = 3});
    for (int i = 0; i < 5; ++i)
    {
        profiler.beginFrame();
        profiler.endFrame();
    }
    CHECK(profiler.frameCount() == 3);
    CHECK(profiler.frame(0).index == 4);
    CHECK(profiler.frame(2).index == 2);

    profiler.beginFrame();
    profiler.beginScope("lost");
    profiler.beginFrame(); // the unfinished frame 5 is dropped
    profiler.endFrame();
    CHECK(profiler.frameCount() == 3);
    CHECK(profiler.frame(0).index == 6);
    CHECK(profiler.frame(0).scopes.empty());
    CHECK(profiler.frame(1).index == 4);
    CHECK(profiler.frameIndex() == 7);
}

TEST_CASE("endFrame closes scopes left open and scopes outside a frame are ignored")
{
    FrameProfiler profiler = makeProfiler();
    profiler.beginScope("outside");
    profiler.endScope();
    profiler.beginFrame();
    profiler.beginScope("open");
    profiler.beginScope("inner");
    profiler.endFrame();
    const ProfileFrame frame = profiler.frame(0);
    REQUIRE(frame.scopes.size() == 2);
    CHECK(frame.scopes[0].name == "open");
    CHECK(frame.scopes[0].durationMs > 0.0f);
    CHECK(frame.scopes[1].durationMs > 0.0f);
}

TEST_CASE("ending a scope that is not open asserts")
{
    test::ScopedAssertCatcher catcher;
    FrameProfiler profiler = makeProfiler();
    profiler.beginFrame();
    CHECK_THROWS_AS(profiler.endScope(), test::AssertionFailure);
}

TEST_CASE("scopes beyond the per-frame limit or the depth limit are dropped and the frame is truncated")
{
    FrameProfiler profiler = makeProfiler({.frames = 2, .scopesPerFrame = 2, .maxDepth = 2});
    profiler.beginFrame();
    for (int i = 0; i < 3; ++i)
    {
        FrameProfiler::Scope scope(profiler, "many");
    }
    profiler.endFrame();
    CHECK(profiler.frame(0).scopes.size() == 2);
    CHECK(profiler.frame(0).truncated);

    profiler.beginFrame();
    {
        FrameProfiler::Scope a(profiler, "a");
        FrameProfiler::Scope b(profiler, "b");
        FrameProfiler::Scope c(profiler, "c"); // too deep
        FrameProfiler::Scope d(profiler, "d"); // too deep
    }
    FrameProfiler::Scope after(profiler, "after"); // the frame is full; it ends after endFrame()
    profiler.endFrame();
    const ProfileFrame frame = profiler.frame(0);
    REQUIRE(frame.scopes.size() == 2);
    CHECK(frame.scopes[0].name == "a");
    CHECK(frame.scopes[1].name == "b");
    CHECK(frame.truncated);
}

TEST_CASE("a paused profiler keeps its frames and records again after resume")
{
    FrameProfiler profiler = makeProfiler();
    profiler.beginFrame();
    profiler.endFrame();
    profiler.pause();
    CHECK(profiler.paused());
    profiler.beginFrame();
    profiler.beginScope("ignored");
    profiler.endScope();
    profiler.endFrame();
    CHECK(profiler.frameCount() == 1);
    profiler.resume();
    profiler.beginFrame();
    profiler.endFrame();
    CHECK(profiler.frameCount() == 2);
    CHECK(profiler.frame(0).index == 2);
    profiler.clear();
    CHECK(profiler.frameCount() == 0);
}

TEST_CASE("the average time of a place never exceeds its largest time, even after f32 rounding")
{
    // Seven frames of one 0.0007 ms scope: the f32 sum divided by seven rounds above 0.0007.
    g_tinyNow = 0.0;
    FrameProfiler profiler(FrameProfilerDesc{}, &tinyClock);
    for (int i = 0; i < 7; ++i)
    {
        profiler.beginFrame();
        profiler.beginScope("tiny");
        profiler.endScope();
        profiler.endFrame();
    }
    std::vector<ProfileScopeStats> stats;
    profiler.summarize(7, stats);
    REQUIRE(stats.size() == 1);
    CHECK(stats[0].averageMs <= stats[0].maxMs);
}

TEST_CASE("summarize sums the time per place and frame, averaged over the frames, in newest-frame order")
{
    FrameProfiler profiler = makeProfiler();
    // Frame 0: "old" only. Frames 1 and 2: "update" twice (1 ms each), and "render" (1 ms) in frame 2.
    profiler.beginFrame();
    profiler.beginScope("old");
    profiler.endScope();
    profiler.endFrame();
    for (int i = 0; i < 2; ++i)
    {
        profiler.beginFrame();
        if (i == 1)
        {
            g_now += 0.004; // the second frame's update takes 5 ms per call
        }
        for (int call = 0; call < 2; ++call)
        {
            profiler.beginScope("update");
            g_now += i == 1 ? 0.004 : 0.0;
            profiler.endScope();
        }
        if (i == 1)
        {
            profiler.beginScope("render");
            profiler.endScope();
        }
        profiler.endFrame();
    }

    std::vector<ProfileScopeStats> stats;
    profiler.summarize(2, stats);
    REQUIRE(stats.size() == 2);
    CHECK(stats[0].name == "update");
    CHECK(stats[0].calls == 2);
    CHECK(stats[0].lastMs == doctest::Approx(10.0));
    CHECK(stats[0].maxMs == doctest::Approx(10.0));
    CHECK(stats[0].averageMs == doctest::Approx(6.0)); // (2 + 10) / 2
    CHECK(stats[1].name == "render");
    CHECK(stats[1].averageMs == doctest::Approx(0.5));

    profiler.summarize(100, stats); // clamped to the three frames
    REQUIRE(stats.size() == 3);
    CHECK(stats[2].name == "old");
    CHECK(stats[2].calls == 0);
    CHECK(stats[2].lastMs == 0.0f);
    CHECK(stats[2].averageMs == doctest::Approx(1.0 / 3.0));

    profiler.summarize(0, stats);
    CHECK(stats.empty());
}

TEST_CASE("recording and summarizing frames does not allocate")
{
    FrameProfiler profiler = makeProfiler({.frames = 8, .scopesPerFrame = 16, .maxDepth = 4});
    std::vector<ProfileScopeStats> stats;
    stats.reserve(16);
    test::ScopedAllocationCounter counter;
    for (int i = 0; i < 20; ++i)
    {
        profiler.beginFrame();
        {
            FrameProfiler::Scope outer(profiler, "outer");
            FrameProfiler::Scope inner(profiler, "inner");
        }
        profiler.endFrame();
        profiler.summarize(8, stats);
    }
    CHECK(counter.count() == 0);
    CHECK(stats.size() == 2);
}
