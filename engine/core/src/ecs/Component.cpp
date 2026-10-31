#include "ReflectionData.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace illusion
{

struct ComponentRegistry::Lookup
{
    mutable std::mutex mutex;
    std::unordered_map<u64, ComponentId> byHash;
    std::unordered_map<std::string, ComponentId> byStableName;
    std::vector<u32> retiredModules; // their types are taken over by the next registration
    // Registrations of retired or orphaned types, waiting for commitStaged().
    std::vector<std::pair<ComponentId, ComponentInfo>> staged;
    // Every reflection ever made, including replaced ones, so that views never dangle.
    std::vector<std::unique_ptr<detail::ReflectionData>> reflections;
};

namespace
{

std::atomic<ComponentRegistry*> g_registry{nullptr};

// Owner of orphaned types: always retired, never a real module id.
constexpr u32 kOrphaned = 0xFFFFFFFFu;

[[noreturn]] void orphanedLifetime() noexcept
{
    ILL_CHECK(false, "a component type of an unloaded module was used; its module must register it again");
    ILL_UNREACHABLE();
}

void orphanedRelocate(void*, void*) noexcept
{
    orphanedLifetime();
}

void orphanedDestroy(void*) noexcept
{
    orphanedLifetime();
}

ComponentRegistry& defaultRegistry()
{
    static ComponentRegistry registry;
    return registry;
}

} // namespace

ComponentRegistry::ComponentRegistry()
    : m_lookup(std::make_unique<Lookup>())
    , m_structs(std::make_unique<StructRegistry>())
    , m_entries(std::make_unique<std::array<Entry, kMaxComponentTypes>>())
    , m_archetypeUses(std::make_unique<std::array<std::atomic<u32>, kMaxComponentTypes>>())
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
        ComponentInfo& entry = (*m_entries)[it->second].info;
        ILL_CHECK(entry.name == info.name, "component hash collision between '{}' and '{}'", entry.name,
                  info.name);
        // A reloaded module's (retired) or an unloaded module's (orphaned) type goes to the module that
        // registers it next, through a staged registration that the module host commits once it has
        // migrated the existing Components (ADR 0044). The host's code is never unloaded, so it takes any
        // type over from a module at once.
        const auto& retired = m_lookup->retiredModules;
        const bool replaceable = entry.module == kOrphaned ||
                                 std::find(retired.begin(), retired.end(), entry.module) != retired.end();
        const bool hostTakesOver = info.module == 0 && entry.module != 0;
        const bool sameLayout = entry.size == info.size && entry.alignment == info.alignment;
        // An orphaned type has no Components left, so only a new layout must wait for its archetypes to go.
        if (replaceable && !hostTakesOver && entry.module != info.module &&
            (entry.module != kOrphaned || !sameLayout))
        {
            auto& staged = m_lookup->staged;
            const auto existing =
                std::find_if(staged.begin(), staged.end(),
                             [id = it->second](const auto& item) { return item.first == id; });
            if (existing != staged.end())
            {
                existing->second = info;
            }
            else
            {
                staged.emplace_back(it->second, info);
            }
            return it->second;
        }
        ILL_CHECK(sameLayout, "component '{}' re-registered with a different layout", info.name);
        if (hostTakesOver || (replaceable && entry.module != info.module))
        {
            entry.relocate = info.relocate;
            entry.destroy = info.destroy;
            entry.construct = info.construct;
            entry.trivial = info.trivial;
            entry.module = info.module;
        }
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

void ComponentRegistry::retireModule(u32 module)
{
    ILL_ASSERT(module != 0, "the host cannot be retired");
    std::scoped_lock lock(m_lookup->mutex);
    if (std::find(m_lookup->retiredModules.begin(), m_lookup->retiredModules.end(), module) ==
        m_lookup->retiredModules.end())
    {
        m_lookup->retiredModules.push_back(module);
    }
}

void ComponentRegistry::restoreModule(u32 module)
{
    std::scoped_lock lock(m_lookup->mutex);
    std::erase(m_lookup->retiredModules, module);
}

void ComponentRegistry::stagedTypes(std::vector<ComponentId>& out) const
{
    std::scoped_lock lock(m_lookup->mutex);
    for (const auto& [id, info] : m_lookup->staged)
    {
        out.push_back(id);
    }
}

std::optional<ComponentInfo> ComponentRegistry::stagedInfo(ComponentId id) const
{
    std::scoped_lock lock(m_lookup->mutex);
    for (const auto& [stagedId, info] : m_lookup->staged)
    {
        if (stagedId == id)
        {
            return info;
        }
    }
    return std::nullopt;
}

const TypeReflection* ComponentRegistry::stagedReflection(ComponentId id) const
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    std::scoped_lock lock(m_lookup->mutex);
    const Entry& entry = (*m_entries)[id];
    return entry.building != nullptr && entry.building != entry.reflection ? &entry.building->view()
                                                                           : nullptr;
}

void ComponentRegistry::commitStaged(ComponentId id)
{
    std::scoped_lock lock(m_lookup->mutex);
    auto& staged = m_lookup->staged;
    const auto it =
        std::find_if(staged.begin(), staged.end(), [id](const auto& item) { return item.first == id; });
    ILL_ASSERT(it != staged.end(), "component {} has no staged registration", id);
    Entry& entry = (*m_entries)[id];
    const ComponentInfo& next = it->second;
    if (entry.info.size != next.size || entry.info.alignment != next.alignment)
    {
        ILL_CHECK((*m_archetypeUses)[id].load(std::memory_order_acquire) == 0,
                  "the layout of component '{}' cannot change while archetypes use it", entry.name);
        entry.info.size = next.size;
        entry.info.alignment = next.alignment;
    }
    entry.info.relocate = next.relocate;
    entry.info.destroy = next.destroy;
    entry.info.construct = next.construct;
    entry.info.trivial = next.trivial;
    entry.info.module = next.module;
    staged.erase(it);
    if (entry.building != nullptr && entry.building != entry.reflection)
    {
        publishReflection(id);
    }
}

