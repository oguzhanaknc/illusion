#include "GpuCulling.hpp"
#include "ParticleRenderer.hpp"
#include "PostProcessor.hpp"
#include "RenderResourcesImpl.hpp"
#include "VolumetricFogRenderer.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/BuiltinShaders.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/SceneRenderer.hpp>
#include <illusion/render/SkyAtmosphere.hpp>

#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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

struct GpuPointShadow
{
    Mat4 faces[kPointShadowFaces]; // world -> face clip (pointShadowFaceMatrix())
    Vec4 positionNear;             // xyz: light position, w: near distance
    Vec4 params;                   // x: depth bias, y: normal bias (texels), z: source radius, w: resolution
};
static_assert(sizeof(GpuPointShadow) == 416);

struct GpuViewConstants
{
    Mat4 viewProjection;
    Vec4 cameraPosition; // xyz
    Vec4 effects;        // x: AO enabled, y: debug view, z: orthographic projection
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
    Vec4 wind;                   // xyz: normalized direction, w: speed (m/s; 0 without a Wind)
    Vec4 windGusts;              // x: gust strength, y: gust frequency, z: turbulence, w: time
    Vec4 windHistory;            // x: time of the previous frame
    GpuAtmosphere atmosphere;    // the SkyAtmosphere, when atmosphereLook.z is 1
    Vec4 atmosphereLook; // x: star brightness, y: seconds, z: 1 with a SkyAtmosphere, w: moon lit fraction
    Vec4 fog;            // x: 1 with VolumetricFog, y: near, z: slices / log(far / near), w: slices
    Vec4 fogScreen;      // xy: 1 / render size, zw: render size
    GpuPointShadow pointShadows[kMaxShadowedPointLights]; // the shadowed point lights (ADR 0051)
};
static_assert(offsetof(GpuViewConstants, directional) == 112);
static_assert(offsetof(GpuViewConstants, points) == 240);
static_assert(offsetof(GpuViewConstants, inverseViewProjection) == 752);
static_assert(offsetof(GpuViewConstants, sh) == 896);
static_assert(offsetof(GpuViewConstants, cascades) == 1040);
static_assert(offsetof(GpuViewConstants, cameraForward) == 1392);
static_assert(offsetof(GpuViewConstants, previousViewProjection) == 1472);
static_assert(offsetof(GpuViewConstants, wind) == 1536);
static_assert(offsetof(GpuViewConstants, pointShadows) % 16 == 0);

struct GpuInstance
{
    Mat4 world;
    Mat4 normalMatrix;
    Mat4 previousWorld;
    u32 extra[4]; // [0]: material table index
};

// Shadow views: the cascades, then the cube faces of each shadowed point light.
constexpr u32 kPointShadowViews = kMaxShadowedPointLights * kPointShadowFaces;
constexpr u32 kShadowViews = kMaxShadowCascades + kPointShadowViews;

// Distance below which every object draws at full detail (the camera is at or inside its bounds).
constexpr f32 kLodMinDistance = 1.0e-3f;

// GpuInstance::extra[3] (layout matches kLodFade* in Common.slang): the level of detail in bits 20-23 (for
// the debug view) and, while an instance cross-fades between two levels, the dither of this copy: bit 17
// set, bits 0-15 the fade threshold, bit 16 set for the outgoing level (it keeps the pixels whose noise is
// at or above the threshold; the incoming level keeps the others, so every pixel is covered once).
constexpr u32 kLodFadeActive = 1u << 17;
constexpr u32 kLodFadeOutgoing = 1u << 16;
constexpr u32 kLodFadeLevelShift = 20;

// The extra[3] code of one copy of an instance at level @p lod; @p fade in [0, 1) while cross-fading.
u32 lodFadeCode(u32 lod, f32 fade, bool fading, bool outgoing) noexcept
{
    u32 code = lod << kLodFadeLevelShift;
    if (fading)
    {
        code |= kLodFadeActive | static_cast<u32>(std::clamp(fade, 0.0f, 1.0f) * 65535.0f + 0.5f);
        code |= outgoing ? kLodFadeOutgoing : 0u;
    }
    return code;
}

// Bit 0: visible to the camera; bit 1 + v: casts a shadow into shadow view v.
constexpr u32 kCameraPass = 1;
static_assert(kShadowViews + 1 <= 32);

struct DrawItem
{
    GpuInstance instance; // computed by the (parallel) culling jobs
    const RenderResources::GpuMesh* mesh;
    const RenderResources::GpuMaterial* material;
    u32 submesh;
    u32 lod; // level of detail (0: full detail), chosen from the camera for every pass
    u32 passes;
    // Drawn with dithering while it cross-fades between two levels (instance.extra[3], see kLodFadeActive).
    [[nodiscard]] bool fading() const noexcept { return (instance.extra[3] & kLodFadeActive) != 0; }
    f32 distanceSquared;
    Sphere bounds; // world space

    // The index range this item draws.
    [[nodiscard]] SubmeshLod range() const noexcept { return mesh->submeshes[submesh].level(lod); }
};

struct ForwardPush
{
    u32 instanceOffset;
    u32 flags; // bit 0: apply screen-space ambient occlusion; bit 1: apply the volumetric fog
};

constexpr u32 kForwardAmbientOcclusion = 1;
constexpr u32 kForwardFog = 2;

struct ShadowPush
{
    u32 instanceOffset;
    u32 cascade; // shadow view
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
    view.projection =
        camera.projection == CameraProjection::Orthographic
            ? orthographicReversedZ(-camera.orthoHeight * aspect * 0.5f, camera.orthoHeight * aspect * 0.5f,
                                    -camera.orthoHeight * 0.5f, camera.orthoHeight * 0.5f, camera.nearPlane,
                                    std::max(camera.farPlane, camera.nearPlane + 0.001f))
            : perspectiveReversedZ(camera.fovY, aspect, camera.nearPlane);
    view.position = transform.position;
    view.exposure = camera.exposure;
    return view;
}

