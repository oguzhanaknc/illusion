#include "RenderResourcesImpl.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/BuiltinShaders.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace illusion
{
namespace
{

// GPU layouts; must match ForwardPbr.slang (std140 for the constant buffer, std430 for instances).
struct GpuDirectionalLight
{
    Vec4 direction;
    Vec4 color;
};

struct GpuPointLight
{
    Vec4 positionRange;
    Vec4 color;
};

struct GpuViewConstants
{
    Mat4 viewProjection;
    Vec4 cameraPosition; // w: exposure
    Vec4 ambient;        // w: 1 if the shader encodes sRGB
    u32 directionalCount;
    u32 pointCount;
    u32 padding[2];
    GpuDirectionalLight directional[SceneRenderer::kMaxDirectionalLights];
    GpuPointLight points[SceneRenderer::kMaxPointLights];
};
static_assert(offsetof(GpuViewConstants, directional) == 112);
static_assert(offsetof(GpuViewConstants, points) == 240);

struct GpuInstance
{
    Mat4 world;
    Mat4 normalMatrix;
};

struct VisibleItem
{
    GpuInstance instance; // computed by the (parallel) culling jobs
    const RenderResources::GpuMesh* mesh;
    const RenderResources::GpuMaterial* material;
    u32 submesh;
    f32 distanceSquared;
};

constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;

} // namespace

RenderView RenderView::fromCamera(const Transform& transform, const Camera& camera, f32 aspect) noexcept
{
    Transform unscaled = transform;
    unscaled.scale = Vec3(1.0f);
    RenderView view;
    view.view = glm::inverse(unscaled.matrix());
    view.projection = perspectiveReversedZ(camera.fovY, aspect, camera.nearPlane);
    view.position = transform.position;
    view.exposure = camera.exposure;
    return view;
}

struct SceneRenderer::Impl
{
    nvrhi::IDevice* device = nullptr;
    RenderResources* resources = nullptr;
    SceneRendererDesc desc;
    bool encodeSrgb = false;

    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle fragmentShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle viewLayout;
    nvrhi::BufferHandle viewConstants;
    nvrhi::BufferHandle instanceBuffer;
    u32 instanceCapacity = 0;
    nvrhi::BindingSetHandle viewSet;
    // [blend][doubleSided]
    std::array<std::array<nvrhi::GraphicsPipelineHandle, 2>, 2> pipelines{};

    nvrhi::TextureHandle depth;
    std::unordered_map<nvrhi::ITexture*, nvrhi::FramebufferHandle> framebuffers;

    // Per-frame scratch, kept across frames so steady state does not allocate.
    std::vector<VisibleItem> visible;
    std::vector<u32> order;
    std::vector<GpuInstance> instances;
    RenderStats stats;

    bool initialize();
    nvrhi::IFramebuffer* framebufferFor(nvrhi::ITexture* color);
    void ensureInstanceCapacity(u32 count);
    void cull(World& world, const RenderView& view, JobSystem* jobs);
    void gatherLights(World& world, GpuViewConstants& constants);
};

bool SceneRenderer::Impl::initialize()
{
    const std::span<const std::byte> vs = shaders::forwardPbrVertex();
    const std::span<const std::byte> fs = shaders::forwardPbrFragment();
    vertexShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setDebugName("ForwardPbr VS"), vs.data(),
        vs.size());
    fragmentShader = device->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setDebugName("ForwardPbr FS"), fs.data(),
        fs.size());
    if (!vertexShader || !fragmentShader)
    {
        return false;
    }

    const nvrhi::VertexAttributeDesc attributes[] = {
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(Vertex, position))
            .setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc()
            .setName("NORMAL")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(Vertex, normal))
            .setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc()
            .setName("TANGENT")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setOffset(offsetof(Vertex, tangent))
            .setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc()
            .setName("TEXCOORD")
            .setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(Vertex, uv))
            .setElementStride(sizeof(Vertex)),
    };
    inputLayout =
        device->createInputLayout(attributes, static_cast<u32>(std::size(attributes)), vertexShader);

    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.registerSpace = 0;
    layout.registerSpaceIsDescriptorSet = true;
    layout.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        // Push constants take a constant-buffer slot in NVRHI, so they cannot share b0 with the view.
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(u32)),
    };
    viewLayout = device->createBindingLayout(layout);
    if (!viewLayout)
    {
        return false;
    }
    viewConstants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GpuViewConstants), "View constants", 16));

    encodeSrgb = !nvrhi::getFormatInfo(desc.colorFormat).isSRGB;
    const nvrhi::FramebufferInfo info =
        nvrhi::FramebufferInfo().addColorFormat(desc.colorFormat).setDepthFormat(kDepthFormat);
    for (u32 blend = 0; blend < 2; ++blend)
    {
        for (u32 doubleSided = 0; doubleSided < 2; ++doubleSided)
        {
            nvrhi::GraphicsPipelineDesc pipeline;
            pipeline.primType = nvrhi::PrimitiveType::TriangleList;
            pipeline.inputLayout = inputLayout;
            pipeline.VS = vertexShader;
            pipeline.PS = fragmentShader;
            pipeline.bindingLayouts = {viewLayout, resources->materialBindingLayout()};
            pipeline.renderState.rasterState.setFrontCounterClockwise(true);
            if (doubleSided != 0)
            {
                pipeline.renderState.rasterState.setCullNone();
            }
            else
            {
                pipeline.renderState.rasterState.setCullBack();
            }
            pipeline.renderState.depthStencilState.setDepthTestEnable(true)
                .setDepthWriteEnable(blend == 0)
                .setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual); // reversed-Z
            if (blend != 0)
            {
                pipeline.renderState.blendState.targets[0]
                    .setBlendEnable(true)
                    .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                    .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                    .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                    .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
            }
            pipelines[blend][doubleSided] = device->createGraphicsPipeline(pipeline, info);
            if (!pipelines[blend][doubleSided])
            {
                return false;
            }
        }
    }
    ensureInstanceCapacity(1024);
    return true;
}

