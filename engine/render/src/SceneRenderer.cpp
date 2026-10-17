#include "PostProcessor.hpp"
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
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace illusion
{
namespace
{

// GPU layouts; must match Common.slang (std140 for the constant buffer, std430 for instances).
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

struct GpuShadowCascade
{
    Mat4 viewProjection;
    Vec4 params; // x: split far, y: world size, z: depth range
};

struct GpuViewConstants
{
    Mat4 viewProjection;
    Vec4 cameraPosition; // xyz
    Vec4 effects;        // x: 1 if screen-space ambient occlusion is applied
    u32 directionalCount;
    u32 pointCount;
    u32 padding[2];
    GpuDirectionalLight directional[SceneRenderer::kMaxDirectionalLights];
    GpuPointLight points[SceneRenderer::kMaxPointLights];
    Mat4 inverseViewProjection;
    Mat4 skyRotation; // world direction -> environment direction
    Vec4 sky;         // x: specular intensity, y: last specular mip, z: background intensity
    Vec4 sh[9];       // diffuse irradiance, intensity applied
    GpuShadowCascade cascades[kMaxShadowCascades];
    Vec4 shadow;        // x: cascade count, y: shadowed light index, z: tan(angular radius), w: resolution
    Vec4 shadowBias;    // x: depth bias, y: normal bias (texels), z: max distance
    Vec4 cameraForward; // xyz
};
static_assert(offsetof(GpuViewConstants, directional) == 112);
static_assert(offsetof(GpuViewConstants, points) == 240);
static_assert(offsetof(GpuViewConstants, inverseViewProjection) == 752);
static_assert(offsetof(GpuViewConstants, sh) == 896);
static_assert(offsetof(GpuViewConstants, cascades) == 1040);
static_assert(offsetof(GpuViewConstants, cameraForward) == 1392);

struct GpuInstance
{
    Mat4 world;
    Mat4 normalMatrix;
};

// Bit 0: visible to the camera; bit 1 + c: casts a shadow into cascade c.
constexpr u32 kCameraPass = 1;

struct DrawItem
{
    GpuInstance instance; // computed by the (parallel) culling jobs
    const RenderResources::GpuMesh* mesh;
    const RenderResources::GpuMaterial* material;
    u32 submesh;
    u32 passes;
    f32 distanceSquared;
};

struct ForwardPush
{
    u32 instanceOffset;
    u32 flags; // bit 0: apply screen-space ambient occlusion
};

struct ShadowPush
{
    u32 instanceOffset;
    u32 cascade;
};

constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;
constexpr nvrhi::Format kHdrFormat = nvrhi::Format::RGBA16_FLOAT;

nvrhi::ShaderHandle createShader(nvrhi::IDevice* device, nvrhi::ShaderType type,
                                 std::span<const std::byte> code, const char* name)
{
    return device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName(name), code.data(),
                                code.size());
}

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

    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle fragmentShader;
    nvrhi::ShaderHandle skyboxVertexShader;
    nvrhi::ShaderHandle skyboxFragmentShader;
    nvrhi::ShaderHandle shadowVertexShader;
    nvrhi::ShaderHandle shadowFragmentShader;
    nvrhi::ShaderHandle prepassMaskedShader;
    nvrhi::GraphicsPipelineHandle skyboxPipeline;
    nvrhi::SamplerHandle clampSampler;
    nvrhi::SamplerHandle shadowSampler;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::InputLayoutHandle shadowInputLayout;
    nvrhi::BindingLayoutHandle viewLayout;
    nvrhi::BindingLayoutHandle shadowLayout;
    nvrhi::BufferHandle viewConstants;
    nvrhi::BufferHandle instanceBuffer;
    u32 instanceCapacity = 0;
    // View binding sets by environment index (0 = no sky); rebuilt when the instance buffer or the
    // shadow map changes.
    std::unordered_map<u32, nvrhi::BindingSetHandle> viewSets;
    nvrhi::BindingSetHandle shadowSet;
    // [blend][doubleSided]
    std::array<std::array<nvrhi::GraphicsPipelineHandle, 2>, 2> pipelines{};
    // Depth prepass, [alpha masked][doubleSided]
    std::array<std::array<nvrhi::GraphicsPipelineHandle, 2>, 2> prepassPipelines{};

    // Shadow map: one array layer per cascade (a 1x1 placeholder while shadows are off).
    nvrhi::TextureHandle shadowMap;
    std::array<nvrhi::FramebufferHandle, kMaxShadowCascades> shadowFramebuffers{};
    std::array<nvrhi::GraphicsPipelineHandle, 2> shadowPipelines{}; // [alpha masked]
    bool shadowResourcesDirty = true;
    ShadowCascades cascades;

    // Render-size targets: HDR colour and depth, with the forward and prepass framebuffers.
    nvrhi::TextureHandle hdr;
    nvrhi::TextureHandle depth;
    nvrhi::FramebufferHandle sceneFramebuffer;
    nvrhi::FramebufferHandle prepassFramebuffer;
    // Output framebuffers by target texture (tonemapping writes the caller's target).
    std::unordered_map<nvrhi::ITexture*, nvrhi::FramebufferHandle> framebuffers;

    PostProcessor post;
    bool postDirty = true;
    u32 frameIndex = 0;
    Mat4 previousViewProjection{1.0f};
    bool historyValid = false;

    // Per-frame scratch, kept across frames so steady state does not allocate.
    std::vector<DrawItem> items;
    std::vector<u32> cameraOrder;
    std::array<std::vector<u32>, kMaxShadowCascades> shadowOrders;
    std::vector<GpuInstance> instances;
    RenderStats stats;

    bool initialize();
    bool updateShadowResources();
    nvrhi::IFramebuffer* outputFramebufferFor(nvrhi::ITexture* color);
    void ensureInstanceCapacity(u32 count);
    void cull(World& world, const RenderView& view, JobSystem* jobs);
    i32 gatherLights(World& world, GpuViewConstants& constants, f32& angularDiameter);
    nvrhi::IBindingSet* viewSetFor(EnvironmentHandle environment);
    [[nodiscard]] const RenderResources::GpuMaterial* shadowMaterial(const DrawItem& item) const noexcept;
    void sortShadowCasters(u32 cascade);
    void drawShadows(nvrhi::ICommandList* commandList, u32 cascade, u32 instanceOffset);
};

