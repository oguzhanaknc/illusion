#include <illusion/core/Profiler.hpp>
#include <illusion/render/BuiltinShaders.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace illusion
{
namespace
{
bool finite(Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool validStyle(Vec4 color, f32 width, DebugDepth depth) noexcept
{
    return finite(Vec3(color)) && std::isfinite(color.a) && color.a >= 0.0f && color.a <= 1.0f &&
           std::isfinite(width) && width > 0.0f &&
           (depth == DebugDepth::Tested || depth == DebugDepth::OnTop);
}
} // namespace

DebugDraw::DebugDraw(usize maxLines)
    : m_maxLines(maxLines)
{
    m_lines.reserve(maxLines);
}
void DebugDraw::clear() noexcept
{
    m_lines.clear();
}
std::span<const DebugLine> DebugDraw::lines() const noexcept
{
    return m_lines;
}

bool DebugDraw::line(Vec3 start, Vec3 end, Vec4 color, f32 width, DebugDepth depth) noexcept
{
    if (m_lines.size() == m_maxLines || !finite(start) || !finite(end) || start == end ||
        !validStyle(color, width, depth))
    {
        return false;
    }
    m_lines.push_back(
        DebugLine{Vec4(start, width), Vec4(end, depth == DebugDepth::OnTop ? 1.0f : 0.0f), color});
    return true;
}

bool DebugDraw::box(Vec3 min, Vec3 max, Vec4 color, f32 width, DebugDepth depth) noexcept
{
    if (m_maxLines - m_lines.size() < 12 || !finite(min) || !finite(max) || min.x >= max.x ||
        min.y >= max.y || min.z >= max.z || !validStyle(color, width, depth))
    {
        return false;
    }
    std::array<Vec3, 8> corners;
    for (u32 i = 0; i < 8; ++i)
    {
        corners[i] =
            Vec3((i & 1u) != 0 ? max.x : min.x, (i & 2u) != 0 ? max.y : min.y, (i & 4u) != 0 ? max.z : min.z);
    }
    for (u32 i = 0; i < 8; ++i)
    {
        for (u32 axis = 1; axis <= 4; axis <<= 1u)
        {
            if ((i & axis) == 0)
            {
                (void)line(corners[i], corners[i | axis], color, width, depth);
            }
        }
    }
    return true;
}

bool DebugDraw::sphere(Vec3 center, f32 radius, Vec4 color, u32 segments, f32 width,
                       DebugDepth depth) noexcept
{
    if (segments < 8 || segments > 256 || m_maxLines - m_lines.size() < static_cast<usize>(segments) * 3 ||
        !finite(center) || !std::isfinite(radius) || radius <= 0.0f || !validStyle(color, width, depth))
    {
        return false;
    }
    // Reject overflow and radii too small to produce distinct vertices at this centre, atomically.
    if (!finite(center + Vec3(radius)) || !finite(center - Vec3(radius)) ||
        glm::any(glm::equal(center + Vec3(radius), center)))
    {
        return false;
    }
    const usize before = m_lines.size();
    for (u32 axis = 0; axis < 3; ++axis)
    {
        for (u32 i = 0; i < segments; ++i)
        {
            const auto point = [&](u32 step) {
                const f32 angle = 2.0f * std::numbers::pi_v<f32> * static_cast<f32>(step % segments) /
                                  static_cast<f32>(segments);
                Vec3 result = center;
                result[static_cast<int>((axis + 1) % 3)] += radius * std::cos(angle);
                result[static_cast<int>((axis + 2) % 3)] += radius * std::sin(angle);
                return result;
            };
            if (!line(point(i), point(i + 1), color, width, depth))
            {
                m_lines.resize(before);
                return false;
            }
        }
    }
    return true;
}

struct DebugDrawRenderer::Impl
{
    struct Constants
    {
        Mat4 viewProjection;
        Vec4 sizes;   // render xy, output zw
        Vec4 options; // x: explicit sRGB encoding
    };
    nvrhi::IDevice* device = nullptr;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle lines;
    nvrhi::BindingSetHandle bindings;
    nvrhi::FramebufferHandle framebuffer;
    nvrhi::ITexture* boundDepth = nullptr;
    nvrhi::ITexture* boundTarget = nullptr;
    usize capacity = 0;
};

DebugDrawRenderer::DebugDrawRenderer()
    : m_impl(std::make_unique<Impl>())
{
}
DebugDrawRenderer::~DebugDrawRenderer() = default;

std::unique_ptr<DebugDrawRenderer> DebugDrawRenderer::create(RenderDevice& device, nvrhi::Format outputFormat)
{
    std::unique_ptr<DebugDrawRenderer> renderer(new DebugDrawRenderer());
    Impl& impl = *renderer->m_impl;
    impl.device = device.device();
    impl.format = outputFormat;
    const auto shader = [&](nvrhi::ShaderType type, std::span<const std::byte> bytes) {
        return impl.device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName("DebugDraw"),
                                         bytes.data(), bytes.size());
    };
    const auto vertex = shader(nvrhi::ShaderType::Vertex, shaders::debugDrawVertex());
    const auto fragment = shader(nvrhi::ShaderType::Pixel, shaders::debugDrawFragment());
    if (!vertex || !fragment)
    {
        return nullptr;
    }
    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                       nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
                       nvrhi::BindingLayoutItem::Texture_SRV(1)};
    impl.layout = impl.device->createBindingLayout(layout);
    impl.constants = impl.device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(Impl::Constants), "Debug draw constants", 16));
    if (!impl.layout || !impl.constants)
    {
        return nullptr;
    }
    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.VS = vertex;
    pipeline.PS = fragment;
    pipeline.primType = nvrhi::PrimitiveType::TriangleList;
    pipeline.bindingLayouts = {impl.layout};
    pipeline.renderState.rasterState.setCullNone();
    pipeline.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
    pipeline.renderState.blendState.targets[0]
        .setBlendEnable(true)
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
    impl.pipeline =
        impl.device->createGraphicsPipeline(pipeline, nvrhi::FramebufferInfo().addColorFormat(outputFormat));
    return impl.pipeline ? std::move(renderer) : nullptr;
}

