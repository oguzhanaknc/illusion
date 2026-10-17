#include <illusion/core/Assert.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <deque>
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
};

// Owns the strings and arrays that the public ComponentReflection view points into. Deques keep element
// addresses stable while fields are appended.
struct ComponentRegistry::Reflection
{
    std::string name;
    std::deque<std::string> strings;
    std::deque<std::vector<EnumValue>> enums;
    std::vector<FieldInfo> fields;
    ComponentReflection view;
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

namespace
{

bool isValidStableName(std::string_view name) noexcept
{
    if (name.empty())
    {
        return false;
    }
    for (const char c : name)
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '_' || c == '.';
        if (!valid)
        {
            return false;
        }
    }
    return true;
}

bool isValidFieldName(std::string_view name) noexcept
{
    if (name.empty() || (name[0] >= '0' && name[0] <= '9'))
    {
        return false;
    }
    for (const char c : name)
    {
        const bool valid =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!valid)
        {
            return false;
        }
    }
    return true;
}

} // namespace

void ComponentRegistry::beginReflection(ComponentId id, std::string_view stableName, u32 version)
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    ILL_ASSERT(isValidStableName(stableName), "invalid stable name '{}' (use letters, digits, '_' and '.')",
               stableName);
    std::scoped_lock lock(m_lookup->mutex);
    Entry& entry = (*m_entries)[id];
    const auto existing = m_lookup->byStableName.find(std::string(stableName));
    ILL_CHECK(existing == m_lookup->byStableName.end() || existing->second == id,
              "stable name '{}' is already used by component '{}'", stableName,
              existing == m_lookup->byStableName.end() ? std::string_view()
                                                       : (*m_entries)[existing->second].info.name);
    if (entry.reflection)
    {
        m_lookup->byStableName.erase(entry.reflection->name);
    }
    entry.reflection = std::make_unique<Reflection>();
    entry.reflection->name.assign(stableName);
    entry.reflection->view.name = entry.reflection->name;
    entry.reflection->view.version = version;
    m_lookup->byStableName.emplace(entry.reflection->name, id);
}

void ComponentRegistry::addReflectedField(ComponentId id, const FieldInfo& field)
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    std::scoped_lock lock(m_lookup->mutex);
    Entry& entry = (*m_entries)[id];
    ILL_ASSERT(entry.reflection != nullptr, "addReflectedField() before beginReflection()");
    Reflection& reflection = *entry.reflection;
    ILL_ASSERT(isValidFieldName(field.name), "invalid field name '{}' in '{}'", field.name, reflection.name);
    ILL_ASSERT(reflection.view.field(field.name) == nullptr, "field '{}' reflected twice in '{}'", field.name,
               reflection.name);
    ILL_ASSERT(field.offset < entry.info.size, "field '{}' lies outside component '{}'", field.name,
               reflection.name);

    FieldInfo copy = field;
    copy.name = reflection.strings.emplace_back(field.name);
    if (field.type == FieldType::Enum)
    {
        std::vector<EnumValue>& values = reflection.enums.emplace_back();
        for (const EnumValue& value : field.enumValues)
        {
            values.push_back(EnumValue{reflection.strings.emplace_back(value.name), value.value});
        }
        copy.enumValues = values;
    }
    reflection.fields.push_back(copy);
    reflection.view.fields = reflection.fields;
}

const ComponentReflection* ComponentRegistry::reflection(ComponentId id) const noexcept
{
    ILL_ASSERT(id < count(), "unknown component id {}", id);
    const Entry& entry = (*m_entries)[id];
    return entry.reflection ? &entry.reflection->view : nullptr;
}

ComponentId ComponentRegistry::findByStableName(std::string_view stableName) const
{
    std::scoped_lock lock(m_lookup->mutex);
    const auto it = m_lookup->byStableName.find(std::string(stableName));
    return it != m_lookup->byStableName.end() ? it->second : kInvalidComponentId;
}

} // namespace illusion
