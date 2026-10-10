#pragma once

/// @file
/// Entity handle.

#include <illusion/core/Base.hpp>

#include <functional>

namespace illusion
{

/// Handle to an entity in a World: a slot index plus a generation that is bumped every time the slot
/// is reused, so stale handles are detected (World::isAlive() returns false for them).
struct Entity
{
    /// Index value of the null entity.
    static constexpr u32 kInvalidIndex = 0xFFFFFFFFu;

    u32 index = kInvalidIndex; ///< Slot in the world's entity table.
    u32 generation = 0;        ///< Incremented when the slot is recycled.

    /// Returns true for the null entity (default-constructed handle).
    [[nodiscard]] constexpr bool isNull() const noexcept { return index == kInvalidIndex; }

    /// Packs the handle into 64 bits (generation in the high half).
    [[nodiscard]] constexpr u64 toBits() const noexcept
    {
        return (static_cast<u64>(generation) << 32) | index;
    }

    /// Unpacks a handle produced by toBits().
    [[nodiscard]] static constexpr Entity fromBits(u64 bits) noexcept
    {
        return Entity{static_cast<u32>(bits & 0xFFFFFFFFu), static_cast<u32>(bits >> 32)};
    }

    /// Handles are equal when both index and generation match.
    friend constexpr bool operator==(const Entity&, const Entity&) noexcept = default;
};

/// The null entity.
inline constexpr Entity kNullEntity{};

} // namespace illusion

/// Hash support so Entity can be used as a key in unordered containers.
template <>
struct std::hash<illusion::Entity>
{
    /// Hashes the packed 64-bit handle.
    [[nodiscard]] std::size_t operator()(const illusion::Entity& entity) const noexcept
    {
        return std::hash<illusion::u64>{}(entity.toBits());
    }
};