bool DebugDrawRenderer::render(nvrhi::ICommandList* commands, nvrhi::ITexture* target,
                               const RenderViewTargets& view, const DebugDraw& draw)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (!commands || !target || !view.depth || view.renderSize.x == 0 || view.renderSize.y == 0 ||
        view.outputSize.x != target->getDesc().width || view.outputSize.y != target->getDesc().height ||
        view.renderSize.x != view.depth->getDesc().width ||
        view.renderSize.y != view.depth->getDesc().height || target->getDesc().format != impl.format ||
        draw.lines().size() > std::numeric_limits<u32>::max())
    {
        return false;
    }
    if (draw.lines().empty())
    {
        return true;
    }
    if (draw.lines().size() > impl.capacity)
    {
        impl.capacity = std::max(draw.lines().size(), usize{16384});
        nvrhi::BufferDesc desc;
        desc.byteSize = impl.capacity * sizeof(DebugLine);
        desc.structStride = sizeof(DebugLine);
        desc.debugName = "Debug lines";
        desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
        impl.lines = impl.device->createBuffer(desc);
        impl.bindings = nullptr;
        if (!impl.lines)
        {
            impl.capacity = 0;
            return false;
        }
    }
    if (!impl.bindings || impl.boundDepth != view.depth)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, impl.constants),
                         nvrhi::BindingSetItem::StructuredBuffer_SRV(0, impl.lines),
                         nvrhi::BindingSetItem::Texture_SRV(1, view.depth)};
        impl.bindings = impl.device->createBindingSet(desc, impl.layout);
        impl.boundDepth = view.depth;
    }
    if (!impl.framebuffer || impl.boundTarget != target)
    {
        impl.framebuffer =
            impl.device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
        impl.boundTarget = target;
    }
    if (!impl.bindings || !impl.framebuffer)
    {
        return false;
    }
    const Impl::Constants constants{
        view.viewProjection,
        Vec4(static_cast<f32>(view.renderSize.x), static_cast<f32>(view.renderSize.y),
             static_cast<f32>(view.outputSize.x), static_cast<f32>(view.outputSize.y)),
        Vec4(nvrhi::getFormatInfo(impl.format).isSRGB ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f)};
    commands->writeBuffer(impl.constants, &constants, sizeof(constants));
    commands->writeBuffer(impl.lines, draw.lines().data(), draw.lines().size_bytes());
    nvrhi::GraphicsState state;
    state.pipeline = impl.pipeline;
    state.framebuffer = impl.framebuffer;
    state.bindings = {impl.bindings};
    state.viewport.addViewportAndScissorRect(impl.framebuffer->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    commands->draw(
        nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(static_cast<u32>(draw.lines().size())));
    return true;
}

} // namespace illusion
