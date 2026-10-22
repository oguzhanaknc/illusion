#include "ReflectionData.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace illusion
{

struct StructRegistry::Impl
{
    mutable std::mutex mutex;
    std::vector<detail::ReflectionData*> current; // indexed by StructId
    std::unordered_map<u64, StructId> byHash;
    std::unordered_map<std::string, StructId> byStableName;
    // Every reflection ever made, including replaced ones, so that views never dangle.
    std::vector<std::unique_ptr<detail::ReflectionData>> reflections;

    detail::ReflectionData& data(StructId id) const
    {
        ILL_ASSERT(id < current.size(), "unknown struct id {}", id);
        return *current[id];
    }

    const TypeReflection* reflectionOf(u64 typeHash) const
    {
        const auto it = byHash.find(typeHash);
        return it != byHash.end() ? &current[it->second]->view() : nullptr;
    }
};

StructRegistry::StructRegistry()
    : m_impl(std::make_unique<Impl>())
{
}

StructRegistry::~StructRegistry() = default;

StructId StructRegistry::find(u64 typeHash) const
{
    std::scoped_lock lock(m_impl->mutex);
    const auto it = m_impl->byHash.find(typeHash);
    return it != m_impl->byHash.end() ? it->second : kInvalidStructId;
}

StructId StructRegistry::findByStableName(std::string_view stableName) const
{
    std::scoped_lock lock(m_impl->mutex);
    const auto it = m_impl->byStableName.find(std::string(stableName));
    return it != m_impl->byStableName.end() ? it->second : kInvalidStructId;
}

const StructReflection* StructRegistry::reflection(StructId id) const
{
    std::scoped_lock lock(m_impl->mutex);
    return id < m_impl->current.size() ? &m_impl->current[id]->view() : nullptr;
}

u32 StructRegistry::count() const
{
    std::scoped_lock lock(m_impl->mutex);
    return static_cast<u32>(m_impl->current.size());
}

const StructReflection* StructRegistry::reflectionOf(u64 typeHash) const
{
    std::scoped_lock lock(m_impl->mutex);
    return m_impl->reflectionOf(typeHash);
}

StructId StructRegistry::beginReflection(const TypeLayout& layout, std::string_view stableName, u32 version)
{
    ILL_ASSERT(detail::isValidStableName(stableName),
               "invalid stable name '{}' (use letters, digits, '_' and '.')", stableName);
    std::scoped_lock lock(m_impl->mutex);
    Impl& impl = *m_impl;
    const auto existing = impl.byStableName.find(std::string(stableName));
    const auto known = impl.byHash.find(layout.typeHash);
    ILL_CHECK(existing == impl.byStableName.end() ||
                  (known != impl.byHash.end() && existing->second == known->second),
              "stable name '{}' is already used by another struct", stableName);

    StructId id = kInvalidStructId;
    if (known != impl.byHash.end())
    {
        id = known->second;
        ILL_CHECK(impl.current[id]->view().size == layout.size,
                  "struct '{}' re-reflected with a different size", stableName);
        impl.byStableName.erase(std::string(impl.current[id]->view().name));
    }
    else
    {
        ILL_CHECK(impl.current.size() < kInvalidStructId, "too many reflected structs");
        id = static_cast<StructId>(impl.current.size());
        impl.current.push_back(nullptr);
        impl.byHash.emplace(layout.typeHash, id);
    }
    impl.current[id] =
        impl.reflections.emplace_back(std::make_unique<detail::ReflectionData>(stableName, version, layout))
            .get();
    impl.byStableName.emplace(impl.current[id]->view().name, id);
    return id;
}

void StructRegistry::addReflectedField(StructId id, const FieldInfo& field)
{
    std::scoped_lock lock(m_impl->mutex);
    const auto resolve = [](const void* context, u64 typeHash) {
        return static_cast<const Impl*>(context)->reflectionOf(typeHash);
    };
    m_impl->data(id).addField(field, resolve, m_impl.get());
}

void StructRegistry::setReflectionMeta(StructId id, const TypeMeta& meta)
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->data(id).setMeta(meta);
}

void StructRegistry::addReflectionMigration(StructId id, const Migration& migration)
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->data(id).addMigration(migration);
}

} // namespace illusion
