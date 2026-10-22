#include "GpuCulling.hpp"
#include "PostProcessor.hpp"
#include "RenderResourcesImpl.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
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
    Mat4 unjitteredViewProjection;
    Mat4 previousViewProjection; // unjittered, previous frame
};
static_assert(offsetof(GpuViewConstants, directional) == 112);
static_assert(offsetof(GpuViewConstants, points) == 240);
static_assert(offsetof(GpuViewConstants, inverseViewProjection) == 752);
static_assert(offsetof(GpuViewConstants, sh) == 896);
static_assert(offsetof(GpuViewConstants, cascades) == 1040);
static_assert(offsetof(GpuViewConstants, cameraForward) == 1392);
static_assert(offsetof(GpuViewConstants, previousViewProjection) == 1472);

struct GpuInstance
{
    Mat4 world;
    Mat4 normalMatrix;
    Mat4 previousWorld;
    u32 extra[4]; // [0]: material table index
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
    Sphere bounds; // world space
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
constexpr nvrhi::Format kMotionFormat = nvrhi::Format::RG16_FLOAT;

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
    nvrhi::ShaderHandle prepassShader;
    nvrhi::ShaderHandle prepassMaskedShader;
    // Bindless variants (material table + texture descriptor array, multi-draw indirect).
    bool multiDrawIndirect = false;
    nvrhi::ShaderHandle bindlessVertexShader;
    nvrhi::ShaderHandle bindlessFragmentShader;
    nvrhi::ShaderHandle bindlessPrepassShader;
    nvrhi::ShaderHandle bindlessPrepassMaskedShader;
    std::array<nvrhi::GraphicsPipelineHandle, 2> bindlessPipelines{}; // [doubleSided]
    std::array<std::array<nvrhi::GraphicsPipelineHandle, 2>, 2>
        bindlessPrepassPipelines{}; // [masked][doubleSided]
    u32 resourceGeneration = ~0u;
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
    nvrhi::TextureHandle motion; // screen-space motion vectors written by the prepass
    nvrhi::FramebufferHandle sceneFramebuffer;
    nvrhi::FramebufferHandle prepassFramebuffer;
    // Output framebuffers by target texture (tonemapping writes the caller's target).
    std::unordered_map<nvrhi::ITexture*, nvrhi::FramebufferHandle> framebuffers;

    PostProcessor post;
    bool postDirty = true;
    GpuCulling culling;
    std::vector<GpuCulling::Candidate> cullCandidates;
    std::vector<GpuCulling::Batch> cullBatches;
    std::vector<u32> batchStarts; // first camera-order index of each opaque batch
    std::vector<u32> blendedSlots;
    u32 frameIndex = 0;
    Mat4 previousViewProjection{1.0f};
    bool historyValid = false;       // TAA history holds a valid image
    bool cameraHistoryValid = false; // previousViewProjection holds the last frame's camera