void SceneRenderer::Impl::ensureInstanceCapacity(u32 count)
{
    if (count <= instanceCapacity && instanceBuffer)
    {
        return;
    }
    instanceCapacity = std::max(count, instanceCapacity * 2);
    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = static_cast<u64>(instanceCapacity) * sizeof(GpuInstance);
    bufferDesc.structStride = sizeof(GpuInstance);
    bufferDesc.debugName = "Instances";
    bufferDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    instanceBuffer = device->createBuffer(bufferDesc);

    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, viewConstants),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, instanceBuffer),
        nvrhi::BindingSetItem::PushConstants(1, sizeof(u32)),
    };
    viewSet = device->createBindingSet(set, viewLayout);
}

nvrhi::IFramebuffer* SceneRenderer::Impl::framebufferFor(nvrhi::ITexture* color)
{
    const nvrhi::TextureDesc& colorDesc = color->getDesc();
    if (!depth || depth->getDesc().width != colorDesc.width || depth->getDesc().height != colorDesc.height)
    {
        nvrhi::TextureDesc depthDesc;
        depthDesc.width = colorDesc.width;
        depthDesc.height = colorDesc.height;
        depthDesc.format = kDepthFormat;
        depthDesc.isRenderTarget = true;
        depthDesc.debugName = "Scene depth";
        depthDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::DepthWrite);
        depth = device->createTexture(depthDesc);
        framebuffers.clear();
    }
    // Swapchain recreation produces new textures; drop stale framebuffers before the cache grows.
    if (framebuffers.size() > 8)
    {
        framebuffers.clear();
    }
    nvrhi::FramebufferHandle& framebuffer = framebuffers[color];
    if (!framebuffer)
    {
        framebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(color).setDepthAttachment(depth));
    }
    return framebuffer;
}

