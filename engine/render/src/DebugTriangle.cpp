#include <illusion/core/Log.hpp>
#include <illusion/render/BuiltinShaders.hpp>
#include <illusion/render/DebugTriangle.hpp>
#include <illusion/render/RenderDevice.hpp>

namespace illusion
{

std::unique_ptr<DebugTriangle> DebugTriangle::create(RenderDevice& device,
                                                     const nvrhi::FramebufferInfo& framebufferInfo)
{
    nvrhi::IDevice* nvrhiDevice = device.device();
    std::unique_ptr<DebugTriangle> triangle(new DebugTriangle());

    const std::span<const std::byte> vertex = shaders::debugTriangleVertex();
    const std::span<const std::byte> fragment = shaders::debugTriangleFragment();
    triangle->m_vertexShader = nvrhiDevice->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setDebugName("DebugTriangle VS"),
        vertex.data(), vertex.size());
    triangle->m_fragmentShader = nvrhiDevice->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setDebugName("DebugTriangle FS"),
        fragment.data(), fragment.size());
    if (!triangle->m_vertexShader || !triangle->m_fragmentShader)
    {
        ILL_LOG_ERROR("Render", "DebugTriangle: shader creation failed");
        return nullptr;
    }

    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.VS = triangle->m_vertexShader;
    pipelineDesc.PS = triangle->m_fragmentShader;
    pipelineDesc.renderState.rasterState.setCullNone();
    pipelineDesc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
    triangle->m_pipeline = nvrhiDevice->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if (!triangle->m_pipeline)
    {
        ILL_LOG_ERROR("Render", "DebugTriangle: pipeline creation failed");
        return nullptr;
    }
    return triangle;
}

DebugTriangle::~DebugTriangle() = default;

void DebugTriangle::draw(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer) const
{
    const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
    nvrhi::GraphicsState state;
    state.pipeline = m_pipeline;
    state.framebuffer = framebuffer;
    state.viewport.addViewportAndScissorRect(info.getViewport());
    commandList->setGraphicsState(state);
    commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
}

} // namespace illusion
