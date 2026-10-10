#include <illusion/core/Thread.hpp>

#include <doctest/doctest.h>

#include <set>
#include <thread>

using namespace illusion;

TEST_CASE("currentThreadIndex is stable per thread and unique across threads")
{
    const u32 mainIndex = currentThreadIndex();
    CHECK(currentThreadIndex() == mainIndex);

    u32 otherIndex = mainIndex;
    std::thread other([&otherIndex] {
        setCurrentThreadName("illusion test thread with a long name");
        otherIndex = currentThreadIndex();
    });
    other.join();
    CHECK(otherIndex != mainIndex);
}

TEST_CASE("hardwareThreadCount is at least one")
{
    CHECK(hardwareThreadCount() >= 1);
    cpuRelax();
}
