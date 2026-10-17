#pragma once

// Internal: GPU-driven occlusion culling of the camera's opaque draws (two-phase Hi-Z) with indirect
// draw arguments and a draw list of instance indices. Used by SceneRenderer.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <nvrhi/nvrhi.h>

#include <span>
#include <vector>

namespace illusion
{

class GpuCulling
{
public:
    // One cullable instance (layout matches Candidate in OcclusionCull.slang).
    struct Candidate
    {
        Vec4 sphere;    // world-space centre, radius
        u32 batch;      // index of its batch
        u32 batchStart; // first draw-list slot of the batch (= its first candidate)
        u32 padding[2];
    };
    // A run of candidates sharing mesh, submesh and material; drawn with one indirect draw per phase.
    struct Batch
    {
        u32 indexCount;
        u32 firstIndex;   // in the index pool
        u32 vertexOffset; // added to every index (the mesh's place in the vertex pool)
        u32 start;        // first draw-list slot of the batch within a phase region
    };
    struct Frame
    {
        Mat4 viewProjection;         // unjittered
        Mat4 previousViewProjection; // unjittered
        bool occlusion = true;
        // Multi-draw: each batch's draw-list offset goes into its first-instance field instead of a push
        // constant.
        bool firstInstanceOffsets = false;
    };

    bool initialize(nvrhi::IDevice* device);

    // Recreates the Hi-Z pyramid for a depth buffer of this size (invalidates the history).
    void resize(nvrhi::ITexture* depth);

    // Forgets the previous frame's Hi-Z (camera cut, settings change).
    void invalidateHistory() noexcept { m_historyValid = false; }

    // Uploads this frame's candidates and batches and resets the indirect arguments. `extraSlots`
    // reserves draw-list entries after both phase regions, written by `writeDrawList()`.
    void begin(nvrhi::ICommandList* commands, std::span<const Candidate> candidates,
               std::span<const Batch> batches, u32 extraSlots, const Frame& frame);

    // Writes `indices` into the draw list starting at `firstSlot` (after both phase regions).
    void writeDrawList(nvrhi::ICommandList* commands, u32 firstSlot, std::span<const u32> indices);

    // Phase 1 (against the previous Hi-Z) or phase 2 (rejected candidates against the current Hi-Z).
    void cull(nvrhi::ICommandList* commands, u32 phase);

    // Builds the Hi-Z pyramid from the depth buffer given to resize().
    void buildHiZ(nvrhi::ICommandList* commands);

    // Copies the drawn-instance counter to the readback buffer. Call at the end of the frame.
    void finish(nvrhi::ICommandList* commands);

    // Instances drawn by the last finished frame (maps the readback buffer: the GPU must be idle).
    [[nodiscard]] u32 readDrawnInstances() const;

    [[nodiscard]] nvrhi::IBuffer* drawList() const noexcept { return m_drawList; }
    [[nodiscard]] nvrhi::IBuffer* arguments() const noexcept { return m_arguments; }
    // Byte offset of a batch's indirect arguments in `arguments()`.
    [[nodiscard]] u32 argumentOffset(u32 batch, u32 phase) const noexcept
    {
        return ((phase == 1 ? 0u : m_batchCount) + batch) * 5u * 4u;
    }
    // First draw-list slot of the phase-2 region; the extra slots start at twice this value.
    [[nodiscard]] u32 phase2Offset() const noexcept { return m_candidateCount; }
    // Becomes true when buffers were recreated (binding sets that use drawList() must be rebuilt).
    [[nodiscard]] bool takeBuffersChanged() noexcept;

private:
    struct Constants
    {
        Mat4 viewProjection;
        Mat4 previousViewProjection;
        Vec4 hiz;
        u32 candidateCount;
        u32 batchCount;
        u32 phase2Offset;
        u32 flags;
    };

    void ensureCapacity(u32 candidates, u32 batches, u32 extraSlots);
    void rebuildCullSet();

    nvrhi::IDevice* m_device = nullptr;
    nvrhi::BindingLayoutHandle m_cullLayout;
    nvrhi::BindingLayoutHandle m_hizLayout;
    nvrhi::ComputePipelineHandle m_cullPipeline;
    nvrhi::ComputePipelineHandle m_hizPipeline;
    nvrhi::BufferHandle m_constants;
    nvrhi::BufferHandle m_candidates;
    nvrhi::BufferHandle m_arguments;
    nvrhi::BufferHandle m_drawList;
    nvrhi::BufferHandle m_rejected;
    nvrhi::BufferHandle m_readback;
    nvrhi::BindingSetHandle m_cullSet;
    nvrhi::TextureHandle m_hiz;
    nvrhi::TextureHandle m_hizPlaceholder;
    std::vector<nvrhi::BindingSetHandle> m_hizSets; // level i from the depth (i = 0) or level i - 1
    u32 m_hizLevels = 0;
    u32 m_candidateCapacity = 0;
    u32 m_batchCapacity = 0;
    u32 m_drawListCapacity = 0;
    u32 m_candidateCount = 0;
    u32 m_batchCount = 0;
    bool m_historyValid = false;
    bool m_buffersChanged = true;
    std::vector<u32> m_argumentScratch;
};

} // namespace illusion