bool SceneRenderer::Impl::initialize()
{
    vertexShader =
        createShader(device, nvrhi::ShaderType::Vertex, shaders::forwardPbrVertex(), "ForwardPbr VS");
    fragmentShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::forwardPbrFragment(), "ForwardPbr FS");
    skyboxVertexShader =
        createShader(device, nvrhi::ShaderType::Vertex, shaders::skyboxVertex(), "Skybox VS");
    skyboxFragmentShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::skyboxFragment(), "Skybox FS");
    shadowVertexShader =
        createShader(device, nvrhi::ShaderType::Vertex, shaders::shadowCasterVertex(), "Shadow caster VS");
    shadowFragmentShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::shadowCasterFragment(), "Shadow caster FS");
    prepassMaskedShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::prepassMaskedFragment(), "Prepass masked FS");
    if (!vertexShader || !fragmentShader || !skyboxVertexShader || !skyboxFragmentShader ||
        !shadowVertexShader || !shadowFragmentShader || !prepassMaskedShader)
    {
        return false;
    }
    clampSampler = device->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    shadowSampler = device->createSampler(nvrhi::SamplerDesc()
                                              .setAllFilters(true)
                                              .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
                                              .setReductionType(nvrhi::SamplerReductionType::Comparison));

    const auto attribute = [](const char* name, nvrhi::Format format, usize offset) {
        return nvrhi::VertexAttributeDesc()
            .setName(name)
            .setFormat(format)
            .setOffset(static_cast<u32>(offset))
            .setElementStride(sizeof(Vertex));
    };
    const nvrhi::VertexAttributeDesc attributes[] = {
        attribute("POSITION", nvrhi::Format::RGB32_FLOAT, offsetof(Vertex, position)),
        attribute("NORMAL", nvrhi::Format::RGB32_FLOAT, offsetof(Vertex, normal)),
        attribute("TANGENT", nvrhi::Format::RGBA32_FLOAT, offsetof(Vertex, tangent)),
        attribute("TEXCOORD", nvrhi::Format::RG32_FLOAT, offsetof(Vertex, uv)),
    };
    inputLayout =
        device->createInputLayout(attributes, static_cast<u32>(std::size(attributes)), vertexShader);
    const nvrhi::VertexAttributeDesc shadowAttributes[] = {
        attribute("POSITION", nvrhi::Format::RGB32_FLOAT, offsetof(Vertex, position)),
        attribute("TEXCOORD", nvrhi::Format::RG32_FLOAT, offsetof(Vertex, uv)),
    };
    shadowInputLayout = device->createInputLayout(
        shadowAttributes, static_cast<u32>(std::size(shadowAttributes)), shadowVertexShader);

    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.registerSpace = 0;
    layout.registerSpaceIsDescriptorSet = true;
    layout.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1), // specular cube map
        nvrhi::BindingLayoutItem::Texture_SRV(2), // BRDF table
        nvrhi::BindingLayoutItem::Texture_SRV(3), // background cube map
        nvrhi::BindingLayoutItem::Texture_SRV(4), // shadow map
        nvrhi::BindingLayoutItem::Texture_SRV(5), // screen-space ambient occlusion
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1), // shadow comparison
        // Push constants take a constant-buffer slot in NVRHI, so they cannot share b0 with the view.
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(ForwardPush)),
    };
    viewLayout = device->createBindingLayout(layout);
    nvrhi::BindingLayoutDesc shadowLayoutDesc;
    shadowLayoutDesc.visibility = nvrhi::ShaderType::All;
    shadowLayoutDesc.registerSpace = 0;
    shadowLayoutDesc.registerSpaceIsDescriptorSet = true;
    shadowLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(ShadowPush)),
    };
    shadowLayout = device->createBindingLayout(shadowLayoutDesc);
    if (!viewLayout || !shadowLayout)
    {
        return false;
    }
    viewConstants = device->createBuffer(
        nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GpuViewConstants), "View constants", 16));

    const nvrhi::FramebufferInfo info =
        nvrhi::FramebufferInfo().addColorFormat(kHdrFormat).setDepthFormat(kDepthFormat);
    const nvrhi::FramebufferInfo depthOnly = nvrhi::FramebufferInfo().setDepthFormat(kDepthFormat);
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
            // Opaque depth comes from the prepass (same vertex shader, so equal depths pass).
            pipeline.renderState.depthStencilState.setDepthTestEnable(true)
                .setDepthWriteEnable(false)
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

            // Prepass variant: index 0/1 here means opaque/alpha masked.
            nvrhi::GraphicsPipelineDesc prepass = pipeline;
            prepass.PS = blend != 0 ? prepassMaskedShader.Get() : nullptr;
            prepass.renderState.blendState = nvrhi::BlendState();
            prepass.renderState.depthStencilState.setDepthWriteEnable(true);
            prepassPipelines[blend][doubleSided] = device->createGraphicsPipeline(prepass, depthOnly);
            if (!prepassPipelines[blend][doubleSided])
            {
                return false;
            }
        }
    }

    nvrhi::GraphicsPipelineDesc skybox;
    skybox.primType = nvrhi::PrimitiveType::TriangleList;
    skybox.VS = skyboxVertexShader;
    skybox.PS = skyboxFragmentShader;
    skybox.bindingLayouts = {viewLayout};
    skybox.renderState.rasterState.setCullNone();
    // Depth 0 is both the cleared value and infinity with reversed-Z, so only uncovered pixels pass.
    skybox.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(false).setDepthFunc(
        nvrhi::ComparisonFunc::GreaterOrEqual);
    skyboxPipeline = device->createGraphicsPipeline(skybox, info);
    if (!skyboxPipeline)
    {
        return false;
    }
    ensureInstanceCapacity(1024);
    if (!post.initialize(device, desc.colorFormat))
    {
        return false;
    }
    return updateShadowResources();
}

