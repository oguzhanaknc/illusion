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

    // An environment whose cube maps redrawAtmosphere() redraws in place; its sky constants and binding sets
    // are made here, once.
    [[nodiscard]] RenderResources::GpuEnvironment createAtmosphere(u32 cubeSize, u32 specularSize,
                                                                   const char* debugName);

    // Records the redraw of an environment made by createAtmosphere() from `atmosphere`: the sky into the
    // background's mip 0, its mip chain, and the prefiltered reflections with `sampleCount` samples. Does not
    // allocate.
    void redrawAtmosphere(nvrhi::ICommandList* commands, const RenderResources::GpuEnvironment& environment,
                          const GpuAtmosphere& atmosphere, u32 sampleCount);

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
    [[nodiscard]] nvrhi::BindingSetHandle passSet(nvrhi::BindingSetItem source,
                                                  nvrhi::BindingSetItem output) const;
    static void run(nvrhi::ICommandList* commands, nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* set,
                    const Params& params, u32 faces);

    nvrhi::IDevice* m_device = nullptr;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::SamplerHandle m_sampler; // linear, wraps horizontally (equirect seam), clamps vertically
    nvrhi::ComputePipelineHandle m_equirectToCube;
    nvrhi::ComputePipelineHandle m_downsample;
    nvrhi::ComputePipelineHandle m_prefilter;
    nvrhi::ComputePipelineHandle m_brdfLut;
    nvrhi::BindingLayoutHandle m_atmosphereLayout; // b0 sky constants, u0 output, push constants
    nvrhi::ComputePipelineHandle m_atmosphereCube;
    nvrhi::TextureHandle m_dummySource; // bound to t0 by passes without a source
};

} // namespace illusion
