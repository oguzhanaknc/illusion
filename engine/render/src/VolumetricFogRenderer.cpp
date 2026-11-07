#include "VolumetricFogRenderer.hpp"

#include <illusion/core/Profiler.hpp>
#include <illusion/render/BuiltinShaders.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace illusion
{
namespace
{

constexpr nvrhi::Format kFogFormat = nvrhi::Format::RGBA16_FLOAT;
// Weight of the current frame against the reprojected history: about 8 frames of accumulation.
constexpr f32 kCurrentWeight = 0.12f;

f32 halton2(u32 index) noexcept
{
    f32 result = 0.0f;
    f32 fraction = 1.0f;
    while (index > 0)
    {
        fraction *= 0.5f;
        result += fraction * static_cast<f32>(index & 1u);
        index >>= 1u;
    }
    return result;
}

u32 divideRoundingUp(u32 value, u32 divisor) noexcept
{
    return (value + divisor - 1) / divisor;
}

nvrhi::TextureHandle createVolume(nvrhi::IDevice* device, UVec3 size, bool uav, const char* name)
{
    nvrhi::TextureDesc desc;
    desc.dimension = nvrhi::TextureDimension::Texture3D;
    desc.width = size.x;
    desc.height = size.y;
    desc.depth = size.z;
    desc.format = kFogFormat;
    desc.isUAV = uav;
    desc.debugName = name;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device->createTexture(desc);
}

bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool VolumetricFogRenderer::initialize(nvrhi::IDevice* device)
{
    m_device = device;
    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::Compute;
    layout.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // view constants
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // fog constants
        nvrhi::BindingLayoutItem::Texture_SRV(0),            // scattering history
        nvrhi::BindingLayoutItem::Texture_SRV(1),            // scene depth
        nvrhi::BindingLayoutItem::Texture_SRV(2),            // scattering or integrated fog
        nvrhi::BindingLayoutItem::Texture_SRV(4),            // shadow map
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
        nvrhi::BindingLayoutItem::Texture_UAV(0), // volume output
        nvrhi::BindingLayoutItem::Texture_UAV(1), // image output
    };
    m_layout = device->createBindingLayout(layout);
    if (!m_layout)
    {
        return false;
    }
    m_constants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(Constants), "Volumetric fog constants", 16));
    m_linearClamp = device->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    m_shadowCompare = device->createSampler(nvrhi::SamplerDesc()
                                                .setAllFilters(true)
                                                .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
                                                .setReductionType(nvrhi::SamplerReductionType::Comparison));
    m_placeholderVolume = createVolume(device, UVec3(1), false, "Volumetric fog placeholder");
    m_scratchVolume = createVolume(device, UVec3(1), true, "Volumetric fog scratch volume");
    nvrhi::TextureDesc image;
    image.width = 1;
    image.height = 1;
    image.format = kFogFormat;
    image.isUAV = true;
    image.debugName = "Volumetric fog scratch image";
    image.enableAutomaticStateTracking(nvrhi::ResourceStates::UnorderedAccess);
    m_scratchImage = device->createTexture(image);

    const auto pipeline = [&](std::span<const std::byte> code, const char* name) {
        nvrhi::ShaderHandle shader = device->createShader(
            nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute).setDebugName(name), code.data(),
            code.size());
        return shader ? device->createComputePipeline(
                            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(m_layout))
                      : nullptr;
    };
    m_inject = pipeline(shaders::fogInject(), "Volumetric fog inject");
    m_integrate = pipeline(shaders::fogIntegrate(), "Volumetric fog integrate");
    m_apply = pipeline(shaders::fogApply(), "Volumetric fog apply");
    return m_constants && m_linearClamp && m_shadowCompare && m_placeholderVolume && m_scratchVolume &&
           m_scratchImage && m_inject && m_integrate && m_apply;
}

