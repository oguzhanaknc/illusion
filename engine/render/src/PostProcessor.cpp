#include "PostProcessor.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/BuiltinShaders.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <cmath>

namespace illusion
{
namespace
{

constexpr u32 kGroupSize = 8;
constexpr nvrhi::Format kHdrFormat = nvrhi::Format::RGBA16_FLOAT;

f32 halton(u32 index, u32 base) noexcept
{
    f32 result = 0.0f;
    f32 fraction = 1.0f;
    while (index > 0)
    {
        fraction /= static_cast<f32>(base);
        result += fraction * static_cast<f32>(index % base);
        index /= base;
    }
    return result;
}

nvrhi::ShaderHandle createShader(nvrhi::IDevice* device, nvrhi::ShaderType type,
                                 std::span<const std::byte> code, const char* name)
{
    return device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName(name), code.data(),
                                code.size());
}

nvrhi::TextureHandle createTarget(nvrhi::IDevice* device, u32 width, u32 height, nvrhi::Format format,
                                  u32 mips, const char* name)
{
    nvrhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = mips;
    desc.format = format;
    desc.isUAV = true;
    desc.debugName = name;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device->createTexture(desc);
}

nvrhi::TextureSubresourceSet mip(u32 level)
{
    return nvrhi::TextureSubresourceSet(level, 1, 0, 1);
}

} // namespace

Vec2 temporalJitter(u32 frameIndex) noexcept
{
    const u32 index = frameIndex % 8 + 1;
    return Vec2(halton(index, 2) - 0.5f, halton(index, 3) - 0.5f);
}

Mat4 jitterProjection(const Mat4& projection, Vec2 pixelOffset, Vec2 size) noexcept
{
    // Shift clip space by (dx * w, dy * w) so the image moves by the offset after the perspective divide.
    Mat4 shift(1.0f);
    shift[3][0] = 2.0f * pixelOffset.x / size.x;
    shift[3][1] = -2.0f * pixelOffset.y / size.y; // screen y points down, clip y up
    return shift * projection;
}

bool PostProcessor::initialize(nvrhi::IDevice* device, nvrhi::Format outputFormat)
{
    m_device = device;
    m_encodeSrgb = !nvrhi::getFormatInfo(outputFormat).isSRGB;

    nvrhi::BindingLayoutDesc compute;
    compute.visibility = nvrhi::ShaderType::Compute;
    compute.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(Push)),
    };
    m_computeLayout = device->createBindingLayout(compute);
    nvrhi::BindingLayoutDesc tonemap;
    tonemap.visibility = nvrhi::ShaderType::All;
    tonemap.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(Push)),
    };
    m_tonemapLayout = device->createBindingLayout(tonemap);
    if (!m_computeLayout || !m_tonemapLayout)
    {
        return false;
    }
    m_constants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(Constants), "Post-process constants", 16));
    m_linearClamp = device->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    m_dummy = createTarget(device, 1, 1, kHdrFormat, 1, "Post-process placeholder");

    const auto pipeline = [&](std::span<const std::byte> code, const char* name) {
        nvrhi::ShaderHandle shader = createShader(device, nvrhi::ShaderType::Compute, code, name);
        return shader ? device->createComputePipeline(
                            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(
                                m_computeLayout))
                      : nullptr;
    };
    m_gtao = pipeline(shaders::gtao(), "GTAO");
    m_gtaoBlur = pipeline(shaders::gtaoBlur(), "GTAO blur");
    m_taa = pipeline(shaders::taa(), "TAA");
    m_bloomDownsample = pipeline(shaders::bloomDownsample(), "Bloom downsample");
    m_bloomUpsample = pipeline(shaders::bloomUpsample(), "Bloom upsample");

    m_tonemapVertex = createShader(device, nvrhi::ShaderType::Vertex, shaders::tonemapVertex(), "Tonemap VS");
    m_tonemapFragment =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::tonemapFragment(), "Tonemap FS");
    if (!m_tonemapVertex || !m_tonemapFragment)
    {
        return false;
    }
    nvrhi::GraphicsPipelineDesc tonemapPipeline;
    tonemapPipeline.primType = nvrhi::PrimitiveType::TriangleList;
    tonemapPipeline.VS = m_tonemapVertex;
    tonemapPipeline.PS = m_tonemapFragment;
    tonemapPipeline.bindingLayouts = {m_tonemapLayout};
    tonemapPipeline.renderState.rasterState.setCullNone();
    tonemapPipeline.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
    m_tonemap = device->createGraphicsPipeline(tonemapPipeline,
                                               nvrhi::FramebufferInfo().addColorFormat(outputFormat));
    return m_constants && m_linearClamp && m_dummy && m_gtao && m_gtaoBlur && m_taa && m_bloomDownsample &&
           m_bloomUpsample && m_tonemap;
}