Entity findMainCamera(World& world)
{
    Entity best;
    i32 priority = 0;
    world.query<const Transform, const Camera>().without<Inactive>().each(
        [&](Entity entity, const Transform&, const Camera& camera) {
            if (best.isNull() || camera.priority > priority ||
                (camera.priority == priority && entity.index < best.index))
            {
                best = entity;
                priority = camera.priority;
            }
        });
    return best;
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
    nvrhi::ShaderHandle entityIdShader;
    nvrhi::ShaderHandle wireframeShader;
    nvrhi::ShaderHandle bindlessWireframeShader;
    std::array<nvrhi::GraphicsPipelineHandle, 2> entityIdPipelines{};
    std::array<nvrhi::GraphicsPipelineHandle, 2> wireframePipelines{};
    std::array<nvrhi::GraphicsPipelineHandle, 2> bindlessWireframePipelines{};
    bool wireframeSupported = false;
    bool wireframe = false;
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
    // Point light shadows (ADR 0051): six layers (cube faces) per slot, reversed depth (a 1x1 placeholder
    // while point shadows are off).
    nvrhi::TextureHandle pointShadowMap;
    std::array<nvrhi::FramebufferHandle, kPointShadowViews> pointShadowFramebuffers{};
    std::array<nvrhi::GraphicsPipelineHandle, 2> pointShadowPipelines{}; // [alpha masked]
    u32 pointShadowSlots = 0;                                            // slots the map has room for
    bool pointShadowsCleared = false; // the new map has been cleared once (it is bound before it is drawn)
    // Point lights with castShadows gathered this frame (at most kMaxPointLights), and the chosen ones.
    struct PointShadowCandidate
    {
        u32 light; // index in GpuViewConstants::points
        f32 priority;
        Vec3 position;
        f32 range;
        f32 nearPlane;
        f32 bias;
        f32 sourceRadius;
    };
    std::array<PointShadowCandidate, kMaxPointLights> pointCandidates{};
    u32 pointCandidateCount = 0;
    std::array<PointShadowCandidate, kMaxShadowedPointLights> pointShadows{};
    u32 pointShadowCount = 0;
    std::array<Frustum, kPointShadowViews> pointFaceFrusta{};

    // Render-size targets: HDR colour and depth, with the forward and prepass framebuffers.
    nvrhi::TextureHandle hdr;
    nvrhi::TextureHandle depth;
    nvrhi::TextureHandle ids;
    nvrhi::TextureHandle idDepth;
    nvrhi::FramebufferHandle idFramebuffer;
    nvrhi::TextureHandle motion; // screen-space motion vectors written by the prepass
    nvrhi::FramebufferHandle sceneFramebuffer;
    nvrhi::FramebufferHandle prepassFramebuffer;
    // Output framebuffers by target texture (tonemapping writes the caller's target).
    std::unordered_map<nvrhi::ITexture*, nvrhi::FramebufferHandle> framebuffers;
    // Render-size targets replaced by a resize, kept until the GPU has finished the frames that used them:
    // clears do not keep their textures alive in NVRHI's command lists.
    struct RetiredTargets
    {
        std::array<nvrhi::TextureHandle, 5> textures;
        nvrhi::EventQueryHandle done;
    };
    std::vector<RetiredTargets> retired;

    PostProcessor post;
    bool postDirty = true;
    GpuCulling culling;
    ParticleRenderer particles;
    VolumetricFogRenderer fog;
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
    std::array<std::vector<u32>, kShadowViews> shadowOrders; // casters per shadow view
    std::vector<GpuInstance> instances;
    // Per-view, dense by Entity index. No World mutation while drawing (ADR 0040).
    // Renderer-owned history keeps simultaneous views independent (ADR 0040).
    struct MotionEntry
    {
        Mat4 matrix{1.0f};
        u64 frame = 0;
        u32 generation = 0;
        // Level of detail drawn last frame; while `fade` < 1 it cross-fades in from `previousLod`.
        u32 lod = 0;
        u32 previousLod = 0;
        f32 fade = 1.0f;
    };
    std::vector<MotionEntry> motionHistory;
    u64 motionFrame = 0;
    const World* motionWorld = nullptr;
    RenderViewTargets targets;
    RenderDebugView debugView = RenderDebugView::Lit;
    RenderStats stats;
    struct Timers
    {
        std::array<nvrhi::TimerQueryHandle, 5> passes;
        u64 sequence = 0;
    };
    std::array<Timers, 3> timerRing{};
    RenderGpuTimings timings;
    u64 timerSequence = 0;
    u64 resolvedTimer = 0;
    void pollTimers();

    bool initialize();
    bool updateShadowResources();
    nvrhi::IFramebuffer* outputFramebufferFor(nvrhi::ITexture* color);
    void ensureInstanceCapacity(u32 count);
    void cull(World& world, const RenderView& view, f32 viewportHeight, JobSystem* jobs);
    i32 gatherLights(World& world, GpuViewConstants& constants, f32& angularDiameter,
                     const SkyAtmosphere* atmosphere, const SkyAtmosphereState& sky);
    // The environment a SkyAtmosphere draws into, made on first use and redrawn when the sky changes.
    EnvironmentHandle atmosphereEnvironment;
    u32 atmosphereResolution = 0;
    bool atmosphereDrawn = false;
    AtmosphereDesc atmosphereLast;
    void updateAtmosphere(nvrhi::ICommandList* commandList, const SkyAtmosphere& atmosphere,
                          const SkyAtmosphereState& sky);
    ~Impl()
    {
        if (resources != nullptr && atmosphereEnvironment.index != 0)
        {
            resources->releaseEnvironment(atmosphereEnvironment);
        }
    }
    nvrhi::IBindingSet* viewSetFor(EnvironmentHandle environment);
    [[nodiscard]] const RenderResources::GpuMaterial* shadowMaterial(const DrawItem& item) const noexcept;
    void selectPointShadows(const RenderView& view, GpuViewConstants& constants);
    [[nodiscard]] bool shadowViewActive(u32 shadowView) const noexcept
    {
        return shadowView < kMaxShadowCascades
                   ? shadowView < cascades.count
                   : shadowView - kMaxShadowCascades < pointShadowCount * kPointShadowFaces;
    }
    void sortShadowCasters(u32 shadowView);
    void drawShadows(nvrhi::ICommandList* commandList, u32 shadowView, u32 instanceOffset);
};

