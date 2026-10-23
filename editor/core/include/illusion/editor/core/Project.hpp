#pragma once

/// @file
/// Editor projects: the `project.illusion` manifest, the project layout and its mounts.

#include <illusion/core/Expected.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// File name of the manifest in the project root.
inline constexpr std::string_view kProjectFileName = "project.illusion";

/// The `format` value of manifests.
inline constexpr std::string_view kProjectFormat = "illusion.project";

/// The contents of `project.illusion`.
struct ProjectManifest
{
    std::string name;              ///< Display name of the project.
    std::string version = "0.1.0"; ///< Version of the game (semver).
    std::string engineVersion{};   ///< Engine version the project was last saved with.
    std::string startupScene{};    ///< Scene opened first, e.g. "assets:/scenes/main.scene" ("" for none).
    std::vector<std::string> modules{}; ///< Game modules of the project, by name.
};

/// Reads a manifest; the error says what is wrong.
[[nodiscard]] Expected<ProjectManifest, std::string> parseProjectManifest(std::string_view json);

/// Writes a manifest as formatted JSON.
[[nodiscard]] std::string writeProjectManifest(const ProjectManifest& manifest);

/// One mount of a project: a virtual mount name and the directory it points at.
struct ProjectMount
{
    std::string_view name;     ///< Mount name, e.g. "assets".
    std::string_view relative; ///< Directory relative to the project root, e.g. "assets".
};

/// The project mounts, all read-write: `assets:` (content), `settings:`, `modules:` (module sources) and
/// `cache:` (`.illusion/cache`).
[[nodiscard]] std::span<const ProjectMount> projectMounts() noexcept;

/// An open project: its root directory and manifest.
///
/// A project is a directory with `project.illusion`, `assets/`, `settings/`, `modules/` and the git-ignored
/// `.illusion/` (cache, journal, autosave, trash and per-user state).
class Project
{
public:
    /// Opens the project in @p root, creating missing standard directories.
    [[nodiscard]] static Expected<Project, std::string> open(const std::filesystem::path& root);

    /// Creates a project named @p name in @p root (which must be missing or empty) from the template
    /// directory @p templateDirectory (e.g. `editor-data/templates/empty`), then opens it.
    [[nodiscard]] static Expected<Project, std::string>
    create(const std::filesystem::path& root, std::string_view name,
           const std::filesystem::path& templateDirectory);

    /// The project directory (absolute).
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }

    /// The manifest.
    [[nodiscard]] const ProjectManifest& manifest() const noexcept { return m_manifest; }

    /// `.illusion/` in the project.
    [[nodiscard]] std::filesystem::path stateDirectory() const { return m_root / ".illusion"; }

    /// Mounts the project mounts on @p files.
    [[nodiscard]] FileError mount(VirtualFileSystem& files) const;

    /// Removes the project mounts from @p files.
    void unmount(VirtualFileSystem& files) const;

private:
    std::filesystem::path m_root;
    ProjectManifest m_manifest;
};

} // namespace illusion::editor
