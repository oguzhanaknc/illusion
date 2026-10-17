#include "EnvironmentBaker.hpp"

#include <illusion/core/Profiler.hpp>
#include <illusion/render/BuiltinShaders.hpp>

#include <algorithm>
#include <bit>

namespace illusion
{
namespace
{

constexpr u32 kGroupSize = 8;
constexpr u32 kMaxSpecularMips = 6;

u32 groups(u32 size) noexcept
{
    return (size + kGroupSize - 1) / kGroupSize;
}

nvrhi::ComputePipelineHandle createPipeline(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout,
                                            std::span<const std::byte> code, const char* name)
{
    nvrhi::ShaderHandle shader =
        device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute).setDebugName(name),
                             code.data(), code.size());
    if (!shader)
    {
        return nullptr;
    }
    return device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(layout));
}

} // namespace

bool EnvironmentBaker::initialize(nvrhi::IDevice* device)
{
    m_device = device;
    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::Compute;
    layout.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(Params)),
    };
    m_layout = device->createBindingLayout(layout);
    if (!m_layout)
    {
        return false;
    }
    m_sampler = device->createSampler(nvrhi::SamplerDesc()
                                          .setAllFilters(true)
                                          .setAddressU(nvrhi::SamplerAddressMode::Wrap)
                                          .setAddressV(nvrhi::SamplerAddressMode::Clamp)
                                          .setAddressW(nvrhi::SamplerAddressMode::Clamp));
    m_equirectToCube = createPipeline(device, m_layout, shaders::iblEquirectToCube(), "IBL equirect to cube");
    m_downsample = createPipeline(device, m_layout, shaders::iblDownsample(), "IBL downsample");
    m_prefilter = createPipeline(device, m_layout, shaders::iblPrefilter(), "IBL prefilter");
    m_brdfLut = createPipeline(device, m_layout, shaders::iblBrdfLut(), "IBL BRDF table");

    nvrhi::TextureDesc dummy;
    dummy.width = 1;
    dummy.height = 1;
    dummy.format = nvrhi::Format::RGBA16_FLOAT;
    dummy.debugName = "IBL dummy source";
    dummy.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    m_dummySource = device->createTexture(dummy);
    return m_sampler && m_equirectToCube && m_downsample && m_prefilter && m_brdfLut && m_dummySource;
}

nvrhi::TextureHandle EnvironmentBaker::createCube(u32 size, u32 mips, const char* debugName) const
{
    nvrhi::TextureDesc desc;
    desc.dimension = nvrhi::TextureDimension::TextureCube;
    desc.width = size;
    desc.height = size;
    desc.arraySize = 6;
    desc.mipLevels = mips;
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.isUAV = true;
    desc.debugName = debugName;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return m_device->createTexture(desc);
}

void EnvironmentBaker::dispatch(nvrhi::ICommandList* commands, nvrhi::IComputePipeline* pipeline,
                                nvrhi::BindingSetItem source, nvrhi::BindingSetItem output,
                                const Params& params, u32 faces)
{
    nvrhi::BindingSetDesc set;
    set.bindings = {
        source,
        nvrhi::BindingSetItem::Sampler(0, m_sampler),
        output,
        nvrhi::BindingSetItem::PushConstants(0, sizeof(Params)),
    };
    nvrhi::BindingSetHandle bindings = m_device->createBindingSet(set, m_layout);
    commands->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(bindings));
    commands->setPushConstants(&params, sizeof(params));
    commands->dispatch(groups(params.size), groups(params.size), faces);
}

nvrhi::TextureHandle EnvironmentBaker::bakeBrdfLut(nvrhi::ICommandList* commands, u32 size)
{
    ILL_PROFILE_SCOPE();
    nvrhi::TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.isUAV = true;
    desc.debugName = "BRDF table";
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    nvrhi::TextureHandle lut = m_device->createTexture(desc);
    dispatch(commands, m_brdfLut, nvrhi::BindingSetItem::Texture_SRV(0, m_dummySource),
             nvrhi::BindingSetItem::Texture_UAV(0, lut), Params{size, 0.0f, 0.0f, 512}, 1);
    return lut;
}

RenderResources::GpuEnvironment EnvironmentBaker::bake(nvrhi::ICommandList* commands,
                                                       nvrhi::ITexture* equirect, const EnvironmentDesc& desc,
                                                       const char* debugName)
{
    ILL_PROFILE_SCOPE();
    const u32 cubeSize = std::bit_floor(std::max(desc.cubeSize, 1u));
    const u32 specularSize = std::bit_floor(std::max(desc.specularSize, 1u));
    const u32 cubeMips = fullMipCount(cubeSize, cubeSize);
    const u32 specularMips = std::min(fullMipCount(specularSize, specularSize), kMaxSpecularMips);

    RenderResources::GpuEnvironment environment;
    environment.background = createCube(cubeSize, cubeMips, debugName);
    environment.specular = createCube(specularSize, specularMips, debugName);
    environment.specularMips = specularMips;

    const auto mipView = [](u32 mip) {
        return nvrhi::TextureSubresourceSet(mip, 1, 0, 6);
    };
    commands->beginMarker("Environment bake");
    dispatch(commands, m_equirectToCube, nvrhi::BindingSetItem::Texture_SRV(0, equirect),
             nvrhi::BindingSetItem::Texture_UAV(0, environment.background, nvrhi::Format::UNKNOWN, mipView(0),
                                                nvrhi::TextureDimension::Texture2DArray),
             Params{cubeSize, 0.0f, 0.0f, 0}, 6);
    for (u32 mip = 1; mip < cubeMips; ++mip)
    {
        dispatch(commands, m_downsample,
                 nvrhi::BindingSetItem::Texture_SRV(0, environment.background, nvrhi::Format::UNKNOWN,
                                                    mipView(mip - 1),
                                                    nvrhi::TextureDimension::Texture2DArray),
                 nvrhi::BindingSetItem::Texture_UAV(0, environment.background, nvrhi::Format::UNKNOWN,
                                                    mipView(mip), nvrhi::TextureDimension::Texture2DArray),
                 Params{std::max(cubeSize >> mip, 1u), 0.0f, 0.0f, 0}, 6);
    }
    for (u32 mip = 0; mip < specularMips; ++mip)
    {
        const f32 roughness =
            specularMips > 1 ? static_cast<f32>(mip) / static_cast<f32>(specularMips - 1) : 0.0f;
        dispatch(commands, m_prefilter, nvrhi::BindingSetItem::Texture_SRV(0, environment.background),
                 nvrhi::BindingSetItem::Texture_UAV(0, environment.specular, nvrhi::Format::UNKNOWN,
                                                    mipView(mip), nvrhi::TextureDimension::Texture2DArray),
                 Params{std::max(specularSize >> mip, 1u), roughness, static_cast<f32>(cubeSize),
                        std::max(desc.sampleCount, 1u)},
                 6);
    }
    commands->endMarker();
    return environment;
}

RenderResources::GpuEnvironment EnvironmentBaker::makeBlack(nvrhi::ICommandList* commands)
{
    RenderResources::GpuEnvironment environment;
    environment.background = createCube(1, 1, "Black environment");
    environment.specular = environment.background;
    // Uploaded rather than cleared: NVRHI keeps uploaded textures alive until the GPU is done with them.
    constexpr u64 kBlack = 0x3C00000000000000; // RGBA16F (0, 0, 0, 1)
    for (u32 face = 0; face < 6; ++face)
    {
        commands->writeTexture(environment.background, face, 0, &kBlack, sizeof(kBlack));
    }
    return environment;
}

} // namespace illusion