void SceneRenderer::Impl::pollTimers()
{
    if (!timings.available)
    {
        return;
    }
    for (Timers& frame : timerRing)
    {
        if (frame.sequence == 0)
        {
            continue;
        }
        bool ready = true;
        for (auto& query : frame.passes)
        {
            ready = device->pollTimerQuery(query) && ready;
        }
        if (!ready)
        {
            continue;
        }
        if (frame.sequence > resolvedTimer)
        {
            timings.valid = true;
            timings.shadows = device->getTimerQueryTime(frame.passes[0]) * 1000.0f;
            timings.depth = device->getTimerQueryTime(frame.passes[1]) * 1000.0f;
            timings.ambientOcclusion = device->getTimerQueryTime(frame.passes[2]) * 1000.0f;
            timings.shading = device->getTimerQueryTime(frame.passes[3]) * 1000.0f;
            timings.post = device->getTimerQueryTime(frame.passes[4]) * 1000.0f;
            resolvedTimer = frame.sequence;
        }
        for (auto& query : frame.passes)
        {
            device->resetTimerQuery(query);
        }
        frame.sequence = 0;
    }
}

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
    prepassShader = createShader(
        device, nvrhi::ShaderType::Pixel,
        (desc.entityIds ? shaders::prepassIdsFragment() : shaders::prepassFragment()), "Prepass FS");
    prepassMaskedShader = createShader(
        device, nvrhi::ShaderType::Pixel,
        (desc.entityIds ? shaders::prepassMaskedIdsFragment() : shaders::prepassMaskedFragment()),
        "Prepass masked FS");
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
        nvrhi::BindingLayoutItem::Texture_SRV(8),          // volumetric fog
        nvrhi::BindingLayoutItem::Texture_SRV(9),          // point light shadow maps
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
    nvrhi::FramebufferInfo prepassInfo =
        nvrhi::FramebufferInfo().addColorFormat(kMotionFormat).setDepthFormat(kDepthFormat);
    if (desc.entityIds)
    {
        prepassInfo.addColorFormat(nvrhi::Format::RG32_UINT);
        entityIdShader =
            createShader(device, nvrhi::ShaderType::Pixel, shaders::entityIdFragment(), "Entity IDs FS");
        if (!entityIdShader)
        {
            return false;
        }
    }
    if (wireframeSupported)
    {
        wireframeShader =
            createShader(device, nvrhi::ShaderType::Pixel, shaders::wireframeFragment(), "Wireframe FS");
        if (!wireframeShader)
        {
            return false;
        }
    }
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

            if (blend == 0 && desc.entityIds)
            {
                auto idPass = pipeline;
                idPass.PS = entityIdShader;
                idPass.renderState.depthStencilState.setDepthWriteEnable(true);
                entityIdPipelines[doubleSided] =
                    device->createGraphicsPipeline(idPass, nvrhi::FramebufferInfo()
                                                               .addColorFormat(nvrhi::Format::RG32_UINT)
                                                               .setDepthFormat(kDepthFormat));
                if (!entityIdPipelines[doubleSided])
                {
                    return false;
                }
            }
            if (blend == 0 && wireframeSupported)
            {
                auto wire = pipeline;
                wire.PS = wireframeShader;
                wire.renderState.rasterState.setFillWireframe();
                wireframePipelines[doubleSided] = device->createGraphicsPipeline(wire, info);
                if (!wireframePipelines[doubleSided])
                {
                    return false;
                }
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
        bindlessPrepassShader = createShader(
            device, nvrhi::ShaderType::Pixel,
            (desc.entityIds ? shaders::prepassIdsBindlessFragment() : shaders::prepassBindlessFragment()),
            "Prepass bindless FS");
        bindlessPrepassMaskedShader =
            createShader(device, nvrhi::ShaderType::Pixel,
                         (desc.entityIds ? shaders::prepassMaskedIdsBindlessFragment()
                                         : shaders::prepassMaskedBindlessFragment()),
                         "Prepass masked bindless FS");
        if (wireframeSupported)
        {
            bindlessWireframeShader =
                createShader(device, nvrhi::ShaderType::Pixel, shaders::wireframeBindlessFragment(),
                             "Wireframe bindless FS");
            if (!bindlessWireframeShader)
            {
                return false;
            }
        }
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
            if (!bindlessPipelines[doubleSided])
            {
                return false;
            }
            if (wireframeSupported)
            {
                auto wire = pipeline;
                wire.PS = bindlessWireframeShader;
                wire.renderState.rasterState.setFillWireframe();
                bindlessWireframePipelines[doubleSided] = device->createGraphicsPipeline(wire, info);
                if (!bindlessWireframePipelines[doubleSided])
                {
                    return false;
                }
            }
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
    if (!post.initialize(device, desc.colorFormat) || !culling.initialize(device) ||
        !particles.initialize(device, *resources, kHdrFormat) || !fog.initialize(device))
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
        // Point light faces store reversed depth: the nearest caster has the largest value, and the bias
        // pushes casters away from the light, towards 0.
        pipeline.renderState.rasterState.setSlopeScaleDepthBias(-settings.slopeBias);
        pipeline.renderState.depthStencilState.setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
        pointShadowPipelines[masked] = device->createGraphicsPipeline(pipeline, shadowInfo);
    }

    pointShadowSlots = settings.enabled ? std::min(settings.pointLightCount, kMaxShadowedPointLights) : 0u;
    nvrhi::TextureDesc pointDesc = mapDesc;
    pointDesc.width = pointShadowSlots > 0 ? std::max(settings.pointResolution, 1u) : 1u;
    pointDesc.height = pointDesc.width;
    pointDesc.arraySize = std::max(pointShadowSlots * kPointShadowFaces, 1u);
    pointDesc.debugName = "Point light shadow maps";
    pointShadowMap = device->createTexture(pointDesc);
    pointShadowsCleared = false;
    pointShadowFramebuffers = {};
    for (u32 layer = 0; layer < pointShadowSlots * kPointShadowFaces; ++layer)
    {
        pointShadowFramebuffers[layer] =
            device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
                pointShadowMap, nvrhi::TextureSubresourceSet(0, 1, layer, 1)));
    }
    viewSets.clear();
    return shadowMap && pointShadowMap && shadowPipelines[0] && shadowPipelines[1] &&
           pointShadowPipelines[0] && pointShadowPipelines[1];
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
            nvrhi::BindingSetItem::Texture_SRV(8, fog.volume()),
            nvrhi::BindingSetItem::Texture_SRV(9, pointShadowMap),
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
        if (depth)
        {
            // Everything submitted so far may still use the old targets.
            RetiredTargets old{{depth, hdr, motion, ids, idDepth}, device->createEventQuery()};
            device->setEventQuery(old.done, nvrhi::CommandQueue::Graphics);
            retired.push_back(std::move(old));
        }
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
        hdrDesc.isUAV = true; // the volumetric fog is applied in a compute pass
        hdrDesc.setClearValue(nvrhi::Color(0.0f));
        hdrDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        hdr = device->createTexture(hdrDesc);
        sceneFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(hdr).setDepthAttachment(depth));
        nvrhi::TextureDesc motionDesc = hdrDesc;
        motionDesc.format = kMotionFormat;
        motionDesc.debugName = "Motion vectors";
        motionDesc.isUAV = false;
        motion = device->createTexture(motionDesc);
        auto prepassDesc = nvrhi::FramebufferDesc().addColorAttachment(motion).setDepthAttachment(depth);
        if (desc.entityIds)
        {
            auto idDesc = hdrDesc;
            idDesc.format = nvrhi::Format::RG32_UINT;
            idDesc.debugName = "Entity IDs";
            idDesc.isUAV = false;
            ids = device->createTexture(idDesc);
            depthDesc.debugName = "Entity ID depth";
            idDepth = device->createTexture(depthDesc);
            idFramebuffer = device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(ids).setDepthAttachment(idDepth));
            prepassDesc.addColorAttachment(ids);
        }
        prepassFramebuffer = device->createFramebuffer(prepassDesc);
        framebuffers.clear();
        postDirty = true;
        culling.resize(depth);
    }
    if (postDirty)
    {
        post.resize(hdr, depth, motion, desc.post);
        fog.resize(hdr, depth);
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

void SceneRenderer::Impl::cull(World& world, const RenderView& view, f32 viewportHeight, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE_N("SceneRenderer::cull");
    const u32 candidates = world.query<const Transform, const RenderMesh>().without<Inactive>().count();
    stats.candidates = candidates;
    // An instance that cross-fades between two levels of detail is drawn twice.
    if (items.size() < static_cast<usize>(candidates) * 2)
    {
        items.resize(static_cast<usize>(candidates) * 2);
    }
    const Frustum frustum = Frustum::fromViewProjection(view.projection * view.view);
    std::array<Frustum, kMaxShadowCascades> cascadeFrusta{};
    for (u32 c = 0; c < cascades.count; ++c)
    {
        cascadeFrusta[c] = Frustum::fromViewProjection(cascades.cascades[c].viewProjection);
    }
    const u32 cascadeCount = cascades.count;
    const u32 pointCount = pointShadowCount;
    // Pixels covered by one world unit at distance 1 (perspective) or at any distance (orthographic).
    const bool orthographic = view.projection[3][3] == 1.0f;
    const f32 pixelsPerUnit = 0.5f * viewportHeight * std::abs(view.projection[1][1]);
    const f32 lodPixelError = view.lodPixelError;
    const f32 fadeStep =
        view.lodFadeSeconds > 0.0f ? std::max(view.deltaSeconds, 0.0f) / view.lodFadeSeconds : 1.0f;
    std::atomic<u32> count{0};
    auto query = world.query<const Transform, const RenderMesh>().without<Inactive>();
    usize historySize = 0;
    query.each([&](Entity entity, const Transform&, const RenderMesh&) {
        historySize = std::max(historySize, static_cast<usize>(entity.index) + 1);
    });
    if (motionHistory.size() < historySize)
    {
        motionHistory.resize(historySize);
    }
    // Tests one entity; `previous` is its world matrix from the last frame (equal to `matrix` when new).
    // `state` holds its level of detail from the last frame (`known`: it was drawn then) and is updated.
    const auto test = [&](Entity entity, const Mat4& matrix, const Mat4& previous, const RenderMesh& renderer,
                          MotionEntry& state, bool known) {
        const RenderResources::GpuMesh* mesh = resources->mesh(renderer.mesh);
        if (mesh == nullptr || renderer.submesh >= mesh->submeshes.size() ||
            (renderer.flags & RenderMesh::kVisible) == 0 || (renderer.layers & view.layerMask) == 0 ||
            (entity.index / 64u < view.hiddenEntities.size() &&
             (view.hiddenEntities[entity.index / 64u] & (u64{1} << (entity.index % 64u))) != 0))
        {
            return;
        }
        const RenderResources::GpuMaterial* material = resources->material(renderer.material);
        const Submesh& submesh = mesh->submeshes[renderer.submesh];
        const Sphere local = Sphere::fromAabb(submesh.bounds);
        const Sphere bounds = local.transformed(matrix);
        u32 passes = frustum.intersects(bounds) ? kCameraPass : 0u;
        if (material->alphaMode != AlphaMode::Blend && (renderer.flags & RenderMesh::kCastShadows) != 0)
        {
            for (u32 c = 0; c < cascadeCount; ++c)
            {
                passes |= cascadeFrusta[c].intersects(bounds) ? (2u << c) : 0u;
            }
            // Point light casters: within the light's range, then per cube face.
            for (u32 light = 0; light < pointCount; ++light)
            {
                const PointShadowCandidate& shadow = pointShadows[light];
                const Vec3 offset = bounds.center - shadow.position;
                const f32 reach = shadow.range + bounds.radius;
                if (glm::dot(offset, offset) > reach * reach)
                {
                    continue;
                }
                for (u32 face = 0; face < kPointShadowFaces; ++face)
                {
                    const u32 index = light * kPointShadowFaces + face;
                    passes |=
                        pointFaceFrusta[index].intersects(bounds) ? (2u << (kMaxShadowCascades + index)) : 0u;
                }
            }
        }
        const Vec3 offset = bounds.center - view.position;
        u32 lod = 0;
        if (submesh.lodCount != 0 && lodPixelError > 0.0f)
        {
            const f32 scale = local.radius > 0.0f ? bounds.radius / local.radius : 1.0f;
            const f32 distance = std::max(glm::length(offset) - bounds.radius, kLodMinDistance);
            lod = selectLod(submesh, scale * (orthographic ? pixelsPerUnit : pixelsPerUnit / distance),
                            lodPixelError);
        }
        if (passes == 0)
        {
            // Not drawn: it reappears at its current level without a fade.
            state.lod = lod;
            state.previousLod = lod;
            state.fade = 1.0f;
            return;
        }
        if (!known)
        {
            state.lod = lod;
            state.previousLod = lod;
            state.fade = 1.0f;
        }
        else if (lod != state.lod)
        {
            if (state.fade < 1.0f && lod == state.previousLod)
            {
                // Back to the level it was leaving: reverse the fade from where it is.
                std::swap(state.lod, state.previousLod);
                state.fade = 1.0f - state.fade;
            }
            else
            {
                state.previousLod = state.lod;
                state.lod = lod;
                state.fade = 0.0f;
            }
        }
        if (state.fade < 1.0f)
        {
            state.fade = std::min(state.fade + fadeStep, 1.0f);
        }
        const bool fading = state.fade < 1.0f && state.previousLod != state.lod;
        const GpuInstance instance{matrix,
                                   glm::transpose(glm::inverse(matrix)),
                                   previous,
                                   {material->index, entity.index, entity.generation,
                                    lodFadeCode(state.lod, state.fade, fading, false)}};
        const f32 distanceSquared = glm::dot(offset, offset);
        const u32 slot = count.fetch_add(fading ? 2u : 1u, std::memory_order_relaxed);
        items[slot] =
            DrawItem{instance, mesh, material, renderer.submesh, state.lod, passes, distanceSquared, bounds};
        if (fading)
        {
            DrawItem& outgoing = items[slot + 1];
            outgoing = items[slot];
            outgoing.lod = state.previousLod;
            outgoing.instance.extra[3] = lodFadeCode(state.previousLod, state.fade, true, true);
        }
    };
    const auto drawEntity = [&](Entity entity, const Transform& transform, const RenderMesh& renderer) {
        const Mat4 matrix = transform.matrix();
        MotionEntry& entry = motionHistory[entity.index];
        const bool known =
            entry.frame == motionFrame && motionFrame != 0 && entry.generation == entity.generation;
        const Mat4 previous = known ? entry.matrix : matrix;
        test(entity, matrix, previous, renderer, entry, known);
        entry.matrix = matrix;
        entry.frame = motionFrame + 1;
        entry.generation = entity.generation;
    };
    if (jobs != nullptr)
    {
        query.parallelEach(*jobs, drawEntity);
    }
    else
    {
        query.each(drawEntity);
    }
    ++motionFrame;
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
        for (u32 shadowView = 0; shadowView < kShadowViews; ++shadowView)
        {
            if ((passes & (2u << shadowView)) != 0)
            {
                shadowOrders[shadowView].push_back(i);
            }
        }
    }
    stats.visible = static_cast<u32>(cameraOrder.size());
    for (const u32 index : cameraOrder)
    {
        const DrawItem& item = items[index];
        ++stats.lodInstances[item.lod];
        stats.lodFades += item.fading() && (item.instance.extra[3] & kLodFadeOutgoing) == 0 ? 1u : 0u;
    }
}