bool SceneRenderer::Impl::updateShadowResources()
{
    if (!shadowResourcesDirty)
    {
        return true;
    }
    shadowResourcesDirty = false;
    const ShadowSettings& settings = desc.shadows;
    const u32 size = settings.enabled ? std::max(settings.resolution, 1u) : 1u;
    const u32 layers = settings.enabled ? std::clamp(settings.cascadeCount, 1u, kMaxShadowCascades) : 1u;

    nvrhi::TextureDesc mapDesc;
    mapDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
    mapDesc.width = size;
    mapDesc.height = size;
    mapDesc.arraySize = layers;
    mapDesc.format = kDepthFormat;
    mapDesc.isRenderTarget = true;
    mapDesc.debugName = "Shadow map";
    mapDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    shadowMap = device->createTexture(mapDesc);
    shadowFramebuffers = {};
    for (u32 layer = 0; layer < layers; ++layer)
    {
        shadowFramebuffers[layer] = device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
            shadowMap, nvrhi::TextureSubresourceSet(0, 1, layer, 1)));
    }

    const nvrhi::FramebufferInfo shadowInfo = nvrhi::FramebufferInfo().setDepthFormat(kDepthFormat);
    for (u32 masked = 0; masked < 2; ++masked)
    {
        nvrhi::GraphicsPipelineDesc pipeline;
        pipeline.primType = nvrhi::PrimitiveType::TriangleList;
        pipeline.inputLayout = shadowInputLayout;
        pipeline.VS = shadowVertexShader;
        pipeline.PS = masked != 0 ? shadowFragmentShader.Get() : nullptr;
        pipeline.bindingLayouts = {shadowLayout, resources->materialBindingLayout()};
        // Both faces cast: single-sided geometry must not leak light.
        pipeline.renderState.rasterState.setCullNone().setSlopeScaleDepthBias(settings.slopeBias);
        pipeline.renderState.depthStencilState.setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
        shadowPipelines[masked] = device->createGraphicsPipeline(pipeline, shadowInfo);
    }
    viewSets.clear();
    return shadowMap && shadowPipelines[0] && shadowPipelines[1];
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
    viewSets.clear();

    nvrhi::BindingSetDesc shadowSetDesc;
    shadowSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, viewConstants),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, instanceBuffer),
        nvrhi::BindingSetItem::PushConstants(1, sizeof(ShadowPush)),
    };
    shadowSet = device->createBindingSet(shadowSetDesc, shadowLayout);
}