    // Per-frame scratch, kept across frames so steady state does not allocate.
    std::vector<DrawItem> items;
    std::vector<u32> cameraOrder;
    std::array<std::vector<u32>, kMaxShadowCascades> shadowOrders;
    std::vector<GpuInstance> instances;
    // Entities seen for the first time; they receive a MotionHistory after culling.
    std::vector<Entity> newMotionEntities;
    std::vector<Mat4> newMotionMatrices;
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
    prepassShader = createShader(device, nvrhi::ShaderType::Pixel, shaders::prepassFragment(), "Prepass FS");
    prepassMaskedShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::prepassMaskedFragment(), "Prepass masked FS");
    if (!vertexShader || !fragmentShader || !skyboxVertexShader || !skyboxFragmentShader ||
        !shadowVertexShader || !shadowFragmentShader || !prepassShader || !prepassMaskedShader)
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
        nvrhi::BindingLayoutItem::Texture_SRV(1),          // specular cube map
        nvrhi::BindingLayoutItem::Texture_SRV(2),          // BRDF table
        nvrhi::BindingLayoutItem::Texture_SRV(3),          // background cube map
        nvrhi::BindingLayoutItem::Texture_SRV(4),          // shadow map
        nvrhi::BindingLayoutItem::Texture_SRV(5),          // screen-space ambient occlusion
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6), // draw list
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7), // material table
        nvrhi::BindingLayoutItem::Sampler(2).setSize(9),   // material samplers
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
    const nvrhi::FramebufferInfo prepassInfo =
        nvrhi::FramebufferInfo().addColorFormat(kMotionFormat).setDepthFormat(kDepthFormat);
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
            prepass.PS = blend != 0 ? prepassMaskedShader.Get() : prepassShader.Get();
            prepass.renderState.blendState = nvrhi::BlendState();
            prepass.renderState.depthStencilState.setDepthWriteEnable(true);
            prepassPipelines[blend][doubleSided] = device->createGraphicsPipeline(prepass, prepassInfo);
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
    if (resources->bindless() && multiDrawIndirect)
    {
        bindlessVertexShader = createShader(device, nvrhi::ShaderType::Vertex,
                                            shaders::forwardPbrBindlessVertex(), "ForwardPbr bindless VS");
        bindlessFragmentShader =
            createShader(device, nvrhi::ShaderType::Pixel, shaders::forwardPbrBindlessFragment(),
                         "ForwardPbr bindless FS");
        bindlessPrepassShader = createShader(device, nvrhi::ShaderType::Pixel,
                                             shaders::prepassBindlessFragment(), "Prepass bindless FS");
        bindlessPrepassMaskedShader =
            createShader(device, nvrhi::ShaderType::Pixel, shaders::prepassMaskedBindlessFragment(),
                         "Prepass masked bindless FS");
        for (u32 doubleSided = 0; doubleSided < 2; ++doubleSided)
        {
            nvrhi::GraphicsPipelineDesc pipeline;
            pipeline.primType = nvrhi::PrimitiveType::TriangleList;
            pipeline.inputLayout = inputLayout;
            pipeline.VS = bindlessVertexShader;
            pipeline.PS = bindlessFragmentShader;
            pipeline.bindingLayouts = {viewLayout, resources->bindlessLayout()};
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
                .setDepthWriteEnable(false)
                .setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
            bindlessPipelines[doubleSided] = device->createGraphicsPipeline(pipeline, info);
            for (u32 masked = 0; masked < 2; ++masked)
            {
                nvrhi::GraphicsPipelineDesc prepass = pipeline;
                prepass.PS = masked != 0 ? bindlessPrepassMaskedShader.Get() : bindlessPrepassShader.Get();
                prepass.renderState.depthStencilState.setDepthWriteEnable(true);
                bindlessPrepassPipelines[masked][doubleSided] =
                    device->createGraphicsPipeline(prepass, prepassInfo);
            }
        }
    }
    ensureInstanceCapacity(1024);
    if (!post.initialize(device, desc.colorFormat) || !culling.initialize(device))
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
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, culling.drawList()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(7, resources->materialTable()),
            nvrhi::BindingSetItem::Sampler(0, clampSampler),
            nvrhi::BindingSetItem::Sampler(1, shadowSampler),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(ForwardPush)),
        };
        for (u32 i = 0; i < 9; ++i)
        {
            setDesc.bindings.push_back(
                nvrhi::BindingSetItem::Sampler(2, resources->materialSampler(i)).setArrayElement(i));
        }
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
        nvrhi::TextureDesc motionDesc = hdrDesc;
        motionDesc.format = kMotionFormat;
        motionDesc.debugName = "Motion vectors";
        motion = device->createTexture(motionDesc);
        prepassFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(motion).setDepthAttachment(depth));
        framebuffers.clear();
        postDirty = true;
        culling.resize(depth);
    }
    if (postDirty)
    {
        post.resize(hdr, depth, motion, desc.post);
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
    const u32 candidates = world.query<const Transform, const MeshRenderer>().without<Inactive>().count();
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
    // Tests one entity; `previous` is its world matrix from the last frame (equal to `matrix` when new).
    const auto test = [&](const Mat4& matrix, const Mat4& previous, const MeshRenderer& renderer) {
        const RenderResources::GpuMesh* mesh = resources->mesh(renderer.mesh);
        if (mesh == nullptr || renderer.submesh >= mesh->submeshes.size())
        {
            return;
        }
        const RenderResources::GpuMaterial* material = resources->material(renderer.material);
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
        items[count.fetch_add(1, std::memory_order_relaxed)] = DrawItem{
            GpuInstance{matrix, glm::transpose(glm::inverse(matrix)), previous, {material->index, 0, 0, 0}},
            mesh,
            material,
            renderer.submesh,
            passes,
            glm::dot(offset, offset),
            bounds};
    };
    const auto tracked = [&](const Transform& transform, const MeshRenderer& renderer,
                             MotionHistory& history) {
        const Mat4 matrix = transform.matrix();
        test(matrix, history.previousWorld, renderer);
        history.previousWorld = matrix;
    };
    // Entities without history are recorded and receive one after iteration (no structural changes
    // while a query runs).
    auto untrackedQuery =
        world.query<const Transform, const MeshRenderer>().without<MotionHistory, Inactive>();
    const u32 untrackedCount = untrackedQuery.count();
    newMotionEntities.resize(untrackedCount);
    newMotionMatrices.resize(untrackedCount);
    std::atomic<u32> newCount{0};
    const auto untracked = [&](Entity entity, const Transform& transform, const MeshRenderer& renderer) {
        const Mat4 matrix = transform.matrix();
        test(matrix, matrix, renderer);
        const u32 slot = newCount.fetch_add(1, std::memory_order_relaxed);
        newMotionEntities[slot] = entity;
        newMotionMatrices[slot] = matrix;
    };
    auto trackedQuery = world.query<const Transform, const MeshRenderer, MotionHistory>().without<Inactive>();
    if (jobs != nullptr)
    {
        trackedQuery.parallelEach(*jobs, tracked);
        untrackedQuery.parallelEach(*jobs, untracked);
    }
    else
    {
        trackedQuery.each(tracked);
        untrackedQuery.each(untracked);
    }
    for (u32 i = 0; i < newCount.load(); ++i)
    {
        world.emplace<MotionHistory>(newMotionEntities[i], MotionHistory{newMotionMatrices[i]});
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
    world.query<const Transform, const DirectionalLight>().without<Inactive>().each(
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
    world.query<const Transform, const PointLight>().without<Inactive>().each(
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
            nvrhi::VertexBufferBinding().setBuffer(resources->vertexPool()).setSlot(0).setOffset(0)};
        state.indexBuffer =
            nvrhi::IndexBufferBinding().setBuffer(resources->indexPool()).setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewportAndScissorRect(viewport);
        commandList->setGraphicsState(state);
        const ShadowPush push{instanceOffset + begin, cascade};
        commandList->setPushConstants(&push, sizeof(push));
        const Submesh& submesh = first.mesh->submeshes[first.submesh];
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(submesh.indexCount)
                                     .setInstanceCount(end - begin)
                                     .setStartIndexLocation(first.mesh->indexOffset + submesh.indexOffset)
                                     .setStartVertexLocation(first.mesh->vertexOffset));
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
    impl.multiDrawIndirect = device.gpu().multiDrawIndirect;
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
    constants.unjitteredViewProjection = view.projection * view.view;
    constants.previousViewProjection =
        impl.cameraHistoryValid ? impl.previousViewProjection : constants.unjitteredViewProjection;
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

    // Bindless rendering draws each pipeline's batches with one multi-draw; materials vary per instance.
    const bool bindless = impl.desc.bindless && impl.resources->bindless() && impl.multiDrawIndirect;
    const auto pipelineKey = [](const DrawItem& item) {
        return (item.material->alphaMode == AlphaMode::Mask ? 2u : 0u) +
               (item.material->doubleSided ? 1u : 0u);
    };
    // Opaque and masked first, grouped by material (or pipeline) and mesh; then blended back to front.
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
        if (bindless && pipelineKey(x) != pipelineKey(y))
        {
            return pipelineKey(x) < pipelineKey(y);
        }
        if (!bindless && x.material != y.material)
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
    world.query<const Transform, const SkyLight>().without<Inactive>().each(
        [&](const Transform& transform, const SkyLight& sky) {
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

    // Opaque and masked items come first in the camera order; they form batches of identical
    // (mesh, submesh, material) that the GPU culls and draws indirectly.
    u32 opaqueCount = 0;
    while (opaqueCount < visibleCount &&
           impl.items[impl.cameraOrder[opaqueCount]].material->alphaMode != AlphaMode::Blend)
    {
        ++opaqueCount;
    }
    impl.cullCandidates.resize(opaqueCount);
    impl.cullBatches.clear();
    impl.batchStarts.clear();
    for (u32 begin = 0; begin < opaqueCount;)
    {
        const DrawItem& first = impl.items[impl.cameraOrder[begin]];
        u32 end = begin + 1;
        while (end < opaqueCount)
        {
            const DrawItem& next = impl.items[impl.cameraOrder[end]];
            const bool sameState =
                bindless ? pipelineKey(next) == pipelineKey(first) : next.material == first.material;
            if (!sameState || next.mesh != first.mesh || next.submesh != first.submesh)
            {
                break;
            }
            ++end;
        }
        const auto batch = static_cast<u32>(impl.cullBatches.size());
        const Submesh& submesh = first.mesh->submeshes[first.submesh];
        impl.cullBatches.push_back(GpuCulling::Batch{submesh.indexCount,
                                                     first.mesh->indexOffset + submesh.indexOffset,
                                                     first.mesh->vertexOffset, begin});
        impl.batchStarts.push_back(begin);
        for (u32 i = begin; i < end; ++i)
        {
            const Sphere& bounds = impl.items[impl.cameraOrder[i]].bounds;
            impl.cullCandidates[i] =
                GpuCulling::Candidate{Vec4(bounds.center, bounds.radius), batch, begin, {}};
        }
        begin = end;
    }
    // Blended items are drawn by the CPU; their draw-list slots follow both culling regions.
    impl.blendedSlots.resize(visibleCount - opaqueCount);
    for (u32 i = opaqueCount; i < visibleCount; ++i)
    {
        impl.blendedSlots[i - opaqueCount] = i;
    }
    GpuCulling::Frame cullFrame;
    cullFrame.viewProjection = constants.unjitteredViewProjection;
    cullFrame.previousViewProjection = constants.previousViewProjection;
    cullFrame.occlusion = impl.desc.occlusionCulling;
    cullFrame.firstInstanceOffsets = bindless;
    impl.culling.begin(commandList, impl.cullCandidates, impl.cullBatches, visibleCount - opaqueCount,
                       cullFrame);
    const u32 blendedSlotBase = 2 * opaqueCount;
    impl.culling.writeDrawList(commandList, blendedSlotBase, impl.blendedSlots);
    if (impl.culling.takeBuffersChanged() || impl.resourceGeneration != impl.resources->generation())
    {
        impl.viewSets.clear();
        impl.resourceGeneration = impl.resources->generation();
    }
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
    const auto setDrawState = [&](nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* target,
                                  const DrawItem& first, bool indirect, u32 slot, u32 flags) {
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = target;
        if (bindless && indirect)
        {
            state.bindings = {viewSet, impl.resources->bindlessTextures()};
        }
        else
        {
            state.bindings = {viewSet, first.material->bindingSet};
        }
        state.vertexBuffers = {
            nvrhi::VertexBufferBinding().setBuffer(impl.resources->vertexPool()).setSlot(0).setOffset(0)};
        state.indexBuffer = nvrhi::IndexBufferBinding()
                                .setBuffer(impl.resources->indexPool())
                                .setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewportAndScissorRect(viewport);
        if (indirect)
        {
            state.indirectParams = impl.culling.arguments();
        }
        commandList->setGraphicsState(state);
        const ForwardPush push{slot, flags};
        commandList->setPushConstants(&push, sizeof(push));
    };
    // Opaque batches are drawn in groups: with bindless rendering a group is every consecutive batch with
    // the same pipeline, drawn by one multi-draw indirect call; otherwise each batch is its own group.
    const auto batchFirst = [&](u32 batch) -> const DrawItem& {
        return impl.items[impl.cameraOrder[impl.batchStarts[batch]]];
    };
    const auto batchCount = static_cast<u32>(impl.cullBatches.size());
    const auto forEachGroup = [&](auto&& draw) {
        for (u32 first = 0; first < batchCount;)
        {
            u32 last = first + 1;
            while (bindless && last < batchCount &&
                   pipelineKey(batchFirst(last)) == pipelineKey(batchFirst(first)))
            {
                ++last;
            }
            draw(first, last - first, batchFirst(first));
            first = last;
        }
    };
    const auto drawGroup = [&](nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* target,
                               u32 firstBatch, u32 count, u32 phase, u32 flags) {
        const u32 start = impl.batchStarts[firstBatch];
        // Single draws carry their draw-list offset in a push constant; multi-draws in each draw's
        // first-instance field.
        const u32 slot = bindless ? 0u : (phase == 1 ? 0u : impl.culling.phase2Offset()) + start;
        setDrawState(pipeline, target, batchFirst(firstBatch), true, slot, flags);
        commandList->drawIndexedIndirect(impl.culling.argumentOffset(firstBatch, phase), count);
        ++impl.stats.drawCalls;
    };
    const auto prepassPipeline = [&](const DrawItem& first) {
        const u32 masked = first.material->alphaMode == AlphaMode::Mask ? 1 : 0;
        const u32 doubleSided = first.material->doubleSided ? 1 : 0;
        return bindless ? impl.bindlessPrepassPipelines[masked][doubleSided].Get()
                        : impl.prepassPipelines[masked][doubleSided].Get();
    };
    const auto shadingPipeline = [&](const DrawItem& first) {
        const u32 doubleSided = first.material->doubleSided ? 1 : 0;
        return bindless ? impl.bindlessPipelines[doubleSided].Get() : impl.pipelines[0][doubleSided].Get();
    };

    // Depth prepass in two culling phases: draw what last frame's depth does not hide, build a Hi-Z of
    // that, then draw what it does not hide among the rest. The final Hi-Z serves the next frame.
    commandList->beginMarker("Depth prepass");
    commandList->clearDepthStencilTexture(impl.depth, nvrhi::AllSubresources, true, 0.0f, false, 0);
    commandList->clearTextureFloat(impl.motion, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    impl.culling.cull(commandList, 1);
    forEachGroup([&](u32 first, u32 count, const DrawItem& item) {
        drawGroup(prepassPipeline(item), impl.prepassFramebuffer, first, count, 1, 0);
    });
    if (impl.desc.occlusionCulling)
    {
        impl.culling.buildHiZ(commandList);
        impl.culling.cull(commandList, 2);
        forEachGroup([&](u32 first, u32 count, const DrawItem& item) {
            drawGroup(prepassPipeline(item), impl.prepassFramebuffer, first, count, 2, 0);
        });
        impl.culling.buildHiZ(commandList);
    }
    commandList->endMarker();
    if (post.ambientOcclusion.enabled)
    {
        impl.post.computeAmbientOcclusion(commandList);
    }

    commandList->beginMarker("Shading");
    commandList->clearTextureFloat(impl.hdr, nvrhi::AllSubresources,
                                   nvrhi::Color(impl.desc.clearColor.r, impl.desc.clearColor.g,
                                                impl.desc.clearColor.b, impl.desc.clearColor.a));
    forEachGroup([&](u32 first, u32 count, const DrawItem& item) {
        drawGroup(shadingPipeline(item), framebuffer, first, count, 1, 1);
        if (impl.desc.occlusionCulling)
        {
            drawGroup(shadingPipeline(item), framebuffer, first, count, 2, 1);
        }
    });
    for (u32 batch = 0; batch < batchCount; ++batch)
    {
        const u32 instances =
            (batch + 1 < batchCount ? impl.batchStarts[batch + 1] : opaqueCount) - impl.batchStarts[batch];
        impl.stats.triangles += static_cast<u64>(impl.cullBatches[batch].indexCount / 3) * instances;
    }
    // The sky after opaque geometry (covered pixels fail the depth test) and before blended geometry.
    if (drawBackground)
    {
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
    }
    // Blended items one by one, back to front.
    for (u32 i = opaqueCount; i < visibleCount; ++i)
    {
        const DrawItem& item = impl.items[impl.cameraOrder[i]];
        setDrawState(impl.pipelines[1][item.material->doubleSided ? 1 : 0], framebuffer, item, false,
                     blendedSlotBase + (i - opaqueCount), 0);
        const Submesh& submesh = item.mesh->submeshes[item.submesh];
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(submesh.indexCount)
                                     .setStartIndexLocation(item.mesh->indexOffset + submesh.indexOffset)
                                     .setStartVertexLocation(item.mesh->vertexOffset));
        ++impl.stats.drawCalls;
        impl.stats.triangles += submesh.indexCount / 3;
    }
    commandList->endMarker();
    impl.culling.finish(commandList);

    commandList->beginMarker("Post-processing");
    impl.post.resolve(commandList, outputFramebuffer, post);
    commandList->endMarker();
    commandList->endMarker();

    impl.previousViewProjection = view.projection * view.view;
    impl.historyValid = post.temporalAntiAliasing.enabled;
    impl.cameraHistoryValid = true;
    ++impl.frameIndex;
}

const RenderStats& SceneRenderer::stats() const noexcept
{
    return m_impl->stats;
}

nvrhi::ITexture* SceneRenderer::motionVectors() const noexcept
{
    return m_impl->motion;
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

bool SceneRenderer::usesBindless() const noexcept
{
    return m_impl->desc.bindless && m_impl->resources->bindless() && m_impl->multiDrawIndirect;
}

bool SceneRenderer::occlusionCulling() const noexcept
{
    return m_impl->desc.occlusionCulling;
}

void SceneRenderer::setOcclusionCulling(bool enabled) noexcept
{
    m_impl->desc.occlusionCulling = enabled;
    m_impl->culling.invalidateHistory();
}

u32 SceneRenderer::readGpuDrawnInstances() const
{
    return m_impl->culling.readDrawnInstances();
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
