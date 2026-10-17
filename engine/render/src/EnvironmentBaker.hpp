#pragma once

// Internal: GPU compute passes that turn an equirectangular image into the cube maps used for
// image-based lighting, and bake the split-sum BRDF table. Used by RenderResources.

#include "RenderResourcesImpl.hpp"

#include <nvrhi/nvrhi.h>

namespace illusion
{

class EnvironmentBaker
{
public:
    // Creates shaders, the shared binding layout and pipelines. Returns false on failure.
    bool initialize(nvrhi::IDevice* device);

    // Records the BRDF table bake (RGBA16F, x = scale, y = bias) into `commands`.
    [[nodiscard]] nvrhi::TextureHandle bakeBrdfLut(nvrhi::ICommandList* commands, u32 size);

    // Records the cube map bakes from `equirect` (a 2D texture) into `commands`.
    [[nodiscard]] RenderResources::GpuEnvironment bake(nvrhi::ICommandList* commands,
                                                       nvrhi::ITexture* equirect, const EnvironmentDesc& desc,
                                                       const char* debugName);

    // A black 1x1 environment, cleared in `commands`.
    [[nodiscard]] RenderResources::GpuEnvironment makeBlack(nvrhi::ICommandList* commands);

private:
    struct Params
    {
        u32 size;
        f32 roughness;
        f32 sourceSize;
        u32 sampleCount;
    };

    [[nodiscard]] nvrhi::TextureHandle createCube(u32 size, u32 mips, const char* debugName) const;
    void dispatch(nvrhi::ICommandList* commands, nvrhi::IComputePipeline* pipeline,
                  nvrhi::BindingSetItem source, nvrhi::BindingSetItem output, const Params& params,
                  u32 faces);

    nvrhi::IDevice* m_device = nullptr;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::SamplerHandle m_sampler; // linear, wraps horizontally (equirect seam), clamps vertically
    nvrhi::ComputePipelineHandle m_equirectToCube;
    nvrhi::ComputePipelineHandle m_downsample;
    nvrhi::ComputePipelineHandle m_prefilter;
    nvrhi::ComputePipelineHandle m_brdfLut;
    nvrhi::TextureHandle m_dummySource; // bound to t0 by passes without a source
};

} // namespace illusion
