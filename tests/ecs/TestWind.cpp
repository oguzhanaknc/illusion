#include "TestSupport.hpp"

#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace illusion;

TEST_CASE("advanceWind gives new winds a clock and advances every clock")
{
    World world;
    const Entity first = world.create(Wind{});
    advanceWind(world, 0.5f);
    REQUIRE(world.has<WindState>(first));
    CHECK(world.get<WindState>(first).time == doctest::Approx(0.5));
    CHECK(world.get<WindState>(first).previousTime == doctest::Approx(0.0));

    const Entity second = world.create(Wind{});
    advanceWind(world, 0.25f);
    CHECK(world.get<WindState>(first).time == doctest::Approx(0.75));
    CHECK(world.get<WindState>(first).previousTime == doctest::Approx(0.5));
    CHECK(world.get<WindState>(second).time == doctest::Approx(0.25));

    // Negative and non-finite steps do not move the clock.
    advanceWind(world, -1.0f);
    advanceWind(world, std::numeric_limits<f32>::quiet_NaN());
    CHECK(world.get<WindState>(first).time == doctest::Approx(0.75));
    CHECK(world.get<WindState>(first).previousTime == doctest::Approx(0.75));
}

TEST_CASE("gusts average one, stay within their strength and travel downwind")
{
    const Wind wind{
        .direction = Vec3(2.0f, 0.0f, 0.0f), .speed = 4.0f, .gustStrength = 0.5f, .gustFrequency = 0.25f};
    f64 sum = 0.0;
    constexpr int kSamples = 4000;
    for (int i = 0; i < kSamples; ++i)
    {
        const f32 time = static_cast<f32>(i) * 0.01f; // ten periods of the slowest wave
        const f32 gust = windGust(wind, time, Vec3(0.0f));
        CHECK(gust >= 0.5f - 1e-5f);
        CHECK(gust <= 1.5f + 1e-5f);
        sum += static_cast<f64>(gust);
    }
    CHECK(sum / kSamples == doctest::Approx(1.0).epsilon(0.02));

    // The gust at the origin reaches a point 8 m downwind 2 s later (at 4 m/s).
    CHECK(windGust(wind, 3.0f, Vec3(8.0f, 0.0f, 0.0f)) == doctest::Approx(windGust(wind, 1.0f, Vec3(0.0f))));
    // The velocity follows the normalized direction.
    const Vec3 velocity = windVelocity(wind, 1.0f, Vec3(0.0f));
    CHECK(velocity.x == doctest::Approx(4.0f * windGust(wind, 1.0f, Vec3(0.0f))));
    CHECK(velocity.y == 0.0f);

    const Wind steady{.direction = Vec3(0.0f, 0.0f, -1.0f), .speed = 3.0f, .gustStrength = 0.0f};
    CHECK(windVelocity(steady, 12.0f, Vec3(5.0f)).z == doctest::Approx(-3.0));
    CHECK(windVelocity(Wind{.direction = Vec3(0.0f)}, 1.0f, Vec3(0.0f)) == Vec3(0.0f));
    CHECK(windVelocity(Wind{.speed = -2.0f}, 1.0f, Vec3(0.0f)) == Vec3(0.0f));
}

TEST_CASE("Wind is a reflected singleton and its clock is runtime state")
{
    ComponentRegistry registry;
    reflectCoreComponents(registry);
    const TypeReflection* wind = registry.reflection(registry.findByStableName("illusion.Wind"));
    REQUIRE(wind != nullptr);
    CHECK(wind->meta.singleton);
    CHECK_FALSE(wind->meta.transient);
    CHECK(wind->fields.size() == 5);
    const TypeReflection* state = registry.reflection(registry.findByStableName("illusion.WindState"));
    REQUIRE(state != nullptr);
    CHECK(state->meta.transient);
}

TEST_CASE("advanceWind does not allocate once every wind has its clock")
{
    World world;
    world.create(Wind{});
    advanceWind(world, 0.1f);
    const test::ScopedAllocationCounter allocations;
    for (int i = 0; i < 100; ++i)
    {
        advanceWind(world, 0.1f);
    }
    CHECK(allocations.count() == 0);
}