void VolumetricFogRenderer::resize(nvrhi::ITexture* hdr, nvrhi::ITexture* depth)
{
    m_hdr = hdr;
    m_depth = depth;
    m_size = UVec2(hdr->getDesc().width, hdr->getDesc().height);
    m_injectSets = {};
    m_integrateSets = {};
    m_applySet = nullptr;
    m_historyValid = false;
}

Vec4 VolumetricFogRenderer::prepare(const Frame& frame)
{
    const VolumetricFog* fog = frame.fog;
    m_active = fog != nullptr && m_size.x > 0 && m_size.y > 0 && std::isfinite(fog->density) &&
               fog->density > 0.0f && std::isfinite(fog->heightFalloff) && std::isfinite(fog->baseHeight) &&
               finite(fog->albedo) && std::isfinite(fog->anisotropy) && std::isfinite(fog->lightIntensity) &&
               std::isfinite(fog->ambientIntensity) && std::isfinite(fog->maxDistance) &&
               std::isfinite(fog->noiseAmount) && std::isfinite(fog->noiseSize) &&
               std::isfinite(fog->windFactor);
    if (!m_active)
    {
        m_historyValid = false;
        return Vec4(0.0f);
    }
    ILL_PROFILE_SCOPE();
    const u32 tile = std::max(8u, divideRoundingUp(std::max(m_size.x, m_size.y), kMaxColumns));
    const UVec2 grid(divideRoundingUp(m_size.x, tile), divideRoundingUp(m_size.y, tile));
    if (grid != m_grid || !m_integrated)
    {
        m_grid = grid;
        createVolumes();
    }
    const f32 far = std::max(fog->maxDistance, 1.0f);
    const f32 near = std::clamp(far * 0.005f, 0.05f, 1.0f);
    const f32 logRange = std::log(far / near);
    const auto slices = static_cast<f32>(kSlices);
    m_parity ^= 1u;

    m_frame.inverseViewProjection = frame.inverseViewProjection;
    // Without history the samples sit in the middle of their slices; with it they cycle through them.
    const f32 jitter = m_historyValid ? halton2(frame.frameIndex % 8u + 1u) : 0.5f;
    m_frame.grid = Vec4(static_cast<f32>(grid.x), static_cast<f32>(grid.y), slices, jitter);
    m_frame.depth = Vec4(near, far, logRange, 0.0f);
    m_frame.medium = Vec4(glm::clamp(fog->albedo, Vec3(0.0f), Vec3(1.0f)), fog->density);
    m_frame.shape = Vec4(std::max(fog->heightFalloff, 0.0f), fog->baseHeight,
                         std::clamp(fog->anisotropy, -0.9f, 0.9f), std::max(fog->ambientIntensity, 0.0f));
    m_frame.noise = Vec4(std::clamp(fog->noiseAmount, 0.0f, 1.0f), 1.0f / std::max(fog->noiseSize, 0.1f),
                         std::max(fog->windFactor, 0.0f), std::max(fog->lightIntensity, 0.0f));
    m_frame.history = Vec4(m_historyValid ? 1.0f : 0.0f, kCurrentWeight, 0.0f, 0.0f);
    m_frame.previousCamera = Vec4(m_previousPosition, m_previousNear);
    m_frame.previousForward = Vec4(m_previousForward, m_previousLogRange);
    m_previousPosition = frame.cameraPosition;
    m_previousForward = frame.cameraForward;
    m_previousNear = near;
    m_previousLogRange = logRange;
    return Vec4(1.0f, near, slices / logRange, slices);
}

bool VolumetricFogRenderer::takeVolumeChanged() noexcept
{
    return std::exchange(m_volumeChanged, false);
}

nvrhi::ITexture* VolumetricFogRenderer::volume() const noexcept
{
    return m_integrated ? m_integrated.Get() : m_placeholderVolume.Get();
}