void SceneRenderer::Impl::cull(World& world, const RenderView& view, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE_N("SceneRenderer::cull");
    auto query = world.query<const Transform, const MeshRenderer>();
    const u32 candidates = query.count();
    stats.candidates = candidates;
    if (visible.size() < candidates)
    {
        visible.resize(candidates);
    }
    const Frustum frustum = Frustum::fromViewProjection(view.projection * view.view);
    std::atomic<u32> count{0};
    const auto test = [&](const Transform& transform, const MeshRenderer& renderer) {
        const RenderResources::GpuMesh* mesh = resources->mesh(renderer.mesh);
        if (mesh == nullptr || renderer.submesh >= mesh->submeshes.size())
        {
            return;
        }
        const Mat4 matrix = transform.matrix();
        const Sphere bounds = Sphere::fromAabb(mesh->submeshes[renderer.submesh].bounds).transformed(matrix);
        if (!frustum.intersects(bounds))
        {
            return;
        }
        const Vec3 offset = bounds.center - view.position;
        visible[count.fetch_add(1, std::memory_order_relaxed)] =
            VisibleItem{GpuInstance{matrix, glm::transpose(glm::inverse(matrix))}, mesh,
                        resources->material(renderer.material), renderer.submesh, glm::dot(offset, offset)};
    };
    if (jobs != nullptr)
    {
        query.parallelEach(*jobs, test);
    }
    else
    {
        query.each(test);
    }
    stats.visible = count.load();
}

void SceneRenderer::Impl::gatherLights(World& world, GpuViewConstants& constants)
{
    constants.directionalCount = 0;
    world.query<const Transform, const DirectionalLight>().each(
        [&](const Transform& transform, const DirectionalLight& light) {
            if (constants.directionalCount < kMaxDirectionalLights)
            {
                constants.directional[constants.directionalCount++] = GpuDirectionalLight{
                    Vec4(transform.forward(), 0.0f), Vec4(light.color * light.intensity, 0.0f)};
            }
        });
    constants.pointCount = 0;
    world.query<const Transform, const PointLight>().each(
        [&](const Transform& transform, const PointLight& light) {
            if (constants.pointCount < kMaxPointLights)
            {
                constants.points[constants.pointCount++] =
                    GpuPointLight{Vec4(transform.position, std::max(light.range, 1e-3f)),
                                  Vec4(light.color * light.intensity, 0.0f)};
            }
        });
}

SceneRenderer::SceneRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

SceneRenderer::~SceneRenderer() = default;

std::unique_ptr<SceneRenderer> SceneRenderer::create(RenderDevice& device, RenderResources& resources,
                                                     const SceneRendererDesc& desc)
{
    std::unique_ptr<SceneRenderer> renderer(new SceneRenderer());
    Impl& impl = *renderer->m_impl;
    impl.device = device.device();
    impl.resources = &resources;
    impl.desc = desc;
    if (!impl.initialize())
    {
        ILL_LOG_ERROR("Render", "SceneRenderer: pipeline creation failed");
        return nullptr;
    }
    return renderer;
}

