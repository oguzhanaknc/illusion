#include "GpuCulling.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/BuiltinShaders.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <cstring>

namespace illusion
{
namespace
{

constexpr u32 kCullGroupSize = 64;
constexpr u32 kHizGroupSize = 8;

struct CullPush
{
    u32 phase;
    u32 padding[3];
};

nvrhi::BufferHandle createBuffer(nvrhi::IDevice* device, u32 elements, u32 stride, bool uav, bool indirect,
                                 const char* name)
{
    nvrhi::BufferDesc desc;
    desc.byteSize = static_cast<u64>(std::max(elements, 1u)) * stride;
    desc.structStride = stride;
    desc.canHaveUAVs = uav;
    desc.isDrawIndirectArgs = indirect;
    desc.debugName = name;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device->createBuffer(desc);
}

} // namespace

bool GpuCulling::initialize(nvrhi::IDevice* device)
{
    m_device = device;
    nvrhi::BindingLayoutDesc cull;
    cull.visibility = nvrhi::ShaderType::Compute;
    cull.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(CullPush)),
    };
    m_cullLayout = device->createBindingLayout(cull);
    nvrhi::BindingLayoutDesc hiz;
    hiz.visibility = nvrhi::ShaderType::Compute;
    hiz.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_UAV(0)};
    m_hizLayout = device->createBindingLayout(hiz);
    if (!m_cullLayout || !m_hizLayout)
    {
        return false;
    }
    const auto pipeline = [&](std::span<const std::byte> code, nvrhi::IBindingLayout* layout,
                              const char* name) {
        nvrhi::ShaderHandle shader = device->createShader(
            nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute).setDebugName(name), code.data(),
            code.size());
        return shader ? device->createComputePipeline(
                            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(layout))
                      : nullptr;
    };
    m_cullPipeline = pipeline(shaders::occlusionCull(), m_cullLayout, "Occlusion cull");
    m_hizPipeline = pipeline(shaders::hizReduce(), m_hizLayout, "Hi-Z reduce");
    m_constants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(Constants), "Cull constants", 16));
    nvrhi::BufferDesc readback;
    readback.byteSize = sizeof(u32);
    readback.cpuAccess = nvrhi::CpuAccessMode::Read;
    readback.debugName = "Cull statistics readback";
    m_readback = device->createBuffer(readback);

    nvrhi::TextureDesc placeholder;
    placeholder.width = 1;
    placeholder.height = 1;
    placeholder.format = nvrhi::Format::R32_FLOAT;
    placeholder.isUAV = true;
    placeholder.debugName = "Hi-Z placeholder";
    placeholder.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    m_hizPlaceholder = device->createTexture(placeholder);
    m_hiz = m_hizPlaceholder;
    m_hizLevels = 1;
    ensureCapacity(1024, 256, 256);
    return m_cullPipeline && m_hizPipeline && m_constants && m_readback && m_hizPlaceholder;
}

void GpuCulling::resize(nvrhi::ITexture* depth)
{
    const u32 width = std::max(depth->getDesc().width / 2, 1u);
    const u32 height = std::max(depth->getDesc().height / 2, 1u);
    m_hizLevels = fullMipCount(width, height);
    nvrhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = m_hizLevels;
    desc.format = nvrhi::Format::R32_FLOAT;
    desc.isUAV = true;
    desc.debugName = "Hi-Z";
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    m_hiz = m_device->createTexture(desc);

    m_hizSets.clear();
    for (u32 level = 0; level < m_hizLevels; ++level)
    {
        nvrhi::BindingSetDesc set;
        set.bindings = {
            level == 0 ? nvrhi::BindingSetItem::Texture_SRV(0, depth)
                       : nvrhi::BindingSetItem::Texture_SRV(0, m_hiz, nvrhi::Format::UNKNOWN,
                                                            nvrhi::TextureSubresourceSet(level - 1, 1, 0, 1)),
            nvrhi::BindingSetItem::Texture_UAV(0, m_hiz, nvrhi::Format::UNKNOWN,
                                               nvrhi::TextureSubresourceSet(level, 1, 0, 1)),
        };
        m_hizSets.push_back(m_device->createBindingSet(set, m_hizLayout));
    }
    m_historyValid = false;
    rebuildCullSet();
}

void GpuCulling::ensureCapacity(u32 candidates, u32 batches, u32 extraSlots)
{
    const u32 drawListSlots = candidates * 2 + extraSlots;
    if (candidates <= m_candidateCapacity && batches <= m_batchCapacity &&
        drawListSlots <= m_drawListCapacity && m_candidates)
    {
        return;
    }
    m_candidateCapacity = std::max(candidates, m_candidateCapacity * 2);
    m_batchCapacity = std::max(batches, m_batchCapacity * 2);
    m_drawListCapacity = std::max(drawListSlots, m_drawListCapacity * 2);
    m_candidates =
        createBuffer(m_device, m_candidateCapacity, sizeof(Candidate), false, false, "Cull candidates");
    m_rejected = createBuffer(m_device, m_candidateCapacity, sizeof(u32), true, false, "Cull rejected flags");
    m_arguments =
        createBuffer(m_device, m_batchCapacity * 10 + 1, sizeof(u32), true, true, "Indirect arguments");
    m_drawList = createBuffer(m_device, m_drawListCapacity, sizeof(u32), true, false, "Draw list");
    m_argumentScratch.reserve(static_cast<usize>(m_batchCapacity) * 10 + 1);
    m_buffersChanged = true;
    rebuildCullSet();
}

