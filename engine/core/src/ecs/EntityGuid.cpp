#include <illusion/core/Assert.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace illusion
{
namespace
{

constexpr u64 kGolden = 0x9e3779b97f4a7c15ull;

// The splitmix64 finalizer: a bijection of 64-bit numbers that maps only 0 to 0.
constexpr u64 mix(u64 z) noexcept
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

constexpr char kHexDigits[] = "0123456789abcdef";

} // namespace

std::array<char, kGuidTextLength> formatGuid(EntityGuid guid) noexcept
{
    std::array<char, kGuidTextLength> text{};
    for (usize i = 0; i < kGuidTextLength; ++i)
    {
        const u64 shift = (kGuidTextLength - 1 - i) * 4;
        text[i] = kHexDigits[(guid.value >> shift) & 0xF];
    }
    return text;
}

std::string toString(EntityGuid guid)
{
    const std::array<char, kGuidTextLength> text = formatGuid(guid);
    return std::string(text.data(), text.size());
}

std::optional<EntityGuid> parseGuid(std::string_view text) noexcept
{
    if (text.size() != kGuidTextLength)
    {
        return std::nullopt;
    }
    u64 value = 0;
    for (const char c : text)
    {
        u64 digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = static_cast<u64>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = static_cast<u64>(c - 'a') + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = static_cast<u64>(c - 'A') + 10;
        }
        else
        {
            return std::nullopt;
        }
        value = (value << 4) | digit;
    }
    if (value == 0)
    {
        return std::nullopt;
    }
    return EntityGuid{value};
}

GuidGenerator::GuidGenerator()
{
    std::array<std::byte, sizeof(u64)> bytes{};
    ILL_CHECK(secureRandomBytes(bytes), "the operating system's secure random source failed");
    std::memcpy(&m_key, bytes.data(), sizeof(m_key));
}

EntityGuid GuidGenerator::next() noexcept
{
    // An odd multiplier makes counter -> counter * kGolden a bijection too, and spreads consecutive counters.
    for (;;)
    {
        const u64 counter = m_counter.fetch_add(1, std::memory_order_relaxed);
        const u64 value = mix(m_key + counter * kGolden);
        if (value != 0)
        {
            return EntityGuid{value};
        }
    }
}

GuidGenerator& GuidGenerator::shared()
{
    static GuidGenerator generator;
    return generator;
}

u32 assignMissingGuids(World& world, GuidGenerator& generator)
{
    ILL_PROFILE_SCOPE();
    ComponentMask none;
    none.set(componentId<EntityGuid>());
    std::vector<Entity> missing;
    world.forEachMatching({}, none, [&missing](const RawChunk& chunk) {
        const std::span<const Entity> entities = chunk.entities();
        missing.insert(missing.end(), entities.begin(), entities.end());
    });
    // Archetype order depends on the order components were added: assign in entity order instead, so a
    // seeded generator gives the same ids for the same World.
    std::sort(missing.begin(), missing.end(), [](Entity a, Entity b) { return a.index < b.index; });
    for (const Entity entity : missing)
    {
        world.emplace<EntityGuid>(entity, generator.next());
    }
    return static_cast<u32>(missing.size());
}

GuidEntityResolver::GuidEntityResolver(const World& world)
    : m_world(&world)
{
    refresh();
}

void GuidEntityResolver::refresh()
{
    ILL_PROFILE_SCOPE();
    m_entities.clear();
    ComponentMask all;
    const ComponentId guidId = componentId<EntityGuid>();
    all.set(guidId);
    m_world->forEachMatching(all, {}, [this, guidId](const RawChunk& chunk) {
        const auto* guids = static_cast<const EntityGuid*>(chunk.column(guidId));
        const std::span<const Entity> entities = chunk.entities();
        for (usize i = 0; i < entities.size(); ++i)
        {
            m_entities.emplace(guids[i].value, entities[i]);
        }
    });
}

Entity GuidEntityResolver::find(EntityGuid guid) const noexcept
{
    const auto it = m_entities.find(guid.value);
    return it != m_entities.end() && m_world->isAlive(it->second) ? it->second : kNullEntity;
}

EntityGuid GuidEntityResolver::guidOf(Entity entity) const noexcept
{
    if (!m_world->isAlive(entity))
    {
        return EntityGuid{};
    }
    const EntityGuid* guid = m_world->tryGet<EntityGuid>(entity);
    return guid != nullptr ? *guid : EntityGuid{};
}

JsonMutValue GuidEntityResolver::writeEntity(JsonMutDocument& doc, Entity entity) const
{
    const EntityGuid guid = guidOf(entity);
    if (guid.isNull())
    {
        return doc.makeNull();
    }
    const std::array<char, kGuidTextLength> text = formatGuid(guid);
    return doc.makeString(std::string_view(text.data(), text.size()));
}

Expected<Entity, std::string> GuidEntityResolver::readEntity(JsonValue value) const
{
    if (value.isNull())
    {
        return kNullEntity;
    }
    const std::optional<std::string_view> text = value.asString();
    const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
    const Entity entity = guid ? find(*guid) : kNullEntity;
    if (entity.isNull())
    {
        return unexpected(std::string("must be null or the id of an entity in this world"));
    }
    return entity;
}

} // namespace illusion
