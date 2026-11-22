#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/core/EditorNavigation.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

namespace illusion::editor
{
namespace
{

struct MeshEntry
{
    AssetHandle<ModelData> model;
    std::optional<MeshData> builtin;
    const MeshData* mesh = nullptr;
    bool loading = false;
};

} // namespace

struct EditorNavigation::Impl
{
    Impl(World& world, VirtualFileSystem& files)
        : streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""})
        , navigation(world, &streamer)
    {
    }

    JobSystem jobs;
    AssetStreamer streamer;
    NavWorld navigation;
    std::unordered_map<u64, MeshEntry> meshes;
};

EditorNavigation::EditorNavigation(World& world, VirtualFileSystem& files)
    : m_impl(std::make_unique<Impl>(world, files))
{
}

EditorNavigation::~EditorNavigation() = default;

NavWorld& EditorNavigation::world() noexcept
{
    return m_impl->navigation;
}

const NavWorld& EditorNavigation::world() const noexcept
{
    return m_impl->navigation;
}

JobSystem& EditorNavigation::jobs() noexcept
{
    return m_impl->jobs;
}

void EditorNavigation::update()
{
    ILL_PROFILE_SCOPE();
    m_impl->streamer.update();
    m_impl->navigation.update(0.0f, &m_impl->jobs, false);
}

void EditorNavigation::waitIdle()
{
    ILL_PROFILE_SCOPE();
    m_impl->streamer.update();
    m_impl->navigation.waitIdle(&m_impl->jobs);
}

void EditorNavigation::pump()
{
    m_impl->streamer.update();
}

void EditorNavigation::reload(std::string_view path)
{
    static_cast<void>(m_impl->streamer.reload(path));
}

void EditorNavigation::forgetMeshes()
{
    m_impl->meshes.clear();
}

const MeshData* EditorNavigation::findMesh(const AssetRef& mesh, u32 meshIndex, bool& waits)
{
    Impl& impl = *m_impl;
    const auto [found, inserted] = impl.meshes.try_emplace(hashCombine(mesh.id.value, meshIndex));
    MeshEntry& entry = found->second;
    const std::optional<AssetPathParts> parts = splitAssetPath(mesh.path());
    if (inserted)
    {
        const bool selectsMesh =
            parts && (parts->fragment == AssetFragment::None || parts->fragment == AssetFragment::Mesh ||
                      parts->fragment == AssetFragment::Node);
        if (!selectsMesh)
        {
            ILL_LOG_WARN("editor", "'{}' of a Collider is not a mesh", mesh.path());
        }
        else if (isOnBuiltinMount(parts->file))
        {
            entry.builtin =
                parts->fragment == AssetFragment::None ? makeBuiltinMesh(parts->file) : std::nullopt;
            entry.mesh = entry.builtin ? &*entry.builtin : nullptr;
        }
        else
        {
            entry.model = impl.streamer.loadModel(parts->file);
            entry.loading = true;
        }
    }
    if (!entry.loading)
    {
        return entry.mesh;
    }
    if (entry.model.state() == AssetState::Loading)
    {
        waits = true;
        return nullptr;
    }
    entry.loading = false;
    const ModelData* model = entry.model.get();
    if (model == nullptr || !parts)
    {
        ILL_LOG_WARN("editor", "cannot read the mesh '{}' of a Collider: {}", mesh.path(),
                     entry.model.error());
        return nullptr;
    }
    i32 index =
        parts->fragment == AssetFragment::Mesh ? static_cast<i32>(parts->index) : static_cast<i32>(meshIndex);
    if (parts->fragment == AssetFragment::Node)
    {
        const auto node = std::ranges::find(model->nodes, parts->node, &ModelNode::name);
        index = node != model->nodes.end() ? node->mesh : -1;
    }
    if (index >= 0 && static_cast<usize>(index) < model->meshes.size())
    {
        entry.mesh = &model->meshes[static_cast<usize>(index)];
    }
    return entry.mesh;
}

} // namespace illusion::editor