void SceneRenderer::render(nvrhi::ICommandList* commandList, nvrhi::ITexture* colorTarget, World& world,
                           const RenderView& view, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.stats = RenderStats{};
    nvrhi::IFramebuffer* framebuffer = impl.framebufferFor(colorTarget);

    impl.cull(world, view, jobs);
    const u32 visibleCount = impl.stats.visible;

    // Opaque and masked first, grouped by material and mesh; then blended back to front.
    impl.order.resize(visibleCount);
    for (u32 i = 0; i < visibleCount; ++i)
    {
        impl.order[i] = i;
    }
    std::sort(impl.order.begin(), impl.order.end(), [&](u32 a, u32 b) {
        const VisibleItem& x = impl.visible[a];
        const VisibleItem& y = impl.visible[b];
        const bool xBlend = x.material->alphaMode == AlphaMode::Blend;
        const bool yBlend = y.material->alphaMode == AlphaMode::Blend;
        if (xBlend != yBlend)
        {
            return !xBlend;
        }
        if (xBlend)
        {
            return x.distanceSquared > y.distanceSquared;
        }
        if (x.material != y.material)
        {
            return x.material < y.material;
        }
        if (x.mesh != y.mesh)
        {
            return x.mesh < y.mesh;
        }
        return x.submesh < y.submesh;
    });

    impl.instances.resize(visibleCount);
    for (u32 i = 0; i < visibleCount; ++i)
    {
        impl.instances[i] = impl.visible[impl.order[i]].instance;
    }
    impl.ensureInstanceCapacity(visibleCount);

    GpuViewConstants constants{};
    constants.viewProjection = view.projection * view.view;
    constants.cameraPosition = Vec4(view.position, view.exposure);
    constants.ambient = Vec4(impl.desc.ambient, impl.encodeSrgb ? 1.0f : 0.0f);
    impl.gatherLights(world, constants);

    commandList->beginMarker("SceneRenderer");
    commandList->clearTextureFloat(colorTarget, nvrhi::AllSubresources,
                                   nvrhi::Color(impl.desc.clearColor.r, impl.desc.clearColor.g,
                                                impl.desc.clearColor.b, impl.desc.clearColor.a));
    commandList->clearDepthStencilTexture(impl.depth, nvrhi::AllSubresources, true, 0.0f, false, 0);
    commandList->writeBuffer(impl.viewConstants, &constants, sizeof(constants));
    if (visibleCount > 0)
    {
        commandList->writeBuffer(impl.instanceBuffer, impl.instances.data(),
                                 visibleCount * sizeof(GpuInstance));
    }

    const nvrhi::Viewport viewport = framebuffer->getFramebufferInfo().getViewport();
    u32 begin = 0;
    while (begin < visibleCount)
    {
        const VisibleItem& first = impl.visible[impl.order[begin]];
        const bool blend = first.material->alphaMode == AlphaMode::Blend;
        u32 end = begin + 1;
        // Transparent objects are drawn one by one to keep the back-to-front order.
        while (!blend && end < visibleCount)
        {
            const VisibleItem& next = impl.visible[impl.order[end]];
            if (next.material != first.material || next.mesh != first.mesh || next.submesh != first.submesh)
            {
                break;
            }
            ++end;
        }

        nvrhi::GraphicsState state;
        state.pipeline = impl.pipelines[blend ? 1 : 0][first.material->doubleSided ? 1 : 0];
        state.framebuffer = framebuffer;
        state.bindings = {impl.viewSet, first.material->bindingSet};
        state.vertexBuffers = {
            nvrhi::VertexBufferBinding().setBuffer(first.mesh->vertexBuffer).setSlot(0).setOffset(0)};
        state.indexBuffer =
            nvrhi::IndexBufferBinding().setBuffer(first.mesh->indexBuffer).setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewportAndScissorRect(viewport);
        commandList->setGraphicsState(state);
        const u32 instanceOffset = begin;
        commandList->setPushConstants(&instanceOffset, sizeof(instanceOffset));

        const Submesh& submesh = first.mesh->submeshes[first.submesh];
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(submesh.indexCount)
                                     .setInstanceCount(end - begin)
                                     .setStartIndexLocation(submesh.indexOffset));
        ++impl.stats.drawCalls;
        impl.stats.triangles += static_cast<u64>(submesh.indexCount / 3) * (end - begin);
        begin = end;
    }
    commandList->endMarker();
}

const RenderStats& SceneRenderer::stats() const noexcept
{
    return m_impl->stats;
}

void SceneRenderer::setAmbient(Vec3 ambient) noexcept
{
    m_impl->desc.ambient = ambient;
}

} // namespace illusion