nvrhi::IBindingSet* SceneRenderer::Impl::viewSetFor(EnvironmentHandle environment)
{
    nvrhi::BindingSetHandle& set = viewSets[environment.index];
    if (!set)
    {
        const RenderResources::GpuEnvironment& gpu = resources->environment(environment);
        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, viewConstants),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, instanceBuffer),
            nvrhi::BindingSetItem::Texture_SRV(1, gpu.specular),
            nvrhi::BindingSetItem::Texture_SRV(2, resources->brdfLut()),
            nvrhi::BindingSetItem::Texture_SRV(3, gpu.background),
            nvrhi::BindingSetItem::Texture_SRV(4, shadowMap),
            nvrhi::BindingSetItem::Texture_SRV(5, post.ambientOcclusion()),
            nvrhi::BindingSetItem::Sampler(0, clampSampler),
            nvrhi::BindingSetItem::Sampler(1, shadowSampler),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(ForwardPush)),
        };
        set = device->createBindingSet(setDesc, viewLayout);
    }
    return set;
}

nvrhi::IFramebuffer* SceneRenderer::Impl::outputFramebufferFor(nvrhi::ITexture* color)
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
        nvrhi::TextureDesc hdrDesc = depthDesc;
        hdrDesc.format = kHdrFormat;
        hdrDesc.debugName = "Scene HDR";
        hdrDesc.setClearValue(nvrhi::Color(0.0f));
        hdrDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        hdr = device->createTexture(hdrDesc);
        sceneFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(hdr).setDepthAttachment(depth));
        prepassFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(depth));
        framebuffers.clear();
        postDirty = true;
    }
    if (postDirty)
    {
        post.resize(hdr, depth, desc.post);
        postDirty = false;
        historyValid = false;
        viewSets.clear();
    }
    // Swapchain recreation produces new textures; drop stale framebuffers before the cache grows.
    if (framebuffers.size() > 8)
    {
        framebuffers.clear();
    }
    nvrhi::FramebufferHandle& framebuffer = framebuffers[color];
    if (!framebuffer)
    {
        framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(color));
    }
    return framebuffer;
}

