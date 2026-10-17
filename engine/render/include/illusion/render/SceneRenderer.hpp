#pragma once

/// @file
/// Forward PBR renderer for ECS scenes: frustum culling (parallel on the Job System), sorting into
/// instanced batches, cascaded soft shadows, a depth prepass, HDR shading (opaque, then
/// back-to-front transparent), reversed-Z depth, then ambient occlusion, TAA, bloom and tonemapping.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/PostProcess.hpp>
#include <illusion/render/Shadows.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>

namespace illusion
{

class JobSystem;
class RenderDevice;
class RenderResources;
class World;

/// Camera matrices for one rendered view.
struct RenderView
{
    Mat4 view{1.0f};       ///< World to view.
    Mat4 projection{1.0f}; ///< View to clip (reversed-Z, see perspectiveReversedZ()).
    Vec3 position{0.0f};   ///< Camera position in world space.
    f32 exposure = 1.0f;   ///< Exposure multiplier.

    /// View looking through @p camera placed at @p transform, for a target with @p aspect (width / height).
    [[nodiscard]] static RenderView fromCamera(const Transform& transform, const Camera& camera,
                                               f32 aspect) noexcept;
};

/// Counters from the last SceneRenderer::render() call.
struct RenderStats
{
    u32 candidates = 0;      ///< MeshRenderer entities considered.
    u32 visible = 0;         ///< Entities that passed frustum culling.
    u32 drawCalls = 0;       ///< Instanced draw calls issued.
    u64 triangles = 0;       ///< Triangles submitted.
    u32 shadowCasters = 0;   ///< Shadow-caster instances, summed over cascades.
    u32 shadowDrawCalls = 0; ///< Instanced draw calls issued for shadows.
};

/// Creation parameters for SceneRenderer.
struct SceneRendererDesc
{
    nvrhi::Format colorFormat = nvrhi::Format::SBGRA8_UNORM; ///< Format of the targets passed to render().
    Vec3 ambient{0.03f}; ///< Uniform ambient radiance, used when the scene has no SkyLight.
    Vec4 clearColor{0.02f, 0.02f, 0.03f,
                    1.0f};        ///< Background radiance where nothing is drawn (before tonemapping).
    ShadowSettings shadows;       ///< Shadow quality.
    PostProcessSettings post;     ///< Ambient occlusion, bloom and anti-aliasing.
    bool occlusionCulling = true; ///< Skip opaque objects hidden behind others (GPU, two-phase Hi-Z).
};

/// Renders World entities that have Transform + MeshRenderer, lit by DirectionalLight, PointLight and
/// SkyLight entities, with the SkyLight's environment as background. Use on the render thread.
class SceneRenderer : NonCopyable
{
public:
    /// Maximum lights used per frame (the first ones found).
    static constexpr u32 kMaxDirectionalLights = 4;
    /// @copydoc kMaxDirectionalLights
    static constexpr u32 kMaxPointLights = 16;

    /// Creates the renderer. Returns nullptr (and logs why) on failure.
    [[nodiscard]] static std::unique_ptr<SceneRenderer>
    create(RenderDevice& device, RenderResources& resources, const SceneRendererDesc& desc = {});
    ~SceneRenderer();

    /// Clears @p colorTarget and renders @p world into it as seen from @p view. Culling runs on @p jobs
    /// when given. The renderer owns a matching depth buffer, resized automatically.
    void render(nvrhi::ICommandList* commandList, nvrhi::ITexture* colorTarget, World& world,
                const RenderView& view, JobSystem* jobs = nullptr);

    /// Counters from the last render() call.
    [[nodiscard]] const RenderStats& stats() const noexcept;

    /// Changes the ambient radiance used without a SkyLight.
    void setAmbient(Vec3 ambient) noexcept;

    /// Current shadow settings.
    [[nodiscard]] const ShadowSettings& shadowSettings() const noexcept;

    /// Changes the shadow settings; GPU resources are recreated at the next render() if needed.
    void setShadowSettings(const ShadowSettings& settings) noexcept;

    /// Current post-processing settings.
    [[nodiscard]] const PostProcessSettings& postProcessSettings() const noexcept;

    /// Changes the post-processing settings; textures are recreated at the next render() if needed.
    void setPostProcessSettings(const PostProcessSettings& settings) noexcept;

    /// Whether hidden opaque objects are culled on the GPU.
    [[nodiscard]] bool occlusionCulling() const noexcept;

    /// Turns GPU occlusion culling on or off.
    void setOcclusionCulling(bool enabled) noexcept;

    /// Opaque instances the GPU drew in the last render() (after occlusion culling). Maps a readback
    /// buffer, so call it only when the GPU has finished that frame (tools, tests, statistics overlays).
    [[nodiscard]] u32 readGpuDrawnInstances() const;

    /// Forgets the accumulated TAA history, for example after a camera cut.
    void resetTemporalHistory() noexcept;

    /// Screen-space motion vectors written by the last render() (RG16F, UV units, current minus previous
    /// position; zero where nothing moved or nothing was drawn). For debug views and tools.
    [[nodiscard]] nvrhi::ITexture* motionVectors() const noexcept;

    /// Cascades used by the last render() call (count 0 when nothing cast shadows).
    [[nodiscard]] const ShadowCascades& shadowCascades() const noexcept;

private:
    SceneRenderer();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