nvrhi::BindingSetHandle PostProcessor::computeSet(nvrhi::ITexture* t0, nvrhi::TextureSubresourceSet t0Range,
                                                  nvrhi::ITexture* t1, nvrhi::ITexture* t2,
                                                  nvrhi::ITexture* output,
                                                  nvrhi::TextureSubresourceSet outputRange,
                                                  nvrhi::ITexture* t3) const
{
    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_constants),
        nvrhi::BindingSetItem::Texture_SRV(0, t0, nvrhi::Format::UNKNOWN, t0Range),
        nvrhi::BindingSetItem::Texture_SRV(1, t1 != nullptr ? t1 : m_dummy.Get()),
        nvrhi::BindingSetItem::Texture_SRV(2, t2 != nullptr ? t2 : m_dummy.Get()),
        nvrhi::BindingSetItem::Texture_SRV(3, t3 != nullptr ? t3 : m_dummy.Get()),
        nvrhi::BindingSetItem::Sampler(0, m_linearClamp),
        nvrhi::BindingSetItem::Texture_UAV(0, output, nvrhi::Format::UNKNOWN, outputRange),
        nvrhi::BindingSetItem::PushConstants(1, sizeof(Push)),
    };
    return m_device->createBindingSet(set, m_computeLayout);
}

void PostProcessor::resize(nvrhi::ITexture* hdr, nvrhi::ITexture* depth, nvrhi::ITexture* motion,
                           const PostProcessSettings& settings)
{
    ILL_PROFILE_SCOPE();
    m_width = hdr->getDesc().width;
    m_height = hdr->getDesc().height;
    m_rawAmbientOcclusion =
        createTarget(m_device, m_width, m_height, nvrhi::Format::R32_FLOAT, 1, "GTAO raw");
    m_ambientOcclusion = createTarget(m_device, m_width, m_height, nvrhi::Format::R32_FLOAT, 1, "GTAO");
    m_history[0] = createTarget(m_device, m_width, m_height, kHdrFormat, 1, "TAA history 0");
    m_history[1] = createTarget(m_device, m_width, m_height, kHdrFormat, 1, "TAA history 1");
    const u32 bloomWidth = std::max(m_width / 2, 1u);
    const u32 bloomHeight = std::max(m_height / 2, 1u);
    m_bloomLevels = std::clamp(settings.bloom.maxLevels, 1u, fullMipCount(bloomWidth, bloomHeight));
    m_bloom = createTarget(m_device, bloomWidth, bloomHeight, kHdrFormat, m_bloomLevels, "Bloom");

    const nvrhi::TextureSubresourceSet all = nvrhi::AllSubresources;
    m_gtaoSet = computeSet(depth, all, nullptr, nullptr, m_rawAmbientOcclusion, mip(0));
    m_gtaoBlurSet = computeSet(m_rawAmbientOcclusion, all, depth, nullptr, m_ambientOcclusion, mip(0));
    for (u32 i = 0; i < 2; ++i)
    {
        m_taaSets[i] = computeSet(hdr, all, depth, m_history[1 - i], m_history[i], mip(0), motion);
    }
    const std::array<nvrhi::ITexture*, kSourceCount> sources = {hdr, m_history[0], m_history[1]};
    m_bloomDownsampleSets.assign(m_bloomLevels, nullptr);
    m_bloomUpsampleSets.assign(m_bloomLevels, nullptr);
    for (u32 source = 0; source < kSourceCount; ++source)
    {
        m_bloomFirstSets[source] = computeSet(sources[source], all, nullptr, nullptr, m_bloom, mip(0));

        nvrhi::BindingSetDesc set;
        set.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_constants),
            nvrhi::BindingSetItem::Texture_SRV(0, sources[source]),
            nvrhi::BindingSetItem::Texture_SRV(1, m_bloom, nvrhi::Format::UNKNOWN, mip(0)),
            nvrhi::BindingSetItem::Sampler(0, m_linearClamp),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(Push)),
        };
        m_tonemapSets[source] = m_device->createBindingSet(set, m_tonemapLayout);
    }
    for (u32 level = 1; level < m_bloomLevels; ++level)
    {
        m_bloomDownsampleSets[level] =
            computeSet(m_bloom, mip(level - 1), nullptr, nullptr, m_bloom, mip(level));
    }
    for (u32 level = 0; level + 1 < m_bloomLevels; ++level)
    {
        m_bloomUpsampleSets[level] =
            computeSet(m_bloom, mip(level + 1), nullptr, nullptr, m_bloom, mip(level));
    }
}