namespace
{

// The first active Wind of the World, or calm air (ADR 0048).
void setWind(World& world, GpuViewConstants& constants)
{
    bool found = false;
    world.query<const Wind>().without<Inactive>().each([&](Entity entity, const Wind& wind) {
        if (found)
        {
            return;
        }
        found = true;
        const f32 length = glm::length(wind.direction);
        const bool valid = length > 1e-6f && std::isfinite(length) && std::isfinite(wind.speed);
        const WindState* state = world.tryGet<WindState>(entity);
        const f32 time = state != nullptr ? state->time : 0.0f;
        constants.wind = valid ? Vec4(wind.direction / length, std::max(wind.speed, 0.0f)) : Vec4(0.0f);
        constants.windGusts = Vec4(std::clamp(wind.gustStrength, 0.0f, 1.0f),
                                   std::max(wind.gustFrequency, 0.0f), std::max(wind.turbulence, 0.0f), time);
        constants.windHistory = Vec4(state != nullptr ? state->previousTime : time, 0.0f, 0.0f, 0.0f);
    });
}

} // namespace

i32 SceneRenderer::Impl::gatherLights(World& world, GpuViewConstants& constants, f32& angularDiameter,
                                      const SkyAtmosphere* atmosphere, const SkyAtmosphereState& sky)
{
    i32 shadowed = -1;
    constants.directionalCount = 0;
    // The sun or moon of a SkyAtmosphere comes first and casts the shadows.
    if (atmosphere != nullptr && sky.light.intensity > 0.0f)
    {
        if (atmosphere->castShadows)
        {
            shadowed = 0;
            angularDiameter = sky.light.moon ? 0.009f : 0.0093f;
        }
        constants.directional[constants.directionalCount++] = GpuDirectionalLight{
            Vec4(sky.light.direction, 0.0f), Vec4(sky.light.color * sky.light.intensity, 0.0f)};
    }
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
            else
            {
                ++stats.lightsDropped;
            }
        });
    constants.pointCount = 0;
    pointCandidateCount = 0;
    world.query<const Transform, const PointLight>().without<Inactive>().each([&](const Transform& transform,
                                                                                  const PointLight& light) {
        if (constants.pointCount < kMaxPointLights)
        {
            if (light.castShadows && light.range > 0.0f)
            {
                pointCandidates[pointCandidateCount++] =
                    PointShadowCandidate{constants.pointCount,
                                         0.0f,
                                         transform.position,
                                         light.range,
                                         std::max(std::min(light.shadowNearPlane, light.range * 0.5f), 1e-4f),
                                         std::max(light.shadowBias, 0.0f),
                                         std::max(light.sourceRadius, 0.0f)};
            }
            constants.points[constants.pointCount++] =
                GpuPointLight{Vec4(transform.position, std::max(light.range, 1e-3f)),
                              Vec4(light.color * light.intensity, 0.0f)};
        }
        else
        {
            ++stats.lightsDropped;
        }
    });
    return shadowed;
}

