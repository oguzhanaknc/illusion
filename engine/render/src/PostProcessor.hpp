#pragma once

// Internal: post-processing passes of the SceneRenderer (GTAO, TAA, bloom, tonemapping) and their
// size-dependent textures and cached binding sets.

#include <illusion/render/PostProcess.hpp>

#include <nvrhi/nvrhi.h>

#include <array>
#include <vector>

namespace illusion
{

class PostProcessor
{
public:
    // Per-frame inputs.
    struct Frame
    {
        Mat4 projection;             // jittered
        Mat4 viewProjection;         // jittered
        Mat4 previousViewProjection; // unjittered
        f32 exposure = 1.0f;
        bool historyValid = false;
        u32 frameIndex = 0;
    };

    bool initialize(nvrhi::IDevice* device, nvrhi::Format outputFormat);

    // Recreates the size-dependent textures and binding sets for `hdr` and `depth` (both of the render
    // size). Call when they change.
    void resize(nvrhi::ITexture* hdr, nvrhi::ITexture* depth, nvrhi::ITexture* motion,
                const PostProcessSettings& settings);

    // Screen-space ambient occlusion texture (R32F, 1 = unoccluded) read by the forward pass.
    [[nodiscard]] nvrhi::ITexture* ambientOcclusion() const noexcept { return m_ambientOcclusion; }

    // Writes the per-frame constants. Call before the passes below.
    void beginFrame(nvrhi::ICommandList* commands, const Frame& frame, const PostProcessSettings& settings);

    // GTAO from the depth buffer (after the depth prepass).
    void computeAmbientOcclusion(nvrhi::ICommandList* commands);

    // Bloom, TAA (when enabled) and tonemapping of the HDR image into `output`.
    void resolve(nvrhi::ICommandList* commands, nvrhi::IFramebuffer* output,
                 const PostProcessSettings& settings);

private:
    struct Constants
    {
        Mat4 inverseProjection;
        Mat4 inverseViewProjection;
        Mat4 previousViewProjection;
        Vec4 size;
        Vec4 ao;
        Vec4 temporal;
        Vec4 bloom;
        Vec4 output;
    };
    struct Push
    {
        u32 level;
        u32 firstPass;
        u32 padding[2];
    };
    // Which image feeds bloom and tonemapping.
    enum Source : u32
    {
        kHdr = 0,
        kHistory0 = 1,
        kHistory1 = 2,
        kSourceCount = 3,
    };

    [[nodiscard]] nvrhi::BindingSetHandle
    computeSet(nvrhi::ITexture* t0, nvrhi::TextureSubresourceSet t0Range, nvrhi::ITexture* t1,
               nvrhi::ITexture* t2, nvrhi::ITexture* output, nvrhi::TextureSubresourceSet outputRange,
               nvrhi::ITexture* t3 = nullptr) const;
    void dispatch(nvrhi::ICommandList* commands, nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* set,
                  const Push& push, u32 width, u32 height) const;

    nvrhi::IDevice* m_device = nullptr;
    bool m_encodeSrgb = false;
    nvrhi::BindingLayoutHandle m_computeLayout;
    nvrhi::BindingLayoutHandle m_tonemapLayout;
    nvrhi::BufferHandle m_constants;
    nvrhi::SamplerHandle m_linearClamp;
    nvrhi::TextureHandle m_dummy;
    nvrhi::ComputePipelineHandle m_gtao;
    nvrhi::ComputePipelineHandle m_gtaoBlur;
    nvrhi::ComputePipelineHandle m_taa;
    nvrhi::ComputePipelineHandle m_bloomDownsample;
    nvrhi::ComputePipelineHandle m_bloomUpsample;
    nvrhi::ShaderHandle m_tonemapVertex;
    nvrhi::ShaderHandle m_tonemapFragment;
    nvrhi::GraphicsPipelineHandle m_tonemap;

    u32 m_width = 0;
    u32 m_height = 0;
    nvrhi::TextureHandle m_rawAmbientOcclusion;
    nvrhi::TextureHandle m_ambientOcclusion;
    std::array<nvrhi::TextureHandle, 2> m_history{};
    nvrhi::TextureHandle m_bloom; // half resolution, one mip per level
    u32 m_bloomLevels = 0;
    u32 m_historyIndex = 0; // history written this frame

    nvrhi::BindingSetHandle m_gtaoSet;
    nvrhi::BindingSetHandle m_gtaoBlurSet;
    std::array<nvrhi::BindingSetHandle, 2> m_taaSets{};
    std::array<nvrhi::BindingSetHandle, kSourceCount> m_bloomFirstSets{};
    std::vector<nvrhi::BindingSetHandle> m_bloomDownsampleSets; // level i >= 1 from level i - 1
    std::vector<nvrhi::BindingSetHandle> m_bloomUpsampleSets;   // level i from level i + 1
    std::array<nvrhi::BindingSetHandle, kSourceCount> m_tonemapSets{};
};

} // namespace illusion
