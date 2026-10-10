#pragma once

/// @file
/// Move-only, type-erased callable with fixed inline storage. Never allocates.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace illusion
{

template <class Signature, usize Capacity = 48>
class InplaceFunction;

/// Like std::function, but the callable is stored inline in @p Capacity bytes and the wrapper is
/// move-only. Assigning a callable larger than @p Capacity is a compile-time error, which keeps hot
/// paths (jobs, events) free of heap allocations.
template <class R, class... Args, usize Capacity>
class InplaceFunction<R(Args...), Capacity>
{
public:
    /// Creates an empty function.
    InplaceFunction() noexcept = default;

    /// Creates an empty function.
    InplaceFunction(std::nullptr_t) noexcept {}

    /// Stores @p callable inline.
    template <class F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, InplaceFunction> &&
                 std::is_invocable_r_v<R, std::remove_cvref_t<F>&, Args...>)
    InplaceFunction(F&& callable) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<F>, F&&>)
    {
        using Fn = std::remove_cvref_t<F>;
        static_assert(sizeof(Fn) <= Capacity, "Callable too large for InplaceFunction; capture less "
                                              "(e.g. a pointer to a struct) or raise Capacity");
        static_assert(alignof(Fn) <= alignof(std::max_align_t), "Callable is over-aligned");
        static_assert(std::is_nothrow_move_constructible_v<Fn>,
                      "Callable must be nothrow move constructible");
        ::new (static_cast<void*>(m_storage)) Fn(std::forward<F>(callable));
        m_ops = &kOpsFor<Fn>;
    }

    /// Moves the callable out of @p other, leaving it empty.
    InplaceFunction(InplaceFunction&& other) noexcept { moveFrom(other); }

    /// Destroys the callable and moves the one from @p other in, leaving @p other empty.
    InplaceFunction& operator=(InplaceFunction&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    InplaceFunction(const InplaceFunction&) = delete;
    InplaceFunction& operator=(const InplaceFunction&) = delete;

    ~InplaceFunction() { reset(); }

    /// Destroys the stored callable, leaving the function empty.
    void reset() noexcept
    {
        if (m_ops != nullptr)
        {
            m_ops->destroy(m_storage);
            m_ops = nullptr;
        }
    }

    /// Returns true if a callable is stored.
    [[nodiscard]] explicit operator bool() const noexcept { return m_ops != nullptr; }

    /// Invokes the stored callable. The function must not be empty.
    R operator()(Args... args)
    {
        ILL_ASSERT(m_ops != nullptr, "calling an empty InplaceFunction");
        return m_ops->invoke(m_storage, std::forward<Args>(args)...);
    }

private:
    struct Ops
    {
        using InvokeFn = R (*)(void* storage, Args&&... args);
        using RelocateFn = void (*)(void* dst, void* src) noexcept;
        using DestroyFn = void (*)(void* storage) noexcept;

        // User-provided constructor: avoids MSVC warning C4268 for the constexpr ops tables.
        constexpr Ops(InvokeFn invokeFn, RelocateFn relocateFn, DestroyFn destroyFn) noexcept
            : invoke(invokeFn)
            , relocate(relocateFn)
            , destroy(destroyFn)
        {
        }

        InvokeFn invoke;
        RelocateFn relocate;
        DestroyFn destroy;
    };

    template <class Fn>
    static R invokeImpl(void* storage, Args&&... args)
    {
        return (*static_cast<Fn*>(storage))(std::forward<Args>(args)...);
    }

    template <class Fn>
    static void relocateImpl(void* dst, void* src) noexcept
    {
        Fn* source = static_cast<Fn*>(src);
        ::new (dst) Fn(std::move(*source));
        std::destroy_at(source);
    }

    template <class Fn>
    static void destroyImpl(void* storage) noexcept
    {
        std::destroy_at(static_cast<Fn*>(storage));
    }

    template <class Fn>
    static constexpr Ops kOpsFor{&invokeImpl<Fn>, &relocateImpl<Fn>, &destroyImpl<Fn>};

    void moveFrom(InplaceFunction& other) noexcept
    {
        if (other.m_ops != nullptr)
        {
            other.m_ops->relocate(m_storage, other.m_storage);
            m_ops = other.m_ops;
            other.m_ops = nullptr;
        }
    }

    alignas(std::max_align_t) std::byte m_storage[Capacity];
    const Ops* m_ops = nullptr;
};

} // namespace illusion