// Opaque rigid casters ignore their material, so they batch by mesh alone; masked casters and casters that
// move in the wind need it.
const RenderResources::GpuMaterial* SceneRenderer::Impl::shadowMaterial(const DrawItem& item) const noexcept
{
    return item.material->alphaMode == AlphaMode::Mask || item.material->wind ? item.material : nullptr;
}

// Chooses the shadowed point lights: candidates whose range reaches into the view, nearest to the camera
// first (ties keep the gather order), up to the slots of the point shadow map.
void SceneRenderer::Impl::selectPointShadows(const RenderView& view, GpuViewConstants& constants)
{
    pointShadowCount = 0;
    if (pointShadowSlots == 0 || pointCandidateCount == 0)
    {
        return;
    }
    const Frustum frustum = Frustum::fromViewProjection(view.projection * view.view);
    u32 visible = 0;
    for (u32 i = 0; i < pointCandidateCount; ++i)
    {
        PointShadowCandidate candidate = pointCandidates[i];
        if (!frustum.intersects(Sphere{candidate.position, candidate.range}))
        {
            continue;
        }
        candidate.priority =
            std::max(glm::length(candidate.position - view.position) - candidate.range, 0.0f);
        pointCandidates[visible++] = candidate;
    }
    // Insertion sort: stable, and it does not allocate (at most kMaxPointLights entries).
    for (u32 i = 1; i < visible; ++i)
    {
        const PointShadowCandidate candidate = pointCandidates[i];
        u32 j = i;
        while (j > 0 && pointCandidates[j - 1].priority > candidate.priority)
        {
            pointCandidates[j] = pointCandidates[j - 1];
            --j;
        }
        pointCandidates[j] = candidate;
    }
    pointShadowCount = std::min(visible, pointShadowSlots);
    const f32 resolution = static_cast<f32>(std::max(desc.shadows.pointResolution, 1u));
    for (u32 slot = 0; slot < pointShadowCount; ++slot)
    {
        const PointShadowCandidate& candidate = pointCandidates[slot];
        pointShadows[slot] = candidate;
        GpuPointShadow& gpu = constants.pointShadows[slot];
        for (u32 face = 0; face < kPointShadowFaces; ++face)
        {
            gpu.faces[face] = pointShadowFaceMatrix(candidate.position, face, candidate.nearPlane);
            pointFaceFrusta[slot * kPointShadowFaces + face] = Frustum::fromViewProjection(gpu.faces[face]);
        }
        gpu.positionNear = Vec4(candidate.position, candidate.nearPlane);
        gpu.params = Vec4(candidate.bias, desc.shadows.normalBias, candidate.sourceRadius, resolution);
        constants.points[candidate.light].color.w = static_cast<f32>(slot + 1);
    }
}

void SceneRenderer::Impl::updateAtmosphere(nvrhi::ICommandList* commandList, const SkyAtmosphere& atmosphere,
                                           const SkyAtmosphereState& sky)
{
    ILL_PROFILE_SCOPE();
    const u32 resolution = std::bit_floor(std::clamp(atmosphere.lightingResolution, 16u, 256u));
    if (atmosphereEnvironment.index == 0 || resolution != atmosphereResolution)
    {
        if (atmosphereEnvironment.index != 0)
        {
            resources->releaseEnvironment(atmosphereEnvironment);
        }
        atmosphereEnvironment =
            resources->createAtmosphereEnvironment(resolution, resolution, "SkyAtmosphere");
        atmosphereResolution = resolution;
        atmosphereDrawn = false;
    }
    // A still sky is drawn once; a moving one every frame (a few small compute passes).
    if (atmosphereDrawn && sky.atmosphere == atmosphereLast)
    {
        return;
    }
    resources->updateAtmosphereEnvironment(commandList, atmosphereEnvironment, sky.atmosphere,
                                           atmosphereIrradiance(sky.atmosphere));
    atmosphereLast = sky.atmosphere;
    atmosphereDrawn = true;
    ++stats.atmosphereUpdates;
}