void GpuCulling::rebuildCullSet()
{
    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_constants),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_candidates),
        nvrhi::BindingSetItem::Texture_SRV(1, m_hiz),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_arguments),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_drawList),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_rejected),
        nvrhi::BindingSetItem::PushConstants(1, sizeof(CullPush)),
    };
    m_cullSet = m_device->createBindingSet(set, m_cullLayout);
}

bool GpuCulling::takeBuffersChanged() noexcept
{
    const bool changed = m_buffersChanged;
    m_buffersChanged = false;
    return changed;
}

void GpuCulling::begin(nvrhi::ICommandList* commands, std::span<const Candidate> candidates,
                       std::span<const Batch> batches, u32 extraSlots, const Frame& frame)
{
    ILL_PROFILE_SCOPE();
    m_candidateCount = static_cast<u32>(candidates.size());
    m_batchCount = static_cast<u32>(batches.size());
    ensureCapacity(m_candidateCount, m_batchCount, extraSlots);

    Constants constants{};
    constants.viewProjection = frame.viewProjection;
    constants.previousViewProjection = frame.previousViewProjection;
    constants.hiz = Vec4(static_cast<f32>(m_hiz->getDesc().width), static_cast<f32>(m_hiz->getDesc().height),
                         static_cast<f32>(m_hizLevels), 0.0f);
    constants.candidateCount = m_candidateCount;
    constants.batchCount = m_batchCount;
    constants.phase2Offset = m_candidateCount;
    constants.flags = (frame.occlusion ? 1u : 0u) | (m_historyValid && m_hiz != m_hizPlaceholder ? 2u : 0u);
    commands->writeBuffer(m_constants, &constants, sizeof(constants));

    if (!candidates.empty())
    {
        commands->writeBuffer(m_candidates, candidates.data(), candidates.size_bytes());
    }
    // Both phases start with zero instances; the counter of drawn instances follows them.
    m_argumentScratch.assign(static_cast<usize>(m_batchCount) * 10 + 1, 0u);
    for (u32 phase = 0; phase < 2; ++phase)
    {
        for (u32 b = 0; b < m_batchCount; ++b)
        {
            u32* arguments = m_argumentScratch.data() + (static_cast<usize>(phase) * m_batchCount + b) * 5;
            arguments[0] = batches[b].indexCount;
            arguments[2] = batches[b].firstIndex;
        }
    }
    commands->writeBuffer(m_arguments, m_argumentScratch.data(), m_argumentScratch.size() * sizeof(u32));
}

void GpuCulling::writeDrawList(nvrhi::ICommandList* commands, u32 firstSlot, std::span<const u32> indices)
{
    if (!indices.empty())
    {
        commands->writeBuffer(m_drawList, indices.data(), indices.size_bytes(),
                              static_cast<u64>(firstSlot) * sizeof(u32));
    }
}

void GpuCulling::cull(nvrhi::ICommandList* commands, u32 phase)
{
    if (m_candidateCount == 0)
    {
        return;
    }
    commands->setComputeState(nvrhi::ComputeState().setPipeline(m_cullPipeline).addBindingSet(m_cullSet));
    const CullPush push{phase, {}};
    commands->setPushConstants(&push, sizeof(push));
    commands->dispatch((m_candidateCount + kCullGroupSize - 1) / kCullGroupSize);
}

void GpuCulling::buildHiZ(nvrhi::ICommandList* commands)
{
    ILL_PROFILE_SCOPE();
    if (m_hizSets.empty())
    {
        return;
    }
    commands->beginMarker("Hi-Z");
    for (u32 level = 0; level < m_hizLevels; ++level)
    {
        const u32 width = std::max(m_hiz->getDesc().width >> level, 1u);
        const u32 height = std::max(m_hiz->getDesc().height >> level, 1u);
        commands->setComputeState(
            nvrhi::ComputeState().setPipeline(m_hizPipeline).addBindingSet(m_hizSets[level]));
        commands->dispatch((width + kHizGroupSize - 1) / kHizGroupSize,
                           (height + kHizGroupSize - 1) / kHizGroupSize);
    }
    commands->endMarker();
}

void GpuCulling::finish(nvrhi::ICommandList* commands)
{
    commands->copyBuffer(m_readback, 0, m_arguments, static_cast<u64>(m_batchCount) * 10 * sizeof(u32),
                         sizeof(u32));
    m_historyValid = true;
}

u32 GpuCulling::readDrawnInstances() const
{
    const void* data = m_device->mapBuffer(m_readback, nvrhi::CpuAccessMode::Read);
    u32 value = 0;
    if (data != nullptr)
    {
        std::memcpy(&value, data, sizeof(value));
        m_device->unmapBuffer(m_readback);
    }
    return value;
}

} // namespace illusion
