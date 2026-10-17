#include <illusion/core/Profiler.hpp>
#include <illusion/render/UploadQueue.hpp>

#include <utility>

namespace illusion
{

std::shared_ptr<const ModelUpload> UploadQueue::upload(AssetHandle<ModelData> model)
{
    auto progress = std::make_shared<ModelUpload>();
    m_queue.push_back(Entry{std::move(model), progress});
    return progress;
}

u64 UploadQueue::uploadSize(const ModelData& model) noexcept
{
    u64 bytes = 0;
    for (const MeshData& mesh : model.meshes)
    {
        bytes += mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(u32);
    }
    for (const Image& image : model.images)
    {
        bytes += image.data.size();
    }
    return bytes;
}

u32 UploadQueue::process(u64 byteBudget)
{
    ILL_PROFILE_SCOPE();
    u32 finished = 0;
    u64 spent = 0;
    for (auto it = m_queue.begin(); it != m_queue.end();)
    {
        const AssetState state = it->model.state();
        if (state == AssetState::Loading)
        {
            ++it;
            continue;
        }
        if (state == AssetState::Failed)
        {
            it->progress->failed = true;
            it = m_queue.erase(it);
            ++finished;
            continue;
        }
        const ModelData& model = it->model.value();
        const u64 size = uploadSize(model);
        if (finished > 0 && spent + size > byteBudget)
        {
            break; // over budget: continue next frame
        }
        it->progress->handles = m_resources->uploadModel(model);
        it->progress->uploaded = true;
        spent += size;
        it = m_queue.erase(it);
        ++finished;
    }
    return finished;
}

} // namespace illusion
