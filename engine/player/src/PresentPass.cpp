#include "PresentPass.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/player/PlayerShaders.hpp>
#include <illusion/render/RenderDevice.hpp>

namespace illusion::detail
{
namespace
{

struct Push
{
    u32 decodeSrgb = 0;
    u32 padding[3] = {};
};

} // namespace

std::unique_ptr<PresentPass> PresentPass::create(RenderDevice& device, nvrhi::IFramebuffer* framebuffer)
{
    std::unique_ptr<PresentPass> pass(new PresentPass());
    nvrhi::IDevice* gpu = device.device();
    pass->m_device = gpu;
    const std::span<const std::byte> vertex = shaders::presentVertex();
    const std::span<const std::byte> fragment = shaders::presentFragment();
    pass->m_vertex = gpu->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setDebugName("Present VS"),
        vertex.data(), vertex.size());
    pass->m_fragment = gpu->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setDebugName("Present FS"),
        fragment.data(), fragment.size());
    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(Push)),
    };
    pass->m_layout = gpu->createBindingLayout(layout);
    pass->m_sampler = gpu->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    if (!pass->m_vertex || !pass->m_fragment || !pass->m_layout || !pass->m_sampler)
    {
        ILL_LOG_ERROR("Player", "cannot create the present pass");
        return nullptr;
    }
    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.primType = nvrhi::PrimitiveType::TriangleList;
    pipeline.VS = pass->m_vertex;
    pipeline.PS = pass->m_fragment;
    pipeline.bindingLayouts = {pass->m_layout};
    pipeline.renderState.rasterState.setCullNone();
    pipeline.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
    pass->m_pipeline = gpu->createGraphicsPipeline(pipeline, framebuffer->getFramebufferInfo());
    if (!pass->m_pipeline)
    {
        ILL_LOG_ERROR("Player", "cannot create the present pipeline");
        return nullptr;
    }
    const nvrhi::Format format = framebuffer->getDesc().colorAttachments[0].texture->getDesc().format;
    pass->m_decodeSrgb = nvrhi::getFormatInfo(format).isSRGB;
    return pass;
}

void PresentPass::draw(nvrhi::ICommandList* commands, nvrhi::IFramebuffer* framebuffer,
                       nvrhi::ITexture* source)
{
    if (source != m_boundSource)
    {
        nvrhi::BindingSetDesc set;
        set.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, source),
            nvrhi::BindingSetItem::Sampler(0, m_sampler),
            nvrhi::BindingSetItem::PushConstants(0, sizeof(Push)),
        };
        m_bindings = m_device->createBindingSet(set, m_layout);
        m_boundSource = source;
    }
    nvrhi::GraphicsState state;
    state.pipeline = m_pipeline;
    state.framebuffer = framebuffer;
    state.bindings = {m_bindings};
    state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    Push push;
    push.decodeSrgb = m_decodeSrgb ? 1u : 0u;
    commands->setPushConstants(&push, sizeof(push));
    commands->draw(nvrhi::DrawArguments().setVertexCount(3));
}

} // namespace illusion::detail
