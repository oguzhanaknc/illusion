#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace illusion
{
namespace
{

std::atomic<AssetPathTable*> g_table{nullptr};

AssetPathTable& defaultTable()
{
    static AssetPathTable table;
    return table;
}

} // namespace

struct AssetPathTable::Impl
{
    mutable std::shared_mutex mutex;
    std::deque<std::string> paths;                  // stable storage: elements never move
    std::unordered_map<u64, std::string_view> byId; // views into paths
};

AssetPathTable::AssetPathTable()
    : m_impl(std::make_unique<Impl>())
{
}

AssetPathTable::~AssetPathTable() = default;

AssetPathTable& AssetPathTable::instance() noexcept
{
    AssetPathTable* table = g_table.load(std::memory_order_acquire);
    return table != nullptr ? *table : defaultTable();
}

void AssetPathTable::setInstance(AssetPathTable* table) noexcept
{
    g_table.store(table, std::memory_order_release);
}

AssetId AssetPathTable::intern(std::string_view path)
{
    // Fast path: a recorded path is valid and normalized, so it needs no checks.
    {
        const u64 hash = fnv1a64(path);
        std::shared_lock lock(m_impl->mutex);
        if (const auto found = m_impl->byId.find(hash); found != m_impl->byId.end() && found->second == path)
        {
            return AssetId{hash};
        }
    }
    std::optional<std::string> normalized;
    if (!isNormalizedAssetPath(path))
    {
        normalized = normalizeAssetPath(path);
        if (!normalized)
        {
            return {};
        }
        path = *normalized;
    }
    const AssetId id{fnv1a64(path)};
    {
        std::shared_lock lock(m_impl->mutex);
        if (const auto found = m_impl->byId.find(id.value); found != m_impl->byId.end())
        {
            if (found->second == path)
            {
                return id;
            }
            ILL_LOG_ERROR("Assets", "asset paths '{}' and '{}' have the same id {:#x}; rename one of them",
                          found->second, path, id.value);
            return {};
        }
    }
    std::unique_lock lock(m_impl->mutex);
    const auto [entry, inserted] = m_impl->byId.try_emplace(id.value);
    if (inserted)
    {
        entry->second = m_impl->paths.emplace_back(path);
    }
    else if (entry->second != path) // another thread recorded a colliding path meanwhile
    {
        ILL_LOG_ERROR("Assets", "asset paths '{}' and '{}' have the same id {:#x}; rename one of them",
                      entry->second, path, id.value);
        return {};
    }
    return id;
}

std::string_view AssetPathTable::path(AssetId id) const noexcept
{
    if (!id)
    {
        return {};
    }
    std::shared_lock lock(m_impl->mutex);
    const auto found = m_impl->byId.find(id.value);
    return found != m_impl->byId.end() ? found->second : std::string_view{};
}

usize AssetPathTable::size() const noexcept
{
    std::shared_lock lock(m_impl->mutex);
    return m_impl->byId.size();
}

std::string_view assetKindName(AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetKind::Any:
        return "Any";
    case AssetKind::Mesh:
        return "Mesh";
    case AssetKind::Material:
        return "Material";
    case AssetKind::Texture:
        return "Texture";
    case AssetKind::Environment:
        return "Environment";
    case AssetKind::AudioClip:
        return "AudioClip";
    case AssetKind::Model:
        return "Model";
    case AssetKind::Scene:
        return "Scene";
    }
    return "Unknown";
}

} // namespace illusion
