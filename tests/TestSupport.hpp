#pragma once

// Shared helpers for illusion unit tests.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>

#include <atomic>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace illusion::test
{

/// Thrown by ScopedAssertCatcher's handler instead of aborting.
struct AssertionFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Installs an assert handler that throws AssertionFailure for the lifetime of the object, so tests
/// can check that ILL_ASSERT fires. Only use around calls that are not noexcept.
class ScopedAssertCatcher
{
public:
    ScopedAssertCatcher()
        : m_previous(setAssertHandler(&handler))
    {
    }
    ~ScopedAssertCatcher() { setAssertHandler(m_previous); }
    ScopedAssertCatcher(const ScopedAssertCatcher&) = delete;
    ScopedAssertCatcher& operator=(const ScopedAssertCatcher&) = delete;

private:
    static void handler(const AssertInfo& info)
    {
        throw AssertionFailure(std::string(info.expression) + ": " + std::string(info.message));
    }

    AssertHandler m_previous;
};

/// Counts constructions and destructions to verify the ECS manages object lifetimes exactly.
struct LifetimeCounter
{
    static inline int alive = 0;
    static inline int constructed = 0;

    int value = 0;

    LifetimeCounter() noexcept
    {
        ++alive;
        ++constructed;
    }
    explicit LifetimeCounter(int v) noexcept
        : value(v)
    {
        ++alive;
        ++constructed;
    }
    LifetimeCounter(const LifetimeCounter& other) noexcept
        : value(other.value)
    {
        ++alive;
        ++constructed;
    }
    LifetimeCounter(LifetimeCounter&& other) noexcept
        : value(other.value)
    {
        other.value = -1;
        ++alive;
        ++constructed;
    }
    LifetimeCounter& operator=(const LifetimeCounter&) noexcept = default;
    LifetimeCounter& operator=(LifetimeCounter&&) noexcept = default;
    ~LifetimeCounter() { --alive; }

    static void reset() noexcept
    {
        alive = 0;
        constructed = 0;
    }
};

/// Counts the heap allocations made on the calling thread while it is alive: every form of global operator
/// new, and in sanitizer builds malloc() as well. Implemented in AllocationCounter.cpp.
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter();
    ScopedAllocationCounter(const ScopedAllocationCounter&) = delete;
    ScopedAllocationCounter& operator=(const ScopedAllocationCounter&) = delete;

    /// Allocations since construction.
    [[nodiscard]] usize count() const noexcept;

private:
    usize m_start;
};

/// Creates a unique empty directory under the system temp directory and deletes it afterwards.
struct TempDirectory
{
    std::filesystem::path path;

    TempDirectory()
    {
        // CTest runs test cases as parallel processes, so names must be unique across processes.
        static std::atomic<int> counter{0};
        const auto unique = std::random_device{}();
        path = std::filesystem::temp_directory_path() /
               ("illusion-test-" + std::to_string(unique) + "-" + std::to_string(counter.fetch_add(1)));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

} // namespace illusion::test
