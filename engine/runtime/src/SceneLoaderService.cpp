#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/SceneLoaderService.hpp>

#include <algorithm>
#include <format>

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
    SceneLoadOptions options;
    options.ids = IdPolicy::Regenerate;
    SceneLoadResult result = loadScene(m_world, m_text, options);
    if (!result.ok())
    {
        m_error = std::format("cannot load '{}': {}", virtualPath, result.error);
        return std::nullopt;
    }
    std::copy_n(result.entities.begin(), std::min(created.size(), result.entities.size()), created.begin());
    return static_cast<u32>(result.entities.size());
}

std::string_view SceneLoaderService::lastError() const
{
    return m_error;
}

} // namespace illusion