void SceneRenderer::Impl::cull(World& world, const RenderView& view, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE_N("SceneRenderer::cull");
    auto query = world.query<const Transform, const MeshRenderer>();
    const u32 candidates = query.count();
    stats.candidates = candidates;
    if (items.size() < candidates)
    {
        items.resize(candidates);
    }
    const Frustum frustum = Frustum::fromViewProjection(view.projection * view.view);
    std::array<Frustum, kMaxShadowCascades> cascadeFrusta{};
    for (u32 c = 0; c < cascades.count; ++c)
    {
        cascadeFrusta[c] = Frustum::fromViewProjection(cascades.cascades[c].viewProjection);
    }
    const u32 cascadeCount = cascades.count;
    std::atomic<u32> count{0};
    const auto test = [&](const Transform& transform, const MeshRenderer& renderer) {
        const RenderResources::GpuMesh* mesh = resources->mesh(renderer.mesh);
        if (mesh == nullptr || renderer.submesh >= mesh->submeshes.size())
        {
            return;
        }
        const RenderResources::GpuMaterial* material = resources->material(renderer.material);
        const Mat4 matrix = transform.matrix();
        const Sphere bounds = Sphere::fromAabb(mesh->submeshes[renderer.submesh].bounds).transformed(matrix);
        u32 passes = frustum.intersects(bounds) ? kCameraPass : 0u;
        if (material->alphaMode != AlphaMode::Blend)
        {
            for (u32 c = 0; c < cascadeCount; ++c)
            {
                passes |= cascadeFrusta[c].intersects(bounds) ? (2u << c) : 0u;
            }
        }
        if (passes == 0)
        {
            return;
        }
        const Vec3 offset = bounds.center - view.position;
        items[count.fetch_add(1, std::memory_order_relaxed)] =
            DrawItem{GpuInstance{matrix, glm::transpose(glm::inverse(matrix))},
                     mesh,
                     material,
                     renderer.submesh,
                     passes,
                     glm::dot(offset, offset)};
    };
    if (jobs != nullptr)
    {
        query.parallelEach(*jobs, test);
    }
    else
    {
        query.each(test);
    }
    const u32 itemCount = count.load();

    cameraOrder.clear();
    for (std::vector<u32>& order : shadowOrders)
    {
        order.clear();
    }
    for (u32 i = 0; i < itemCount; ++i)
    {
        const u32 passes = items[i].passes;
        if ((passes & kCameraPass) != 0)
        {
            cameraOrder.push_back(i);
        }
        for (u32 c = 0; c < cascadeCount; ++c)
        {
            if ((passes & (2u << c)) != 0)
            {
                shadowOrders[c].push_back(i);
            }
        }
    }
    stats.visible = static_cast<u32>(cameraOrder.size());
}

