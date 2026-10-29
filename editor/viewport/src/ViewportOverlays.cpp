#include "ViewportOverlays.hpp"

#include <illusion/core/Profiler.hpp>
#include <illusion/editor/viewport/ViewportShaders.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <bit>

namespace illusion::editor::viewport::detail
{
namespace
{

static_assert(std::endian::native == std::endian::little,
              "the outline shader reads the u64 selection words as pairs of u32 words");

struct GridConstants
{
    Mat4 inverseViewProjection;
    Mat4 viewProjection;
    Vec4 sizes;
    Vec4 eye;
    Vec4 scale;
    Vec4 color;
    Vec4 axisU;
    Vec4 axisV;
};

struct OutlineConstants
{
    Vec4 sizes;
    Vec4 color;
    glm::uvec4 options;
};

Vec4 sizesOf(const RenderViewTargets& view) noexcept
{
    return Vec4(static_cast<f32>(view.renderSize.x), static_cast<f32>(view.renderSize.y),
                static_cast<f32>(view.outputSize.x), static_cast<f32>(view.outputSize.y));
}

} // namespace

struct ViewportOverlays::Impl
{
    nvrhi::IDevice* device = nullptr;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;

    nvrhi::BindingLayoutHandle gridLayout;
    nvrhi::GraphicsPipelineHandle gridPipeline;
    nvrhi::BufferHandle gridConstants;
    nvrhi::BindingSetHandle gridBindings;
    nvrhi::ITexture* gridDepth = nullptr;

    nvrhi::BindingLayoutHandle outlineLayout;
    nvrhi::GraphicsPipelineHandle outlinePipeline;
    nvrhi::BufferHandle outlineConstants;
    nvrhi::BufferHandle selection;
    usize selectionWords = 0;
    nvrhi::BindingSetHandle outlineBindings;
    nvrhi::ITexture* outlineIds = nullptr;

    nvrhi::FramebufferHandle framebuffer;
    nvrhi::ITexture* framebufferTarget = nullptr;

    nvrhi::GraphicsPipelineHandle pipeline(nvrhi::IBindingLayout* layout, nvrhi::ShaderHandle vertex,
                                           nvrhi::ShaderHandle fragment) const
    {
        nvrhi::GraphicsPipelineDesc desc;
        desc.VS = vertex;
        desc.PS = fragment;
        desc.primType = nvrhi::PrimitiveType::TriangleList;
        desc.bindingLayouts = {layout};
        desc.renderState.rasterState.setCullNone();
        desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
        desc.renderState.blendState.targets[0]
            .setBlendEnable(true)
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
            .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::One)
            .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
        return device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(format));
    }

    bool bindTarget(nvrhi::ITexture* target)
    {
        if (!framebuffer || framebufferTarget != target)
        {
            framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
            framebufferTarget = target;
        }
        return framebuffer != nullptr;
    }

    void draw(nvrhi::ICommandList* commands, nvrhi::IGraphicsPipeline* pipelineState,
              nvrhi::IBindingSet* bindings) const
    {
        nvrhi::GraphicsState state;
        state.pipeline = pipelineState;
        state.framebuffer = framebuffer;
        state.bindings = {bindings};
        state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
        commands->setGraphicsState(state);
        commands->draw(nvrhi::DrawArguments().setVertexCount(3));
    }
};

ViewportOverlays::ViewportOverlays()
    : m_impl(std::make_unique<Impl>())
{
}

ViewportOverlays::~ViewportOverlays() = default;

