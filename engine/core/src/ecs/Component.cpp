#include <illusion/core/Assert.hpp>
#include <illusion/ecs/Component.hpp>

#include <mutex>
#include <unordered_map>

namespace illusion
{

struct ComponentRegistry::Lookup
{
    mutable std::mutex mutex;
    std::unordered_map<u64, ComponentId> byHash;
};

namespace
{

std::atomic<ComponentRegistry*> g_registry{nullptr};

ComponentRegistry& defaultRegistry()
{
    static ComponentRegistry registry;
    return registry;
}

} // namespace

ComponentRegistry::ComponentRegistry()
    : m_lookup(std::make_unique<Lookup>())
    , m_entries(std::make_unique<std::array<Entry, kMaxComponentTypes>>())
{
}

ComponentRegistry::~ComponentRegistry() = default;

ComponentRegistry& ComponentRegistry::instance() noexcept
{
    ComponentRegistry* registry = g_registry.load(std::memory_order_acquire);
    return registry != nullptr ? *registry : defaultRegistry();
}

void ComponentRegistry::setInstance(ComponentRegistry* registry) noexcept
{
    g_registry.store(registry, std::memory_order_release);
}

ComponentId ComponentRegistry::registerType(const ComponentInfo& info)
{
    ILL_CHECK(info.relocate != nullptr && info.destroy != nullptr, "component '{}' has no lifetime functions",
              info.name);
    ILL_CHECK(isPowerOfTwo(info.alignment), "component '{}' has invalid alignment {}", info.name,
              info.alignment);

    std::scoped_lock lock(m_lookup->mutex);
    if (const auto it = m_lookup->byHash.find(info.hash); it != m_lookup->byHash.end())
    {
        const ComponentInfo& existing = (*m_entries)[it->second].info;
        ILL_CHECK(existing.name == info.name, "component hash collision between '{}' and '{}'", existing.name,
                  info.name);
        ILL_CHECK(existing.size == info.size && existing.alignment == info.alignment,
                  "component '{}' re-registered with a different layout", info.name);
        return it->second;
    }

    const u32 index = m_count.load(std::memory_order_relaxed);
    ILL_CHECK(index < kMaxComponentTypes, "too many component types (max {})", kMaxComponentTypes);

    Entry& entry = (*m_entries)[index];
    entry.name.assign(info.name);
    entry.info = info;
    entry.info.name = entry.name;

    const auto id = static_cast<ComponentId>(index);
    m_lookup->byHash.emplace(info.hash, id);
    // Publish the entry: readers that observe the new count also observe the entry contents.
    m_count.store(index + 1, std::memory_order_release);
    return id;
}

ComponentId ComponentRegistry::find(u64 hash) const
{
    std::scoped_lock lock(m_lookup->mutex);
    const auto it = m_lookup->byHash.find(hash);
    return it != m_lookup->byHash.end() ? it->second : kInvalidComponentId;
}

const ComponentInfo& ComponentRegistry::info(ComponentId id) const noexcept
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    return (*m_entries)[id].info;
}

} // namespace illusion
