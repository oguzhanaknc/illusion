#pragma once

/// @file
/// Expected<T, E>: a value or an error, for recoverable failures (C++20 has no std::expected).
///
/// ```cpp
/// Expected<u32, std::string> parsePort(std::string_view text)
/// {
///     if (text.empty())
///     {
///         return unexpected(std::string("empty port"));
///     }
///     return 8080u;
/// }
///
/// const auto port = parsePort(argument);
/// if (!port)
/// {
///     ILL_LOG_ERROR("Net", "{}", port.error());
/// }
/// ```
///
/// Accessing the value of an error (or the error of a value) is a programmer error caught by ILL_ASSERT.
/// Engine code never throws; Expected never throws either, but it does not catch exceptions thrown by the
/// constructors of T or E.

#include <illusion/core/Assert.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace illusion
{

/// Wraps an error so that it can be converted into an Expected. Create one with unexpected().
template <class E>
class Unexpected
{
    static_assert(std::is_object_v<E> && !std::is_array_v<E> && !std::is_const_v<E> && !std::is_volatile_v<E>,
                  "the error type must be a non-const, non-array object type");

public:
    /// Stores @p error.
    constexpr explicit Unexpected(const E& error)
        : m_error(error)
    {
    }

    /// Stores @p error.
    constexpr explicit Unexpected(E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : m_error(std::move(error))
    {
    }

    /// The stored error.
    [[nodiscard]] constexpr E& error() & noexcept { return m_error; }
    /// @copydoc error()
    [[nodiscard]] constexpr const E& error() const& noexcept { return m_error; }
    /// @copydoc error()
    [[nodiscard]] constexpr E&& error() && noexcept { return std::move(m_error); }

private:
    E m_error;
};

/// Deduces the error type of an Unexpected.
template <class E>
Unexpected(E) -> Unexpected<E>;

/// Returns @p error wrapped in an Unexpected: `return unexpected(JsonError{...});`. The error type decays,
/// so `unexpected("message")` holds a `const char*`.
template <class E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> unexpected(E&& error)
{
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

/// Implementation details. Not part of the public API.
namespace detail
{
template <class T>
inline constexpr bool kIsUnexpected = false;
template <class E>
inline constexpr bool kIsUnexpected<Unexpected<E>> = true;
} // namespace detail

/// Holds either a value of type @p T or an error of type @p E. Default-constructs to a value-initialized
/// T. Copyable and movable when T and E are; nothrow-movable when both are.
template <class T, class E>
class [[nodiscard]] Expected
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T> && !detail::kIsUnexpected<std::remove_cv_t<T>>,
                  "the value type must be an object type (use Expected<void, E> for no value)");
    static_assert(std::is_object_v<E> && !std::is_array_v<E> && !std::is_const_v<E>,
                  "the error type must be a non-const, non-array object type");
    static_assert(std::is_nothrow_destructible_v<T> && std::is_nothrow_destructible_v<E>,
                  "T and E must be nothrow destructible");

public:
    using ValueType = T; ///< The value type.
    using ErrorType = E; ///< The error type.

    /// Holds a value-initialized T.
    constexpr Expected() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
        : m_value()
        , m_hasValue(true)
    {
    }

    /// Holds a value constructed from @p value.
    template <class U = T>
        requires(std::is_constructible_v<T, U &&> && !std::is_same_v<std::remove_cvref_t<U>, Expected> &&
                 !detail::kIsUnexpected<std::remove_cvref_t<U>>)
    constexpr explicit(!std::is_convertible_v<U&&, T>)
        Expected(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        : m_value(std::forward<U>(value))
        , m_hasValue(true)
    {
    }

    /// Holds a copy of the error of @p error.
    template <class G>
        requires std::is_constructible_v<E, const G&>
    constexpr Expected(const Unexpected<G>& error) noexcept(std::is_nothrow_constructible_v<E, const G&>)
        : m_error(error.error())
        , m_hasValue(false)
    {
    }

    /// Holds the error of @p error.
    template <class G>
        requires std::is_constructible_v<E, G&&>
    constexpr Expected(Unexpected<G>&& error) noexcept(std::is_nothrow_constructible_v<E, G&&>)
        : m_error(std::move(error).error())
        , m_hasValue(false)
    {
    }

    /// Copies the value or the error of @p other.
    constexpr Expected(const Expected& other)
        requires(std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E>)
        : m_hasValue(other.m_hasValue)
    {
        if (m_hasValue)
        {
            std::construct_at(std::addressof(m_value), other.m_value);
        }
        else
        {
            std::construct_at(std::addressof(m_error), other.m_error);
        }
    }

    /// Moves the value or the error of @p other (which keeps a moved-from value or error).
    constexpr Expected(Expected&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                  std::is_nothrow_move_constructible_v<E>)
        requires(std::is_move_constructible_v<T> && std::is_move_constructible_v<E>)
        : m_hasValue(other.m_hasValue)
    {
        if (m_hasValue)
        {
            std::construct_at(std::addressof(m_value), std::move(other.m_value));
        }
        else
        {
            std::construct_at(std::addressof(m_error), std::move(other.m_error));
        }
    }

    /// Replaces the contents with a copy of @p other. If @p other holds the other alternative, the copy
    /// is made before the current contents are destroyed.
    constexpr Expected& operator=(const Expected& other)
        requires(std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E> &&
                 std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E> &&
                 std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_assignable_v<E>)
    {
        if (this != &other)
        {
            Expected copy(other);
            *this = std::move(copy);
        }
        return *this;
    }

    /// Replaces the contents with those of @p other.
    constexpr Expected& operator=(Expected&& other) noexcept
        requires(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E> &&
                 std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_assignable_v<E>)
    {
        if (this == &other)
        {
            return *this;
        }
        if (m_hasValue && other.m_hasValue)
        {
            m_value = std::move(other.m_value);
        }
        else if (!m_hasValue && !other.m_hasValue)
        {
            m_error = std::move(other.m_error);
        }
        else
        {
            destroy();
            if (other.m_hasValue)
            {
                std::construct_at(std::addressof(m_value), std::move(other.m_value));
            }
            else
            {
                std::construct_at(std::addressof(m_error), std::move(other.m_error));
            }
            m_hasValue = other.m_hasValue;
        }
        return *this;
    }

    /// Destroys the value or the error.
    constexpr ~Expected() { destroy(); }

    /// True if a value is held.
    [[nodiscard]] constexpr bool hasValue() const noexcept { return m_hasValue; }

    /// True if a value is held.
    constexpr explicit operator bool() const noexcept { return m_hasValue; }

    /// The value. Asserts that a value is held.
    [[nodiscard]] constexpr T& value() &
    {
        ILL_ASSERT(m_hasValue, "Expected::value() on an error");
        return m_value;
    }

    /// @copydoc value()
    [[nodiscard]] constexpr const T& value() const&
    {
        ILL_ASSERT(m_hasValue, "Expected::value() on an error");
        return m_value;
    }

    /// @copydoc value()
    [[nodiscard]] constexpr T&& value() &&
    {
        ILL_ASSERT(m_hasValue, "Expected::value() on an error");
        return std::move(m_value);
    }

    /// The error. Asserts that an error is held.
    [[nodiscard]] constexpr E& error() &
    {
        ILL_ASSERT(!m_hasValue, "Expected::error() on a value");
        return m_error;
    }

    /// @copydoc error()
    [[nodiscard]] constexpr const E& error() const&
    {
        ILL_ASSERT(!m_hasValue, "Expected::error() on a value");
        return m_error;
    }

    /// @copydoc error()
    [[nodiscard]] constexpr E&& error() &&
    {
        ILL_ASSERT(!m_hasValue, "Expected::error() on a value");
        return std::move(m_error);
    }

    /// The value. Asserts that a value is held.
    [[nodiscard]] constexpr T& operator*() & { return value(); }
    /// @copydoc operator*()
    [[nodiscard]] constexpr const T& operator*() const& { return value(); }
    /// @copydoc operator*()
    [[nodiscard]] constexpr T&& operator*() && { return std::move(*this).value(); }

    /// Accesses a member of the value. Asserts that a value is held.
    [[nodiscard]] constexpr T* operator->() { return std::addressof(value()); }
    /// @copydoc operator->()
    [[nodiscard]] constexpr const T* operator->() const { return std::addressof(value()); }

    /// Returns a copy of the value, or @p fallback converted to T if an error is held.
    template <class U>
    [[nodiscard]] constexpr T valueOr(U&& fallback) const&
    {
        static_assert(std::is_copy_constructible_v<T> && std::is_convertible_v<U&&, T>,
                      "valueOr() needs a copyable T and a fallback convertible to T");
        return m_hasValue ? m_value : static_cast<T>(std::forward<U>(fallback));
    }

    /// Returns the moved value, or @p fallback converted to T if an error is held.
    template <class U>
    [[nodiscard]] constexpr T valueOr(U&& fallback) &&
    {
        static_assert(std::is_move_constructible_v<T> && std::is_convertible_v<U&&, T>,
                      "valueOr() needs a movable T and a fallback convertible to T");
        return m_hasValue ? std::move(m_value) : static_cast<T>(std::forward<U>(fallback));
    }

private:
    constexpr void destroy() noexcept
    {
        if (m_hasValue)
        {
            std::destroy_at(std::addressof(m_value));
        }
        else
        {
            std::destroy_at(std::addressof(m_error));
        }
    }

    union
    {
        T m_value;
        E m_error;
    };
    bool m_hasValue;
};

/// Success without a value, or an error of type @p E. Default-constructs to success.
template <class E>
class [[nodiscard]] Expected<void, E>
{
    static_assert(std::is_object_v<E> && !std::is_array_v<E> && !std::is_const_v<E>,
                  "the error type must be a non-const, non-array object type");
    static_assert(std::is_nothrow_destructible_v<E>, "E must be nothrow destructible");

public:
    using ValueType = void; ///< The value type.
    using ErrorType = E;    ///< The error type.

    /// Holds success.
    constexpr Expected() noexcept
        : m_hasValue(true)
    {
    }

    /// Holds a copy of the error of @p error.
    template <class G>
        requires std::is_constructible_v<E, const G&>
    constexpr Expected(const Unexpected<G>& error) noexcept(std::is_nothrow_constructible_v<E, const G&>)
        : m_error(error.error())
        , m_hasValue(false)
    {
    }

    /// Holds the error of @p error.
    template <class G>
        requires std::is_constructible_v<E, G&&>
    constexpr Expected(Unexpected<G>&& error) noexcept(std::is_nothrow_constructible_v<E, G&&>)
        : m_error(std::move(error).error())
        , m_hasValue(false)
    {
    }

    /// Copies success or the error of @p other.
    constexpr Expected(const Expected& other)
        requires std::is_copy_constructible_v<E>
        : m_hasValue(other.m_hasValue)
    {
        if (!m_hasValue)
        {
            std::construct_at(std::addressof(m_error), other.m_error);
        }
    }

    /// Moves success or the error of @p other.
    constexpr Expected(Expected&& other) noexcept(std::is_nothrow_move_constructible_v<E>)
        requires std::is_move_constructible_v<E>
        : m_hasValue(other.m_hasValue)
    {
        if (!m_hasValue)
        {
            std::construct_at(std::addressof(m_error), std::move(other.m_error));
        }
    }

    /// Replaces the contents with a copy of @p other.
    constexpr Expected& operator=(const Expected& other)
        requires(std::is_copy_constructible_v<E> && std::is_nothrow_move_constructible_v<E> &&
                 std::is_nothrow_move_assignable_v<E>)
    {
        if (this != &other)
        {
            Expected copy(other);
            *this = std::move(copy);
        }
        return *this;
    }

    /// Replaces the contents with those of @p other.
    constexpr Expected& operator=(Expected&& other) noexcept
        requires(std::is_nothrow_move_constructible_v<E> && std::is_nothrow_move_assignable_v<E>)
    {
        if (this == &other)
        {
            return *this;
        }
        if (!m_hasValue && !other.m_hasValue)
        {
            m_error = std::move(other.m_error);
        }
        else if (m_hasValue != other.m_hasValue)
        {
            if (m_hasValue)
            {
                std::construct_at(std::addressof(m_error), std::move(other.m_error));
            }
            else
            {
                std::destroy_at(std::addressof(m_error));
            }
            m_hasValue = other.m_hasValue;
        }
        return *this;
    }

    /// Destroys the error, if any.
    constexpr ~Expected()
    {
        if (!m_hasValue)
        {
            std::destroy_at(std::addressof(m_error));
        }
    }

    /// True on success.
    [[nodiscard]] constexpr bool hasValue() const noexcept { return m_hasValue; }

    /// True on success.
    constexpr explicit operator bool() const noexcept { return m_hasValue; }

    /// Asserts success (there is no value to return).
    constexpr void value() const { ILL_ASSERT(m_hasValue, "Expected::value() on an error"); }

    /// The error. Asserts that an error is held.
    [[nodiscard]] constexpr E& error() &
    {
        ILL_ASSERT(!m_hasValue, "Expected::error() on success");
        return m_error;
    }

    /// @copydoc error()
    [[nodiscard]] constexpr const E& error() const&
    {
        ILL_ASSERT(!m_hasValue, "Expected::error() on success");
        return m_error;
    }

    /// @copydoc error()
    [[nodiscard]] constexpr E&& error() &&
    {
        ILL_ASSERT(!m_hasValue, "Expected::error() on success");
        return std::move(m_error);
    }

private:
    union
    {
        E m_error;
    };
    bool m_hasValue;
};

} // namespace illusion
