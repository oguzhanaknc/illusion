#pragma once

/// @file
/// Persistent entity ids: the EntityGuid Component, id generation and references by id.
///
/// An Entity handle is valid only inside the World and process that created it. An EntityGuid is a 64-bit
/// id that stays with the Entity when it is saved and loaded again, so scene files, the editor's undo
/// history, its commands and prefabs can refer to entities by id
/// (docs/adr/0033-entity-ids-names-scene-format-v2.md).
///
/// @code
/// illusion::World world;
/// const illusion::Entity player = world.create(illusion::Transform{});
/// illusion::assignMissingGuids(world); // player now has an EntityGuid
///
/// const illusion::EntityGuid id = world.get<illusion::EntityGuid>(player);
/// const std::string text = illusion::toString(id); // "3f9a0c1e5b7d2a40"
///
/// const illusion::GuidEntityResolver ids(world);
/// const illusion::Entity found = ids.find(*illusion::parseGuid(text)); // == player
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Entity.hpp>

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace illusion
{

class World;

/// Persistent id of an Entity, unique within a project (reflected as "illusion.EntityGuid", read-only).
/// The value 0 means "no id" and is never generated. Scene files write it as the entity's "id".
struct EntityGuid
{
    u64 value = 0; ///< The id; 0 for none.

    /// True if this is not an id (value 0).
    [[nodiscard]] constexpr bool isNull() const noexcept { return value == 0; }

    /// Ids are equal when their values are.
    friend constexpr bool operator==(const EntityGuid&, const EntityGuid&) noexcept = default;
};

/// Number of characters in the text form of an EntityGuid.
inline constexpr usize kGuidTextLength = 16;

/// Writes @p guid as 16 lower-case hexadecimal digits, the form used in scene files and by the editor.
[[nodiscard]] std::array<char, kGuidTextLength> formatGuid(EntityGuid guid) noexcept;

/// Returns @p guid as 16 lower-case hexadecimal digits (see formatGuid()).
[[nodiscard]] std::string toString(EntityGuid guid);

/// Parses the text form of an id: exactly 16 hexadecimal digits (either case), not all zero. Returns
/// nullopt for anything else.
[[nodiscard]] std::optional<EntityGuid> parseGuid(std::string_view text) noexcept;

/// Generates entity ids. Every id of one generator is different until 2^64 ids have been made: a counter is
/// mapped through a bijection of 64-bit numbers (the splitmix64 finalizer) offset by a key. Ids of different
/// generators collide only by chance. Thread-safe: next() is a single atomic addition.
class GuidGenerator : NonCopyable
{
public:
    /// Creates a generator whose key comes from the operating system's secure random source
    /// (secureRandomBytes()), so that ids made in different sessions and on different machines do not
    /// collide. Failing to read that source is a fatal error.
    GuidGenerator();

    /// Creates a deterministic generator: the same @p seed gives the same ids on every platform. For tests
    /// and reproducible tools; ids of two generators with the same seed collide.
    explicit GuidGenerator(u64 seed) noexcept
        : m_key(seed)
    {
    }

    /// Returns a new id (never the null id).
    [[nodiscard]] EntityGuid next() noexcept;

    /// The process-wide generator with a secure key, used when no generator is given.
    [[nodiscard]] static GuidGenerator& shared();

private:
    u64 m_key = 0;
    std::atomic<u64> m_counter{0};
};

/// Adds an EntityGuid made by @p generator to every live entity of @p world that has none, and returns how
/// many entities got one. A structural change: not allowed while a query iterates @p world.
u32 assignMissingGuids(World& world, GuidGenerator& generator = GuidGenerator::shared());

/// Converts Entity fields to and from ids for the entities of one World: the EntityResolver of the editor's
/// commands and tools. A reference is written as the target's id, or `null` if the target is null, dead or
/// has no EntityGuid; reading accepts `null` and the id of an entity of the World.
///
/// The resolver indexes the World's ids when it is created: call refresh() after structural changes. If two
/// entities share an id, find() returns one of them.
class GuidEntityResolver final : public EntityResolver
{
public:
    /// Indexes the ids of @p world, which must outlive the resolver.
    explicit GuidEntityResolver(const World& world);

    /// Indexes the World's ids again.
    void refresh();

    /// Returns the entity with @p guid, or the null entity if there is none.
    [[nodiscard]] Entity find(EntityGuid guid) const noexcept;

    /// Returns the id of @p entity, or the null id if it is dead or has none.
    [[nodiscard]] EntityGuid guidOf(Entity entity) const noexcept;

    /// Writes the id of @p entity as a string, or `null`.
    [[nodiscard]] JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override;

    /// Reads `null` or the id of an entity of the World.
    [[nodiscard]] Expected<Entity, std::string> readEntity(JsonValue value) const override;

private:
    const World* m_world;
    std::unordered_map<u64, Entity> m_entities;
};

} // namespace illusion
