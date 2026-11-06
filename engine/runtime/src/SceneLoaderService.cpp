#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/SceneLoaderService.hpp>

#include <algorithm>
#include <format>
#include <vector>

namespace illusion
{

// Out of line so that the vtable is emitted in the library, which only the host links.
SceneLoaderService::~SceneLoaderService() = default;

bool SceneLoaderService::exists(std::string_view virtualPath) const
{
    return m_files.exists(virtualPath);
}

std::optional<u32> SceneLoaderService::instantiate(std::string_view virtualPath, std::span<Entity> created)
{
    m_error.clear();
    if (const FileError error = m_files.readText(virtualPath, m_text); error != FileError::None)
    {
        m_error = std::format("cannot read '{}': {}", virtualPath, fileErrorName(error));
        return std::nullopt;
    }
    const PrefabSource prefabs = [this](std::string_view path) -> std::optional<std::string> {
        std::string text;
        return m_files.readText(path, text) == FileError::None ? std::optional<std::string>(std::move(text))
                                                               : std::nullopt;
    };
    std::vector<Entity> entities;
    if (virtualPath.ends_with(kPrefabExtension))
    {
        PrefabInstantiateResult placed =
            instantiatePrefab(m_world, PrefabData{std::string(virtualPath), m_text}, Entity{},
                              GuidGenerator::shared().next(), prefabs);
        if (!placed.ok())
        {
            m_error = std::format("cannot load '{}': {}", virtualPath, placed.error);
            return std::nullopt;
        }
        entities = std::move(placed.entities);
    }
    else
    {
        SceneLoadOptions options;
        options.ids = IdPolicy::Regenerate;
        options.prefabs = prefabs;
        SceneLoadResult result = loadScene(m_world, m_text, options);
        if (!result.ok())
        {
            m_error = std::format("cannot load '{}': {}", virtualPath, result.error);
            return std::nullopt;
        }
        entities = std::move(result.entities);
    }
    std::copy_n(entities.begin(), std::min(created.size(), entities.size()), created.begin());
    return static_cast<u32>(entities.size());
}

std::string_view SceneLoaderService::lastError() const
{
    return m_error;
}

} // namespace illusion