void SceneRenderer::Impl::sortShadowCasters(u32 shadowView)
{
    std::sort(shadowOrders[shadowView].begin(), shadowOrders[shadowView].end(), [&](u32 a, u32 b) {
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
        if (x.submesh != y.submesh)
        {
            return x.submesh < y.submesh;
        }
        if (x.lod != y.lod)
        {
            return x.lod < y.lod;
        }
        return x.fading() < y.fading();
    });
}

void SceneRenderer::Impl::drawShadows(nvrhi::ICommandList* commandList, u32 shadowView, u32 instanceOffset)
{
    const std::vector<u32>& order = shadowOrders[shadowView];
    const bool point = shadowView >= kMaxShadowCascades;
    nvrhi::IFramebuffer* framebuffer = point ? pointShadowFramebuffers[shadowView - kMaxShadowCascades].Get()
                                             : shadowFramebuffers[shadowView].Get();
    const auto& casterPipelines = point ? pointShadowPipelines : shadowPipelines;
    const nvrhi::Viewport viewport = framebuffer->getFramebufferInfo().getViewport();
    const u32 count = static_cast<u32>(order.size());
    u32 begin = 0;
    while (begin < count)
    {
        const DrawItem& first = items[order[begin]];
        const RenderResources::GpuMaterial* own = shadowMaterial(first); // nullptr: any material will do
        u32 end = begin + 1;
        while (end < count)
        {
            const DrawItem& next = items[order[end]];
            if (shadowMaterial(next) != own || next.mesh != first.mesh || next.submesh != first.submesh ||
                next.lod != first.lod || next.fading() != first.fading())
            {
                break;
            }
            ++end;
        }
        nvrhi::GraphicsState state;
        // Cross-fading casters dither in the alpha-tested pipeline (an opaque material never fails its test).
        const bool masked = (own != nullptr && own->alphaMode == AlphaMode::Mask) || first.fading();
        state.pipeline = casterPipelines[masked ? 1 : 0];
        state.framebuffer = framebuffer;
        // The material set is bound for opaque casters too: the pipeline layout includes it.
        state.bindings = {shadowSet, own != nullptr ? own->bindingSet : first.material->bindingSet};
        state.vertexBuffers = {
            nvrhi::VertexBufferBinding().setBuffer(resources->vertexPool()).setSlot(0).setOffset(0)};
        state.indexBuffer =
            nvrhi::IndexBufferBinding().setBuffer(resources->indexPool()).setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewportAndScissorRect(viewport);
        commandList->setGraphicsState(state);
        const ShadowPush push{instanceOffset + begin, shadowView};
        commandList->setPushConstants(&push, sizeof(push));
        const SubmeshLod range = first.range();
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(range.indexCount)
                                     .setInstanceCount(end - begin)
                                     .setStartIndexLocation(first.mesh->indexOffset + range.indexOffset)
                                     .setStartVertexLocation(first.mesh->vertexOffset));
        ++(point ? stats.pointShadowDrawCalls : stats.shadowDrawCalls);
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
    impl.wireframeSupported = device.gpu().wireframe;
    impl.timings.available = desc.gpuTimers && device.gpu().timestamps;
    if (impl.timings.available)
    {
        for (auto& frame : impl.timerRing)
        {
            for (auto& query : frame.passes)
            {
                query = impl.device->createTimerQuery();
                impl.timings.available = impl.timings.available && query != nullptr;
            }
        }
    }
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
    std::erase_if(impl.retired,
                  [&](const Impl::RetiredTargets& old) { return impl.device->pollEventQuery(old.done); });
    if (impl.motionWorld != &world)
    {
        resetTemporalHistory();
        impl.motionWorld = &world;
    }
    if (!impl.updateShadowResources())
    {
        ILL_LOG_ERROR("Render", "SceneRenderer: cannot create shadow resources");
        return;
    }
    nvrhi::IFramebuffer* outputFramebuffer = impl.outputFramebufferFor(colorTarget);
    nvrhi::IFramebuffer* framebuffer = impl.sceneFramebuffer;
    PostProcessSettings post = impl.desc.post;
    if (impl.debugView != RenderDebugView::Lit)
    {
        post.temporalAntiAliasing.enabled = false;
        post.bloom.enabled = false;
    }

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
    constants.effects = Vec4(post.ambientOcclusion.enabled ? 1.0f : 0.0f, static_cast<f32>(impl.debugView),
                             view.projection[3][3], 0.0f);
    // The first active SkyAtmosphere draws the sky and its sun or moon is the main light.
    const SkyAtmosphere* atmosphere = nullptr;
    world.query<const SkyAtmosphere>().without<Inactive>().each([&](const SkyAtmosphere& candidate) {
        if (atmosphere == nullptr)
        {
            atmosphere = &candidate;
        }
    });
    const SkyAtmosphereState sky =
        atmosphere != nullptr ? evaluateSkyAtmosphere(*atmosphere) : SkyAtmosphereState{};
    f32 angularDiameter = 0.0f;
    const i32 shadowedLight = impl.gatherLights(world, constants, angularDiameter, atmosphere, sky);
    setWind(world, constants);
    // The first active VolumetricFog fills the air; diagnostic views show the surfaces without it.
    VolumetricFogRenderer::Frame fogFrame;
    if (impl.debugView == RenderDebugView::Lit)
    {
        world.query<const VolumetricFog>().without<Inactive>().each([&fogFrame](const VolumetricFog& fog) {
            if (fogFrame.fog == nullptr)
            {
                fogFrame.fog = &fog;
            }
        });
    }
    fogFrame.inverseViewProjection = glm::inverse(constants.unjitteredViewProjection);
    fogFrame.cameraPosition = view.position;
    fogFrame.cameraForward = Vec3(constants.cameraForward);
    fogFrame.frameIndex = impl.frameIndex;
    constants.fog = impl.fog.prepare(fogFrame);
    constants.fogScreen = Vec4(1.0f / size.x, 1.0f / size.y, size.x, size.y);
    impl.stats.fogFroxels = impl.fog.froxels();
    if (impl.fog.takeVolumeChanged())
    {
        impl.viewSets.clear();
    }

    // Particles face the camera: its right and up axes are the first two rows of the view matrix.
    ParticleRenderer::View particleView{.viewProjection = constants.viewProjection,
                                        .projection = projection,
                                        .position = view.position,
                                        .forward = Vec3(constants.cameraForward),
                                        .right = Vec3(view.view[0][0], view.view[1][0], view.view[2][0]),
                                        .up = Vec3(view.view[0][1], view.view[1][1], view.view[2][1])};
    particleView.fogVolume = impl.fog.volume();
    particleView.fog = constants.fog;
    particleView.fogScreen = constants.fogScreen;
    impl.stats.particles = impl.particles.prepare(world, particleView);

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
    impl.selectPointShadows(view, constants);
    impl.stats.shadowedPointLights = impl.pointShadowCount;

    impl.targets = RenderViewTargets{impl.depth,
                                     impl.ids,
                                     impl.hdr,
                                     impl.motion,
                                     UVec2(colorTarget->getDesc().width, colorTarget->getDesc().height),
                                     UVec2(colorTarget->getDesc().width, colorTarget->getDesc().height),
                                     constants.viewProjection,
                                     constants.unjitteredViewProjection};
    impl.cull(world, view, size.y, jobs);
    const u32 visibleCount = impl.stats.visible;

    // Bindless rendering draws each pipeline's batches with one multi-draw; materials vary per instance.
    const bool bindless = impl.desc.bindless && impl.resources->bindless() && impl.multiDrawIndirect;
    const auto pipelineKey = [](const DrawItem& item) {
        return (item.material->alphaMode == AlphaMode::Mask || item.fading() ? 2u : 0u) +
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
        if (x.submesh != y.submesh)
        {
            return x.submesh < y.submesh;
        }
        if (x.lod != y.lod)
        {
            return x.lod < y.lod;
        }
        return x.fading() < y.fading();
    });

    // Instance data: the camera's instances, then each shadow view's casters.
    std::array<u32, kShadowViews> shadowOffsets{};
    u32 instanceCount = visibleCount;
    for (u32 v = 0; v < kShadowViews; ++v)
    {
        if (!impl.shadowViewActive(v))
        {
            continue;
        }
        impl.sortShadowCasters(v);
        shadowOffsets[v] = instanceCount;
        const auto casters = static_cast<u32>(impl.shadowOrders[v].size());
        instanceCount += casters;
        (v < kMaxShadowCascades ? impl.stats.shadowCasters : impl.stats.pointShadowCasters) += casters;
    }
    impl.instances.resize(instanceCount);
    for (u32 i = 0; i < visibleCount; ++i)
    {
        impl.instances[i] = impl.items[impl.cameraOrder[i]].instance;
    }
    for (u32 v = 0; v < kShadowViews; ++v)
    {
        if (!impl.shadowViewActive(v))
        {
            continue;
        }
        const std::vector<u32>& order = impl.shadowOrders[v];
        for (usize i = 0; i < order.size(); ++i)
        {
            impl.instances[shadowOffsets[v] + i] = impl.items[order[i]].instance;
        }
    }
    impl.ensureInstanceCapacity(instanceCount);

    // A SkyAtmosphere lights the scene with its sky. Otherwise the first SkyLight with an environment
    // provides image-based lighting; without one, the constant ambient light does.
    const SkyLight* skyLight = nullptr;
    EnvironmentHandle environment;
    Quat skyOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    if (atmosphere != nullptr)
    {
        impl.updateAtmosphere(commandList, *atmosphere, sky);
        environment = impl.atmosphereEnvironment;
        constants.atmosphere = packAtmosphere(sky.atmosphere);
        constants.atmosphereLook =
            Vec4(std::max(atmosphere->starIntensity, 0.0f) * sky.starVisibility,
                 static_cast<f32>(impl.frameIndex % 1'000'000u) / 60.0f, 1.0f, sky.moonIllumination);
    }
    else
    {
        world.query<const Transform, const SkyLight, const RenderSky>().without<Inactive>().each(
            [&](const Transform& transform, const SkyLight& light, const RenderSky& gpuSky) {
                if (skyLight == nullptr && gpuSky.environment.index != 0)
                {
                    skyLight = &light;
                    environment = gpuSky.environment;
                    skyOrientation = transform.rotation;
                }
            });
    }
    const RenderResources::GpuEnvironment& gpuEnvironment = impl.resources->environment(environment);
    const bool lit = skyLight != nullptr || atmosphere != nullptr;
    const f32 skyIntensity =
        skyLight != nullptr ? skyLight->intensity : (atmosphere != nullptr ? 1.0f : 0.0f);
    const ShIrradiance irradiance =
        lit ? gpuEnvironment.irradiance : ShIrradiance::uniform(impl.desc.ambient);
    const f32 irradianceScale = lit ? skyIntensity : 1.0f;
    for (usize i = 0; i < irradiance.coefficients.size(); ++i)
    {
        constants.sh[i] = Vec4(irradiance.coefficients[i] * irradianceScale, 0.0f);
    }
    constants.skyRotation = glm::mat4_cast(glm::inverse(glm::normalize(skyOrientation)));
    constants.sky = Vec4(skyIntensity, static_cast<f32>(gpuEnvironment.specularMips - 1), skyIntensity, 0.0f);
    const bool drawBackground = atmosphere != nullptr || (skyLight != nullptr && skyLight->background);

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
            if (!sameState || next.mesh != first.mesh || next.submesh != first.submesh ||
                next.lod != first.lod || next.fading() != first.fading())
            {
                break;
            }
            ++end;
        }
        const auto batch = static_cast<u32>(impl.cullBatches.size());
        const SubmeshLod range = first.range();
        impl.cullBatches.push_back(GpuCulling::Batch{
            range.indexCount, first.mesh->indexOffset + range.indexOffset, first.mesh->vertexOffset, begin});
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

    impl.pollTimers();
    Impl::Timers* timers = nullptr;
    if (impl.timings.available)
    {
        for (auto& frame : impl.timerRing)
        {
            if (frame.sequence == 0)
            {
                timers = &frame;
                break;
            }
        }
    }
    const auto beginPass = [&](usize pass) {
        if (timers)
        {
            commandList->beginTimerQuery(timers->passes[pass]);
        }
    };
    const auto endPass = [&](usize pass) {
        if (timers)
        {
            commandList->endTimerQuery(timers->passes[pass]);
        }
    };
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
    postFrame.exposure = impl.debugView == RenderDebugView::Lit ? view.exposure : 1.0f;
    postFrame.diagnostic = impl.debugView != RenderDebugView::Lit;
    postFrame.historyValid = impl.historyValid;
    postFrame.frameIndex = impl.frameIndex;
    postFrame.deltaSeconds = view.deltaSeconds;
    world.query<const PostEffects>().without<Inactive>().each([&postFrame](const PostEffects& effects) {
        if (postFrame.effects == nullptr)
        {
            postFrame.effects = &effects;
        }
    });
    impl.post.beginFrame(commandList, postFrame, post);

    beginPass(0);
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
    if (!impl.pointShadowsCleared)
    {
        commandList->clearDepthStencilTexture(impl.pointShadowMap, nvrhi::AllSubresources, true, 0.0f, false,
                                              0);
        impl.pointShadowsCleared = true;
    }
    if (impl.pointShadowCount > 0)
    {
        commandList->beginMarker("Point light shadows");
        // Reversed depth: 0 is "nothing in the way". Only the faces drawn this frame are read.
        commandList->clearDepthStencilTexture(
            impl.pointShadowMap,
            nvrhi::TextureSubresourceSet(0, 1, 0, impl.pointShadowCount * kPointShadowFaces), true, 0.0f,
            false, 0);
        for (u32 face = 0; face < impl.pointShadowCount * kPointShadowFaces; ++face)
        {
            impl.drawShadows(commandList, kMaxShadowCascades + face,
                             shadowOffsets[kMaxShadowCascades + face]);
        }
        commandList->endMarker();
    }

    endPass(0);
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
        // Cross-fading instances dither in the alpha-tested prepass.
        const u32 masked = first.material->alphaMode == AlphaMode::Mask || first.fading() ? 1 : 0;
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
    beginPass(1);
    commandList->beginMarker("Depth prepass");
    commandList->clearDepthStencilTexture(impl.depth, nvrhi::AllSubresources, true, 0.0f, false, 0);
    commandList->clearTextureFloat(impl.motion, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    if (impl.ids)
    {
        commandList->clearTextureUInt(impl.ids, nvrhi::AllSubresources, ~0u);
    }
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
    endPass(1);
    beginPass(2);
    if (post.ambientOcclusion.enabled)
    {
        impl.post.computeAmbientOcclusion(commandList);
    }

    endPass(2);
    beginPass(3);
    impl.fog.compute(commandList, impl.viewConstants, impl.shadowMap, impl.pointShadowMap);
    commandList->beginMarker("Shading");
    commandList->clearTextureFloat(impl.hdr, nvrhi::AllSubresources,
                                   nvrhi::Color(impl.desc.clearColor.r, impl.desc.clearColor.g,
                                                impl.desc.clearColor.b, impl.desc.clearColor.a));
    forEachGroup([&](u32 first, u32 count, const DrawItem& item) {
        drawGroup(shadingPipeline(item), framebuffer, first, count, 1, kForwardAmbientOcclusion);
        if (impl.desc.occlusionCulling)
        {
            drawGroup(shadingPipeline(item), framebuffer, first, count, 2, kForwardAmbientOcclusion);
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
    // Fog over the opaque surfaces and the sky; blended surfaces and particles apply it themselves.
    impl.fog.apply(commandList);
    // Blended items one by one, back to front.
    for (u32 i = opaqueCount; i < visibleCount; ++i)
    {
        const DrawItem& item = impl.items[impl.cameraOrder[i]];
        setDrawState(impl.pipelines[1][item.material->doubleSided ? 1 : 0], framebuffer, item, false,
                     blendedSlotBase + (i - opaqueCount), kForwardFog);
        const SubmeshLod range = item.range();
        commandList->drawIndexed(nvrhi::DrawArguments()
                                     .setVertexCount(range.indexCount)
                                     .setStartIndexLocation(item.mesh->indexOffset + range.indexOffset)
                                     .setStartVertexLocation(item.mesh->vertexOffset));
        ++impl.stats.drawCalls;
        impl.stats.triangles += range.indexCount / 3;
    }
    // Particles after the blended geometry, into the HDR colour so bright ones feed bloom.
    impl.particles.draw(commandList, impl.hdr, impl.depth, viewport, particleView);
    impl.stats.drawCalls += impl.particles.drawCalls();
    if (impl.wireframe)
    {
        forEachGroup([&](u32 first, u32 count, const DrawItem& item) {
            const u32 sided = item.material->doubleSided ? 1u : 0u;
            auto* pipeline = bindless ? impl.bindlessWireframePipelines[sided].Get()
                                      : impl.wireframePipelines[sided].Get();
            drawGroup(pipeline, framebuffer, first, count, 1, 0);
            if (impl.desc.occlusionCulling)
            {
                drawGroup(pipeline, framebuffer, first, count, 2, 0);
            }
        });
    }
    if (impl.ids && visibleCount > opaqueCount)
    {
        // Keep the scene depth unchanged for overlays and post processing. A private copy lets the
        // nearest transparent surface win even when triangles within one mesh are unsorted.
        commandList->copyTexture(impl.idDepth, nvrhi::TextureSlice(), impl.depth, nvrhi::TextureSlice());
        for (u32 i = opaqueCount; i < visibleCount; ++i)
        {
            const DrawItem& item = impl.items[impl.cameraOrder[i]];
            setDrawState(impl.entityIdPipelines[item.material->doubleSided ? 1 : 0], impl.idFramebuffer, item,
                         false, blendedSlotBase + i - opaqueCount, 0);
            const SubmeshLod range = item.range();
            commandList->drawIndexed(nvrhi::DrawArguments()
                                         .setVertexCount(range.indexCount)
                                         .setStartIndexLocation(item.mesh->indexOffset + range.indexOffset)
                                         .setStartVertexLocation(item.mesh->vertexOffset));
            ++impl.stats.drawCalls;
        }
    }
    commandList->endMarker();
    impl.culling.finish(commandList);

    endPass(3);
    beginPass(4);
    commandList->beginMarker("Post-processing");
    impl.post.resolve(commandList, outputFramebuffer, post);
    commandList->endMarker();
    commandList->endMarker();

    endPass(4);
    if (timers)
    {
        timers->sequence = ++impl.timerSequence;
    }
    impl.previousViewProjection = view.projection * view.view;
    impl.historyValid = post.temporalAntiAliasing.enabled;
    impl.cameraHistoryValid = true;
    ++impl.frameIndex;
}

const RenderGpuTimings& SceneRenderer::gpuTimings()
{
    m_impl->pollTimers();
    return m_impl->timings;
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
    m_impl->cameraHistoryValid = false;
    m_impl->motionFrame = 0;
    for (Impl::MotionEntry& entry : m_impl->motionHistory)
    {
        entry.frame = 0;
    }
    m_impl->culling.invalidateHistory();
    m_impl->fog.invalidateHistory();
}

void SceneRenderer::setAmbient(Vec3 ambient) noexcept
{
    m_impl->desc.ambient = ambient;
}

void SceneRenderer::setClearColor(Vec4 color) noexcept
{
    m_impl->desc.clearColor = color;
}

const RenderViewTargets& SceneRenderer::viewTargets() const noexcept
{
    return m_impl->targets;
}

void SceneRenderer::setDebugView(RenderDebugView mode) noexcept
{
    if (m_impl->debugView != mode)
    {
        m_impl->debugView = mode;
        resetTemporalHistory();
    }
}

RenderDebugView SceneRenderer::debugView() const noexcept
{
    return m_impl->debugView;
}

bool SceneRenderer::setWireframeOverlay(bool enabled) noexcept
{
    m_impl->wireframe = enabled && m_impl->wireframeSupported;
    return !enabled || m_impl->wireframeSupported;
}

} // namespace illusion
