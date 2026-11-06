#pragma once

/// @file
/// The host's implementation of SceneService over a World and a VirtualFileSystem.
///
/// @code
/// illusion::SceneLoaderService scenes(world, files);
/// services.provide<illusion::SceneService>(scenes);
/// @endcode

#include <illusion/runtime/SceneService.hpp>

#include <string>

namespace illusion
{

class GuidGenerator;
class VirtualFileSystem;
class World;

/// SceneService that reads scene files from a VirtualFileSystem and loads them into a World with
/// loadScene(); both must outlive it. Created by the host; a shared module that constructs one fails to
/// link, since only the host has the library's code (ADR 0041).
class SceneLoaderService final : public SceneService
{
public:
    /// Loads scenes from @p files into @p world. The new EntityGuids of instantiated entities come from
    /// @p guids (which must outlive the service), or from GuidGenerator::shared() when it is null; a seeded
    /// generator makes them repeat from run to run.
    SceneLoaderService(World& world, const VirtualFileSystem& files, GuidGenerator* guids = nullptr) noexcept
        : m_world(world)
        , m_files(files)
        , m_guids(guids)
    {
    }
    ~SceneLoaderService() override;

    /// @name SceneService
    /// @{
    [[nodiscard]] bool exists(std::string_view virtualPath) const override;
    std::optional<u32> instantiate(std::string_view virtualPath, std::span<Entity> created) override;
    [[nodiscard]] std::string_view lastError() const override;
    /// @}

private:
    World& m_world;
    const VirtualFileSystem& m_files;
    GuidGenerator* m_guids; // null: GuidGenerator::shared()
    std::string m_text;     // the last scene file read, kept for its capacity
    std::string m_error;    // why the last instantiate() failed
};

} // namespace illusion