void ComponentRegistry::discardStaged()
{
    std::scoped_lock lock(m_lookup->mutex);
    for (const auto& [id, info] : m_lookup->staged)
    {
        Entry& entry = (*m_entries)[id];
        entry.building = entry.reflection;
    }
    m_lookup->staged.clear();
}

u32 ComponentRegistry::archetypeUses(ComponentId id) const noexcept
{
    ILL_ASSERT(id < kMaxComponentTypes, "unknown component id {}", id);
    return (*m_archetypeUses)[id].load(std::memory_order_acquire);
}

void ComponentRegistry::addArchetypeUse(ComponentId id, i32 delta) const noexcept
{
    ILL_ASSERT(id < kMaxComponentTypes, "unknown component id {}", id);
    (*m_archetypeUses)[id].fetch_add(static_cast<u32>(delta), std::memory_order_acq_rel);
}

void ComponentRegistry::typesOfModule(u32 module, std::vector<ComponentId>& out) const
{
    std::scoped_lock lock(m_lookup->mutex);
    const u32 total = count();
    for (u32 id = 0; id < total; ++id)
    {
        if ((*m_entries)[id].info.module == module)
        {
            out.push_back(static_cast<ComponentId>(id));
        }
    }
}

void ComponentRegistry::orphanModule(u32 module)
{
    ILL_ASSERT(module != 0, "the host cannot be orphaned");
    std::scoped_lock lock(m_lookup->mutex);
    const u32 total = count();
    for (u32 id = 0; id < total; ++id)
    {
        ComponentInfo& info = (*m_entries)[id].info;
        if (info.module == module)
        {
            info.relocate = &orphanedRelocate;
            info.destroy = &orphanedDestroy;
            info.construct = nullptr;
            info.module = kOrphaned;
        }
    }
    std::erase(m_lookup->retiredModules, module);
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

const ComponentInfo* ComponentRegistry::tryInfo(ComponentId id) const noexcept
{
    return contains(id) ? &(*m_entries)[id].info : nullptr;
}

void ComponentRegistry::beginReflection(ComponentId id, std::string_view stableName, u32 version,
                                        const TypeLayout& layout)
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    ILL_ASSERT(detail::isValidStableName(stableName),
               "invalid stable name '{}' (use letters, digits, '_' and '.')", stableName);
    std::scoped_lock lock(m_lookup->mutex);
    Entry& entry = (*m_entries)[id];
    auto data = std::make_unique<detail::ReflectionData>(stableName, version, layout);
    entry.building = m_lookup->reflections.emplace_back(std::move(data)).get();
    const auto& staged = m_lookup->staged;
    if (std::find_if(staged.begin(), staged.end(), [id](const auto& item) { return item.first == id; }) !=
        staged.end())
    {
        return; // published by commitStaged()
    }
    publishReflection(id);
}

void ComponentRegistry::publishReflection(ComponentId id)
{
    Entry& entry = (*m_entries)[id];
    const std::string_view stableName = entry.building->view().name;
    const auto existing = m_lookup->byStableName.find(std::string(stableName));
    ILL_CHECK(existing == m_lookup->byStableName.end() || existing->second == id,
              "stable name '{}' is already used by component '{}'", stableName,
              existing == m_lookup->byStableName.end() ? std::string_view()
                                                       : (*m_entries)[existing->second].info.name);
    if (entry.reflection != nullptr)
    {
        m_lookup->byStableName.erase(std::string(entry.reflection->view().name));
    }
    entry.reflection = entry.building;
    m_lookup->byStableName.emplace(entry.reflection->view().name, id);
}

detail::ReflectionData& ComponentRegistry::reflectionData(ComponentId id) const
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    detail::ReflectionData* reflection = (*m_entries)[id].building;
    ILL_ASSERT(reflection != nullptr, "component {} is not being reflected; call beginReflection() first",
               id);
    return *reflection;
}

void ComponentRegistry::addReflectedField(ComponentId id, const FieldInfo& field)
{
    std::scoped_lock lock(m_lookup->mutex);
    const auto resolve = [](const void* context, u64 typeHash) {
        return static_cast<const StructRegistry*>(context)->reflectionOf(typeHash);
    };
    reflectionData(id).addField(field, resolve, m_structs.get());
}

void ComponentRegistry::setReflectionMeta(ComponentId id, const TypeMeta& meta)
{
    std::scoped_lock lock(m_lookup->mutex);
    reflectionData(id).setMeta(meta);
}

void ComponentRegistry::addReflectionMigration(ComponentId id, const Migration& migration)
{
    std::scoped_lock lock(m_lookup->mutex);
    reflectionData(id).addMigration(migration);
}

const TypeReflection* ComponentRegistry::reflection(ComponentId id) const noexcept
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    const detail::ReflectionData* reflection = (*m_entries)[id].reflection;
    return reflection != nullptr ? &reflection->view() : nullptr;
}

ComponentId ComponentRegistry::findByStableName(std::string_view stableName) const
{
    std::scoped_lock lock(m_lookup->mutex);
    const auto it = m_lookup->byStableName.find(std::string(stableName));
    return it != m_lookup->byStableName.end() ? it->second : kInvalidComponentId;
}

} // namespace illusion