void PostProcessor::beginFrame(nvrhi::ICommandList* commands, const Frame& frame,
                               const PostProcessSettings& settings)
{
    const f32 width = static_cast<f32>(m_width);
    const f32 height = static_cast<f32>(m_height);
    Constants constants{};
    constants.inverseProjection = glm::inverse(frame.projection);
    constants.inverseViewProjection = glm::inverse(frame.viewProjection);
    constants.previousViewProjection = frame.previousViewProjection;
    constants.size = Vec4(width, height, 1.0f / width, 1.0f / height);
    // Golden-ratio noise offset per frame lets TAA average the GTAO noise; without TAA it stays still.
    const bool temporal = settings.temporalAntiAliasing.enabled;
    const f32 noiseOffset = temporal ? std::fmod(static_cast<f32>(frame.frameIndex) * 0.618034f, 1.0f) : 0.0f;
    constants.ao =
        Vec4(std::max(settings.ambientOcclusion.radius, 1e-3f), settings.ambientOcclusion.intensity,
             frame.projection[1][1] * height * 0.5f, noiseOffset);
    constants.temporal = Vec4(std::clamp(settings.temporalAntiAliasing.currentFrameWeight, 0.01f, 1.0f),
                              frame.historyValid ? 1.0f : 0.0f, frame.projection[3][3], 0.0f);
    constants.bloom = Vec4(static_cast<f32>(m_bloomLevels), std::clamp(settings.bloom.intensity, 0.0f, 1.0f),
                           settings.bloom.enabled ? 1.0f : 0.0f, 0.0f);
    constants.output = Vec4(frame.exposure, m_encodeSrgb ? 1.0f : 0.0f, frame.diagnostic ? 1.0f : 0.0f, 0.0f);
    commands->writeBuffer(m_constants, &constants, sizeof(constants));
    if (temporal)
    {
        m_historyIndex = 1 - m_historyIndex;
    }
}

void PostProcessor::dispatch(nvrhi::ICommandList* commands, nvrhi::IComputePipeline* pipeline,
                             nvrhi::IBindingSet* set, const Push& push, u32 width, u32 height) const
{
    commands->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(set));
    commands->setPushConstants(&push, sizeof(push));
    commands->dispatch((width + kGroupSize - 1) / kGroupSize, (height + kGroupSize - 1) / kGroupSize, 1);
}

void PostProcessor::computeAmbientOcclusion(nvrhi::ICommandList* commands)
{
    ILL_PROFILE_SCOPE();
    commands->beginMarker("GTAO");
    dispatch(commands, m_gtao, m_gtaoSet, Push{}, m_width, m_height);
    dispatch(commands, m_gtaoBlur, m_gtaoBlurSet, Push{}, m_width, m_height);
    commands->endMarker();
}

void PostProcessor::resolve(nvrhi::ICommandList* commands, nvrhi::IFramebuffer* output,
                            const PostProcessSettings& settings)
{
    ILL_PROFILE_SCOPE();
    Source source = kHdr;
    if (settings.temporalAntiAliasing.enabled)
    {
        commands->beginMarker("TAA");
        dispatch(commands, m_taa, m_taaSets[m_historyIndex], Push{}, m_width, m_height);
        commands->endMarker();
        source = m_historyIndex == 0 ? kHistory0 : kHistory1;
    }
    if (settings.bloom.enabled)
    {
        commands->beginMarker("Bloom");
        const u32 bloomWidth = m_bloom->getDesc().width;
        const u32 bloomHeight = m_bloom->getDesc().height;
        dispatch(commands, m_bloomDownsample, m_bloomFirstSets[source], Push{0, 1, {}}, bloomWidth,
                 bloomHeight);
        for (u32 level = 1; level < m_bloomLevels; ++level)
        {
            dispatch(commands, m_bloomDownsample, m_bloomDownsampleSets[level], Push{level, 0, {}},
                     std::max(bloomWidth >> level, 1u), std::max(bloomHeight >> level, 1u));
        }
        for (u32 level = m_bloomLevels - 1; level-- > 0;)
        {
            dispatch(commands, m_bloomUpsample, m_bloomUpsampleSets[level], Push{level, 0, {}},
                     std::max(bloomWidth >> level, 1u), std::max(bloomHeight >> level, 1u));
        }
        commands->endMarker();
    }

    nvrhi::GraphicsState state;
    state.pipeline = m_tonemap;
    state.framebuffer = output;
    state.bindings = {m_tonemapSets[source]};
    state.viewport.addViewportAndScissorRect(output->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    const Push push{};
    commands->setPushConstants(&push, sizeof(push));
    commands->draw(nvrhi::DrawArguments().setVertexCount(3));
}

} // namespace illusion
