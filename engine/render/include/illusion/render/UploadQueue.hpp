#pragma once

/// @file
/// Spreads GPU uploads of streamed models over frames with a byte budget, so a large model arriving from
/// the AssetStreamer does not stall one frame.

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/render/RenderResources.hpp>

#include <deque>
#include <memory>

namespace illusion
{

/// Progress of one queued model upload.
struct ModelUpload
{
    bool uploaded = false; ///< True once the model's meshes, textures and materials are on the GPU.
    bool failed = false;   ///< True if the model failed to load (see the AssetHandle's error).
    ModelHandles handles;  ///< GPU handles, valid once uploaded.
};

/// Queue of streamed models waiting for their GPU upload. Use on the render thread.
class UploadQueue : NonCopyable
{
public:
    /// Uploads into @p resources, which must outlive the queue.
    explicit UploadQueue(RenderResources& resources) noexcept
        : m_resources(&resources)
    {
    }

    /// Queues @p model; it is uploaded by process() once loaded. The returned progress is updated by
    /// process().
    [[nodiscard]] std::shared_ptr<const ModelUpload> upload(AssetHandle<ModelData> model);

    /// Uploads loaded models in request order until about @p byteBudget bytes were uploaded (at least one
    /// model per call, so large models make progress). Models still loading keep their place. Returns the
    /// number of uploads finished (uploaded or failed).
    u32 process(u64 byteBudget);

    /// Models queued and not yet finished.
    [[nodiscard]] u32 pending() const noexcept { return static_cast<u32>(m_queue.size()); }

    /// GPU bytes a model needs (vertices, indices and image data).
    [[nodiscard]] static u64 uploadSize(const ModelData& model) noexcept;

private:
    struct Entry
    {
        AssetHandle<ModelData> model;
        std::shared_ptr<ModelUpload> progress;
    };

    RenderResources* m_resources;
    std::deque<Entry> m_queue;
};

} // namespace illusion