std::unique_ptr<ViewportOverlays> ViewportOverlays::create(RenderDevice& device, nvrhi::Format format)
{
    std::unique_ptr<ViewportOverlays> overlays(new ViewportOverlays());
    Impl& impl = *overlays->m_impl;
    impl.device = device.device();
    impl.format = format;
    const auto shader = [&](nvrhi::ShaderType type, std::span<const std::byte> bytes, const char* name) {
        return impl.device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName(name),
                                         bytes.data(), bytes.size());
    };
    const nvrhi::ShaderHandle gridVertex = shader(nvrhi::ShaderType::Vertex, shaders::gridVertex(), "Grid");
    const nvrhi::ShaderHandle gridFragment =
        shader(nvrhi::ShaderType::Pixel, shaders::gridFragment(), "Grid");
    const nvrhi::ShaderHandle outlineVertex =
        shader(nvrhi::ShaderType::Vertex, shaders::outlineVertex(), "Outline");
    const nvrhi::ShaderHandle outlineFragment =
        shader(nvrhi::ShaderType::Pixel, shaders::outlineFragment(), "Outline");
    if (!gridVertex || !gridFragment || !outlineVertex || !outlineFragment)
    {
        return nullptr;
    }

    nvrhi::BindingLayoutDesc grid;
    grid.visibility = nvrhi::ShaderType::All;
    grid.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                     nvrhi::BindingLayoutItem::Texture_SRV(0)};
    impl.gridLayout = impl.device->createBindingLayout(grid);
    nvrhi::BindingLayoutDesc outline;
    outline.visibility = nvrhi::ShaderType::All;
    outline.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                        nvrhi::BindingLayoutItem::Texture_SRV(0),
                        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1)};
    impl.outlineLayout = impl.device->createBindingLayout(outline);
    impl.gridConstants = impl.device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GridConstants), "Grid constants", 16));
    impl.outlineConstants = impl.device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(OutlineConstants), "Outline constants", 16));
    if (!impl.gridLayout || !impl.outlineLayout || !impl.gridConstants || !impl.outlineConstants)
    {
        return nullptr;
    }
    impl.gridPipeline = impl.pipeline(impl.gridLayout, gridVertex, gridFragment);
    impl.outlinePipeline = impl.pipeline(impl.outlineLayout, outlineVertex, outlineFragment);
    return impl.gridPipeline && impl.outlinePipeline ? std::move(overlays) : nullptr;
}

bool ViewportOverlays::drawGrid(nvrhi::ICommandList* commands, nvrhi::ITexture* target,
                                const RenderViewTargets& view, const GridPass& pass)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (commands == nullptr || target == nullptr || view.depth == nullptr || !impl.bindTarget(target))
    {
        return false;
    }
    if (!impl.gridBindings || impl.gridDepth != view.depth)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, impl.gridConstants),
                         nvrhi::BindingSetItem::Texture_SRV(0, view.depth)};
        impl.gridBindings = impl.device->createBindingSet(desc, impl.gridLayout);
        impl.gridDepth = view.depth;
    }
    if (!impl.gridBindings)
    {
        return false;
    }
    const GridConstants constants{pass.inverseViewProjection,
                                  view.viewProjection,
                                  sizesOf(view),
                                  pass.eye,
                                  pass.scale,
                                  pass.color,
                                  pass.axisU,
                                  pass.axisV};
    commands->writeBuffer(impl.gridConstants, &constants, sizeof(constants));
    impl.draw(commands, impl.gridPipeline, impl.gridBindings);
    return true;
}

bool ViewportOverlays::drawOutline(nvrhi::ICommandList* commands, nvrhi::ITexture* target,
                                   const RenderViewTargets& view, std::span<const u64> selection, Vec4 color,
                                   u32 radius)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (commands == nullptr || target == nullptr || view.ids == nullptr || selection.empty() ||
        !impl.bindTarget(target))
    {
        return false;
    }
    if (selection.size() > impl.selectionWords)
    {
        // Grows in steps of 64K entities so that a growing scene reallocates rarely.
        impl.selectionWords = (selection.size() + 1023) / 1024 * 1024;
        nvrhi::BufferDesc desc;
        desc.byteSize = impl.selectionWords * sizeof(u64);
        desc.structStride = sizeof(u32);
        desc.debugName = "Selection bits";
        desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
        impl.selection = impl.device->createBuffer(desc);
        impl.outlineBindings = nullptr;
        if (!impl.selection)
        {
            impl.selectionWords = 0;
            return false;
        }
    }
    if (!impl.outlineBindings || impl.outlineIds != view.ids)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, impl.outlineConstants),
                         nvrhi::BindingSetItem::Texture_SRV(0, view.ids),
                         nvrhi::BindingSetItem::StructuredBuffer_SRV(1, impl.selection)};
        impl.outlineBindings = impl.device->createBindingSet(desc, impl.outlineLayout);
        impl.outlineIds = view.ids;
    }
    if (!impl.outlineBindings)
    {
        return false;
    }
    // The radius is given in output pixels; the IDs are at render resolution.
    const f32 ratio = view.outputSize.x > 0
                          ? static_cast<f32>(view.renderSize.x) / static_cast<f32>(view.outputSize.x)
                          : 1.0f;
    const u32 renderRadius = std::max(1u, static_cast<u32>(static_cast<f32>(radius) * ratio + 0.5f));
    const OutlineConstants constants{
        sizesOf(view), color, glm::uvec4(static_cast<u32>(selection.size() * 2), renderRadius, 0u, 0u)};
    commands->writeBuffer(impl.outlineConstants, &constants, sizeof(constants));
    commands->writeBuffer(impl.selection, selection.data(), selection.size_bytes());
    impl.draw(commands, impl.outlinePipeline, impl.outlineBindings);
    return true;
}

} // namespace illusion::editor::viewport::detail