void VolumetricFogRenderer::createVolumes()
{
    const UVec3 size(m_grid, kSlices);
    m_scattering[0] = createVolume(m_device, size, true, "Volumetric fog scattering 0");
    m_scattering[1] = createVolume(m_device, size, true, "Volumetric fog scattering 1");
    m_integrated = createVolume(m_device, size, true, "Volumetric fog");
    m_injectSets = {};
    m_integrateSets = {};
    m_applySet = nullptr;
    m_historyValid = false;
    m_volumeChanged = true;
}

nvrhi::BindingSetHandle VolumetricFogRenderer::createSet(nvrhi::IBuffer* viewConstants,
                                                         nvrhi::ITexture* history, nvrhi::ITexture* volume,
                                                         nvrhi::ITexture* outVolume,
                                                         nvrhi::ITexture* outImage) const
{
    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, viewConstants),
        nvrhi::BindingSetItem::ConstantBuffer(1, m_constants),
        nvrhi::BindingSetItem::Texture_SRV(0, history),
        nvrhi::BindingSetItem::Texture_SRV(1, m_depth),
        nvrhi::BindingSetItem::Texture_SRV(2, volume),
        nvrhi::BindingSetItem::Texture_SRV(4, m_boundShadowMap),
        nvrhi::BindingSetItem::Sampler(0, m_linearClamp),
        nvrhi::BindingSetItem::Sampler(1, m_shadowCompare),
        nvrhi::BindingSetItem::Texture_UAV(0, outVolume),
        nvrhi::BindingSetItem::Texture_UAV(1, outImage),
    };
    return m_device->createBindingSet(set, m_layout);
}

void VolumetricFogRenderer::createSets(nvrhi::IBuffer* viewConstants, nvrhi::ITexture* shadowMap)
{
    if (m_applySet && m_boundConstants == viewConstants && m_boundShadowMap == shadowMap)
    {
        return;
    }
    m_boundConstants = viewConstants;
    m_boundShadowMap = shadowMap;
    for (u32 parity = 0; parity < 2; ++parity)
    {
        m_injectSets[parity] = createSet(viewConstants, m_scattering[1 - parity], m_placeholderVolume,
                                         m_scattering[parity], m_scratchImage);
        m_integrateSets[parity] =
            createSet(viewConstants, m_placeholderVolume, m_scattering[parity], m_integrated, m_scratchImage);
    }
    m_applySet = createSet(viewConstants, m_placeholderVolume, m_integrated, m_scratchVolume, m_hdr);
}

void VolumetricFogRenderer::compute(nvrhi::ICommandList* commands, nvrhi::IBuffer* viewConstants,
                                    nvrhi::ITexture* shadowMap)
{
    if (!m_active)
    {
        return;
    }
    ILL_PROFILE_SCOPE();
    createSets(viewConstants, shadowMap);
    commands->beginMarker("Volumetric fog");
    commands->writeBuffer(m_constants, &m_frame, sizeof(m_frame));
    nvrhi::ComputeState state;
    state.pipeline = m_inject;
    state.bindings = {m_injectSets[m_parity]};
    commands->setComputeState(state);
    commands->dispatch(divideRoundingUp(m_grid.x, 4), divideRoundingUp(m_grid.y, 4), kSlices / 4);
    state.pipeline = m_integrate;
    state.bindings = {m_integrateSets[m_parity]};
    commands->setComputeState(state);
    commands->dispatch(divideRoundingUp(m_grid.x, 8), divideRoundingUp(m_grid.y, 8), 1);
    commands->endMarker();
    m_historyValid = true;
}

void VolumetricFogRenderer::apply(nvrhi::ICommandList* commands)
{
    if (!m_active || !m_applySet)
    {
        return;
    }
    ILL_PROFILE_SCOPE();
    commands->beginMarker("Volumetric fog apply");
    nvrhi::ComputeState state;
    state.pipeline = m_apply;
    state.bindings = {m_applySet};
    commands->setComputeState(state);
    commands->dispatch(divideRoundingUp(m_size.x, 8), divideRoundingUp(m_size.y, 8), 1);
    commands->endMarker();
}

} // namespace illusion