i32 SceneRenderer::Impl::gatherLights(World& world, GpuViewConstants& constants, f32& angularDiameter)
{
    i32 shadowed = -1;
    constants.directionalCount = 0;
    world.query<const Transform, const DirectionalLight>().each(
        [&](const Transform& transform, const DirectionalLight& light) {
            if (constants.directionalCount < kMaxDirectionalLights)
            {
                if (shadowed < 0 && light.castShadows)
                {
                    shadowed = static_cast<i32>(constants.directionalCount);
                    angularDiameter = light.angularDiameter;
                }
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
    return shadowed;
}

// Opaque casters ignore their material, so they batch by mesh alone; masked casters need it.
const RenderResources::GpuMaterial* SceneRenderer::Impl::shadowMaterial(const DrawItem& item) const noexcept
{
    return item.material->alphaMode == AlphaMode::Mask ? item.material : nullptr;
}

void SceneRenderer::Impl::sortShadowCasters(u32 cascade)
{
    std::sort(shadowOrders[cascade].begin(), shadowOrders[cascade].end(), [&](u32 a, u32 b) {
        const DrawItem& x = items[a];
        const DrawItem& y = items[b];
        if (shadowMaterial(x) != shadowMaterial(y))
        {
            return shadowMaterial(x) < shadowMaterial(y);
        }
        if (x.mesh != y.mesh)
        {
            return x.mesh < y.mesh;
        }
        return x.submesh < y.submesh;
    });
}

void SceneRenderer::Impl::drawShadows(nvrhi::ICommandList* commandList, u32 cascade, u32 instanceOffset)
{
    const std::vector<u32>& order = shadowOrders[cascade];
    nvrhi::IFramebuffer* framebuffer = shadowFramebuffers[cascade];
    const nvrhi::Viewport viewport = framebuffer->getFramebufferInfo().getViewport();
    const u32 count = static_cast<u32>(order.size());
    u32 begin = 0;
    while (begin < count)
    {
        const DrawItem& first = items[order[begin]];
        const RenderResources::GpuMaterial* masked = shadowMaterial(first);
        u32 end = begin + 1;
        while (end < count)
        {
            const DrawItem& next = items[order[end]];
            if (shadowMaterial(next) != masked || next.mesh != first.mesh || next.submesh != first.submesh)
            {
                break;
            }
            ++end;
        }
        nvrhi::GraphicsState state;
        state.pipeline = shadowPipelines[masked != nullptr ? 1 : 0];
        state.framebuffer = framebuffer;
        // The material set is bound for opaque casters too: the pipeline layout includes it.
        state.bindings = {shadowSet, masked != nullptr ? masked->bindingSet : first.material->bindingSet};
        state.vertexBuffers = {
            nvrhi::VertexBufferBinding().setBuffer(first.mesh->vertexBuffer).setSlot(0).setOffset(0)};
        state.indexBuffer =
            nvrhi::IndexBufferBinding().setBuffer(first.mesh->indexBuffer).setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewportAndScissorRect(viewport);
        commandList->setGraphicsState(state);
        const ShadowPush push{instanceOffset + begin, cascade};
        commandList->setPushConstants(&push, sizeof(push));
        const Submesh& submesh = first.mesh->submeshes[first.submesh];
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(submesh.indexCount)
                                     .setInstanceCount(end - begin)
                                     .setStartIndexLocation(submesh.indexOffset));
        ++stats.shadowDrawCalls;
        begin = end;
    }
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
    if (!impl.updateShadowResources())
    {
        ILL_LOG_ERROR("Render", "SceneRenderer: cannot create shadow resources");
        return;
    }
    nvrhi::IFramebuffer* outputFramebuffer = impl.outputFramebufferFor(colorTarget);
    nvrhi::IFramebuffer* framebuffer = impl.sceneFramebuffer;
    const PostProcessSettings& post = impl.desc.post;

    // With TAA the projection moves by a sub-pixel offset every frame; culling and shadows use the
    // unjittered one.
    const Vec2 size(static_cast<f32>(colorTarget->getDesc().width),
                    static_cast<f32>(colorTarget->getDesc().height));
    const Mat4 projection = post.temporalAntiAliasing.enabled
                                ? jitterProjection(view.projection, temporalJitter(impl.frameIndex), size)
                                : view.projection;

    GpuViewConstants constants{};
    constants.viewProjection = projection * view.view;
    constants.inverseViewProjection = glm::inverse(constants.viewProjection);
    constants.cameraPosition = Vec4(view.position, 0.0f);
    constants.cameraForward = Vec4(-view.view[0][2], -view.view[1][2], -view.view[2][2], 0.0f);
    constants.effects = Vec4(post.ambientOcclusion.enabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    f32 angularDiameter = 0.0f;
    const i32 shadowedLight = impl.gatherLights(world, constants, angularDiameter);

    // Shadow cascades for the first shadow-casting directional light.
    const ShadowSettings& shadowSettings = impl.desc.shadows;
    impl.cascades = ShadowCascades{};
    if (shadowSettings.enabled && shadowedLight >= 0)
    {
        const Vec3 lightDirection = Vec3(constants.directional[shadowedLight].direction);
        impl.cascades = computeShadowCascades(view.view, view.projection, lightDirection, shadowSettings);
        for (u32 c = 0; c < impl.cascades.count; ++c)
        {
            const ShadowCascade& cascade = impl.cascades.cascades[c];
            constants.cascades[c] = GpuShadowCascade{
                cascade.viewProjection, Vec4(cascade.splitFar, cascade.worldSize, cascade.depthRange, 0.0f)};
        }
        constants.shadow =
            Vec4(static_cast<f32>(impl.cascades.count), static_cast<f32>(shadowedLight),
                 std::tan(angularDiameter * 0.5f), static_cast<f32>(std::max(shadowSettings.resolution, 1u)));
        constants.shadowBias = Vec4(shadowSettings.depthBias, shadowSettings.normalBias,
                                    std::max(shadowSettings.maxDistance, 1e-3f), 0.0f);
    }
    else
    {
        // No cascades; the light index is out of range so no light is treated as shadowed.
        constants.shadow = Vec4(0.0f, static_cast<f32>(kMaxDirectionalLights), 0.0f, 1.0f);
    }

    impl.cull(world, view, jobs);
    const u32 visibleCount = impl.stats.visible;

    // Opaque and masked first, grouped by material and mesh; then blended back to front.
    std::sort(impl.cameraOrder.begin(), impl.cameraOrder.end(), [&](u32 a, u32 b) {
        const DrawItem& x = impl.items[a];
        const DrawItem& y = impl.items[b];
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

    // Instance data: the camera's instances, then each cascade's casters.
    std::array<u32, kMaxShadowCascades> shadowOffsets{};
    u32 instanceCount = visibleCount;
    for (u32 c = 0; c < impl.cascades.count; ++c)
    {
        impl.sortShadowCasters(c);
        shadowOffsets[c] = instanceCount;
        instanceCount += static_cast<u32>(impl.shadowOrders[c].size());
        impl.stats.shadowCasters += static_cast<u32>(impl.shadowOrders[c].size());
    }
    impl.instances.resize(instanceCount);
    for (u32 i = 0; i < visibleCount; ++i)
    {
        impl.instances[i] = impl.items[impl.cameraOrder[i]].instance;
    }
    for (u32 c = 0; c < impl.cascades.count; ++c)
    {
        const std::vector<u32>& order = impl.shadowOrders[c];
        for (usize i = 0; i < order.size(); ++i)
        {
            impl.instances[shadowOffsets[c] + i] = impl.items[order[i]].instance;
        }
    }
    impl.ensureInstanceCapacity(instanceCount);

    // The first SkyLight provides image-based lighting; without one, the constant ambient light does.
    const SkyLight* skyLight = nullptr;
    Quat skyOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    world.query<const Transform, const SkyLight>().each([&](const Transform& transform, const SkyLight& sky) {
        if (skyLight == nullptr)
        {
            skyLight = &sky;
            skyOrientation = transform.rotation;
        }
    });
    const EnvironmentHandle environment = skyLight != nullptr ? skyLight->environment : EnvironmentHandle{};
    const RenderResources::GpuEnvironment& gpuEnvironment = impl.resources->environment(environment);
    const f32 skyIntensity = skyLight != nullptr ? skyLight->intensity : 0.0f;
    const ShIrradiance irradiance =
        skyLight != nullptr ? gpuEnvironment.irradiance : ShIrradiance::uniform(impl.desc.ambient);
    const f32 irradianceScale = skyLight != nullptr ? skyIntensity : 1.0f;
    for (usize i = 0; i < irradiance.coefficients.size(); ++i)
    {
        constants.sh[i] = Vec4(irradiance.coefficients[i] * irradianceScale, 0.0f);
    }
    constants.skyRotation = glm::mat4_cast(glm::inverse(glm::normalize(skyOrientation)));
    constants.sky = Vec4(skyIntensity, static_cast<f32>(gpuEnvironment.specularMips - 1), skyIntensity, 0.0f);
    const bool drawBackground = skyLight != nullptr && skyLight->background;
    nvrhi::IBindingSet* viewSet = impl.viewSetFor(environment);

    commandList->beginMarker("SceneRenderer");
    commandList->writeBuffer(impl.viewConstants, &constants, sizeof(constants));
    if (instanceCount > 0)
    {
        commandList->writeBuffer(impl.instanceBuffer, impl.instances.data(),
                                 static_cast<usize>(instanceCount) * sizeof(GpuInstance));
    }
    PostProcessor::Frame postFrame;
    postFrame.projection = projection;
    postFrame.viewProjection = constants.viewProjection;
    postFrame.previousViewProjection =
        impl.historyValid ? impl.previousViewProjection : view.projection * view.view;
    postFrame.exposure = view.exposure;
    postFrame.historyValid = impl.historyValid;
    postFrame.frameIndex = impl.frameIndex;
    impl.post.beginFrame(commandList, postFrame, post);

    commandList->clearDepthStencilTexture(impl.shadowMap, nvrhi::AllSubresources, true, 1.0f, false, 0);
    if (impl.cascades.count > 0)
    {
        commandList->beginMarker("Shadows");
        for (u32 c = 0; c < impl.cascades.count; ++c)
        {
            impl.drawShadows(commandList, c, shadowOffsets[c]);
        }
        commandList->endMarker();
    }

    const nvrhi::Viewport viewport = framebuffer->getFramebufferInfo().getViewport();
    // Runs of identical (mesh, submesh, material) form one instanced draw; blended items draw alone to
    // keep their back-to-front order. Calls `draw(begin, end, first)` for each run of the camera order.
    const auto forEachBatch = [&](u32 count, auto&& draw) {
        u32 begin = 0;
        while (begin < count)
        {
            const DrawItem& first = impl.items[impl.cameraOrder[begin]];
            const bool blend = first.material->alphaMode == AlphaMode::Blend;
            u32 end = begin + 1;
            while (!blend && end < count)
            {
                const DrawItem& next = impl.items[impl.cameraOrder[end]];
                if (next.material != first.material || next.mesh != first.mesh ||
                    next.submesh != first.submesh)
                {
                    break;
                }
                ++end;
            }
            draw(begin, end, first);
            begin = end;
        }
    };
    const auto drawBatch = [&](nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* target, u32 begin,
                               u32 end, const DrawItem& first, u32 flags) {
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = target;
        state.bindings = {viewSet, first.material->bindingSet};
        state.vertexBuffers = {
            nvrhi::VertexBufferBinding().setBuffer(first.mesh->vertexBuffer).setSlot(0).setOffset(0)};
        state.indexBuffer =
            nvrhi::IndexBufferBinding().setBuffer(first.mesh->indexBuffer).setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewportAndScissorRect(viewport);
        commandList->setGraphicsState(state);
        const ForwardPush push{begin, flags};
        commandList->setPushConstants(&push, sizeof(push));
        const Submesh& submesh = first.mesh->submeshes[first.submesh];
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(submesh.indexCount)
                                     .setInstanceCount(end - begin)
                                     .setStartIndexLocation(submesh.indexOffset));
    };
    // Opaque and masked items come first in the camera order.
    u32 opaqueCount = 0;
    while (opaqueCount < visibleCount &&
           impl.items[impl.cameraOrder[opaqueCount]].material->alphaMode != AlphaMode::Blend)
    {
        ++opaqueCount;
    }

    // Depth prepass: fills depth for ambient occlusion and removes overdraw from the shading pass.
    commandList->beginMarker("Depth prepass");
    commandList->clearDepthStencilTexture(impl.depth, nvrhi::AllSubresources, true, 0.0f, false, 0);
    forEachBatch(opaqueCount, [&](u32 begin, u32 end, const DrawItem& first) {
        const bool masked = first.material->alphaMode == AlphaMode::Mask;
        drawBatch(impl.prepassPipelines[masked ? 1 : 0][first.material->doubleSided ? 1 : 0],
                  impl.prepassFramebuffer, begin, end, first, 0);
        ++impl.stats.drawCalls;
    });
    commandList->endMarker();
    if (post.ambientOcclusion.enabled)
    {
        impl.post.computeAmbientOcclusion(commandList);
    }

    commandList->beginMarker("Shading");
    commandList->clearTextureFloat(impl.hdr, nvrhi::AllSubresources,
                                   nvrhi::Color(impl.desc.clearColor.r, impl.desc.clearColor.g,
                                                impl.desc.clearColor.b, impl.desc.clearColor.a));
    bool backgroundDone = !drawBackground;
    // Drawn after opaque geometry (so covered pixels fail the depth test) and before blended geometry.
    const auto drawBackgroundPass = [&] {
        nvrhi::GraphicsState state;
        state.pipeline = impl.skyboxPipeline;
        state.framebuffer = framebuffer;
        state.bindings = {viewSet};
        state.viewport.addViewportAndScissorRect(viewport);
        commandList->setGraphicsState(state);
        const ForwardPush unused{0, 0};
        commandList->setPushConstants(&unused, sizeof(unused));
        commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
        ++impl.stats.drawCalls;
        backgroundDone = true;
    };
    forEachBatch(visibleCount, [&](u32 begin, u32 end, const DrawItem& first) {
        const bool blend = first.material->alphaMode == AlphaMode::Blend;
        if (blend && !backgroundDone)
        {
            drawBackgroundPass();
        }
        drawBatch(impl.pipelines[blend ? 1 : 0][first.material->doubleSided ? 1 : 0], framebuffer, begin, end,
                  first, blend ? 0u : 1u);
        ++impl.stats.drawCalls;
        const Submesh& submesh = first.mesh->submeshes[first.submesh];
        impl.stats.triangles += static_cast<u64>(submesh.indexCount / 3) * (end - begin);
    });
    if (!backgroundDone)
    {
        drawBackgroundPass();
    }
    commandList->endMarker();

    commandList->beginMarker("Post-processing");
    impl.post.resolve(commandList, outputFramebuffer, post);
    commandList->endMarker();
    commandList->endMarker();

    impl.previousViewProjection = view.projection * view.view;
    impl.historyValid = post.temporalAntiAliasing.enabled;
    ++impl.frameIndex;
}

const RenderStats& SceneRenderer::stats() const noexcept
{
    return m_impl->stats;
}

const ShadowCascades& SceneRenderer::shadowCascades() const noexcept
{
    return m_impl->cascades;
}

const ShadowSettings& SceneRenderer::shadowSettings() const noexcept
{
    return m_impl->desc.shadows;
}

void SceneRenderer::setShadowSettings(const ShadowSettings& settings) noexcept
{
    m_impl->desc.shadows = settings;
    m_impl->shadowResourcesDirty = true;
}

const PostProcessSettings& SceneRenderer::postProcessSettings() const noexcept
{
    return m_impl->desc.post;
}

void SceneRenderer::setPostProcessSettings(const PostProcessSettings& settings) noexcept
{
    m_impl->desc.post = settings;
    m_impl->postDirty = true;
}

void SceneRenderer::resetTemporalHistory() noexcept
{
    m_impl->historyValid = false;
}

void SceneRenderer::setAmbient(Vec3 ambient) noexcept
{
    m_impl->desc.ambient = ambient;
}

} // namespace illusion
