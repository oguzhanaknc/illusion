#pragma once

// Internal: the volumetric fog passes of the SceneRenderer (VolumetricFog, ADR 0050): a froxel grid aligned
// with the screen, filled with the scattered light of the frame (inject, temporally reprojected), integrated
// front to back, and applied to the HDR image. The froxel volumes are made the first time a scene has fog and
// kept for the render size; binding sets are cached.

#include <illusion/core/Math.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <nvrhi/nvrhi.h>

#include <array>

namespace illusion
{

class VolumetricFogRenderer
{
public:
    // Froxel slices along the view depth.
    static constexpr u32 kSlices = 64;
    // Most froxels across the wider side of the screen; the cells are at least 8 pixels wide.
    static constexpr u32 kMaxColumns = 240;

    // Per-frame inputs.
    struct Frame
    {
        const VolumetricFog* fog = nullptr; // the scene's, or none: no fog this frame
        Mat4 inverseViewProjection{1.0f};   // unjittered clip -> world
        Vec3 cameraPosition{0.0f};
        Vec3 cameraForward{0.0f, 0.0f, -1.0f};
        u32 frameIndex = 0;
    };

    bool initialize(nvrhi::IDevice* device);

    // The render targets the fog is applied to (both of the render size; `hdr` is a UAV). Call when they
    // change.
    void resize(nvrhi::ITexture* hdr, nvrhi::ITexture* depth);

    // Prepares the frame and returns ViewConstants::fog (x is 0 without fog). Makes the froxel volumes on the
    // first frame with fog.
    [[nodiscard]] Vec4 prepare(const Frame& frame);

    // True once after the texture returned by volume() changed (binding sets that hold it are stale).
    [[nodiscard]] bool takeVolumeChanged() noexcept;

    // The integrated fog read by blended surfaces and particles; a 1x1x1 placeholder before the first fog.
    [[nodiscard]] nvrhi::ITexture* volume() const noexcept;

    // Injects and integrates this frame's fog. `viewConstants` holds the frame's ViewConstants, `shadowMap`
    // the cascades. Does nothing when prepare() found no fog.
    void compute(nvrhi::ICommandList* commands, nvrhi::IBuffer* viewConstants, nvrhi::ITexture* shadowMap);

    // Fogs the HDR image by the scene depth (after the opaque surfaces and the sky).
    void apply(nvrhi::ICommandList* commands);

    // Froxels injected this frame (0 without fog).
    [[nodiscard]] u32 froxels() const noexcept { return m_active ? m_grid.x * m_grid.y * kSlices : 0; }

    // Forgets the reprojection history (camera cut, new World).
    void invalidateHistory() noexcept { m_historyValid = false; }

private:
    struct Constants
    {
        Mat4 inverseViewProjection;
        Vec4 grid;
        Vec4 depth;
        Vec4 medium;
        Vec4 shape;
        Vec4 noise;
        Vec4 history;
        Vec4 previousCamera;
        Vec4 previousForward;
    };

    void createVolumes();
    void createSets(nvrhi::IBuffer* viewConstants, nvrhi::ITexture* shadowMap);
    [[nodiscard]] nvrhi::BindingSetHandle createSet(nvrhi::IBuffer* viewConstants, nvrhi::ITexture* history,
                                                    nvrhi::ITexture* volume, nvrhi::ITexture* outVolume,
                                                    nvrhi::ITexture* outImage) const;

    nvrhi::IDevice* m_device = nullptr;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::BufferHandle m_constants;
    nvrhi::SamplerHandle m_linearClamp;
    nvrhi::SamplerHandle m_shadowCompare;
    nvrhi::ComputePipelineHandle m_inject;
    nvrhi::ComputePipelineHandle m_integrate;
    nvrhi::ComputePipelineHandle m_apply;
    nvrhi::TextureHandle m_placeholderVolume; // read where no fog volume exists
    nvrhi::TextureHandle m_scratchVolume;     // the unused 3D output of the apply pass
    nvrhi::TextureHandle m_scratchImage;      // the unused 2D output of the volume passes

    nvrhi::TextureHandle m_hdr;
    nvrhi::TextureHandle m_depth;
    UVec2 m_size{0, 0};
    UVec2 m_grid{0, 0};
    std::array<nvrhi::TextureHandle, 2> m_scattering{}; // [frame parity]: this frame's, the last one's
    nvrhi::TextureHandle m_integrated;
    std::array<nvrhi::BindingSetHandle, 2> m_injectSets{}; // [parity]
    std::array<nvrhi::BindingSetHandle, 2> m_integrateSets{};
    nvrhi::BindingSetHandle m_applySet;
    nvrhi::IBuffer* m_boundConstants = nullptr; // what the cached sets bind
    nvrhi::ITexture* m_boundShadowMap = nullptr;
    bool m_volumeChanged = false;

    Constants m_frame{};
    bool m_active = false;
    bool m_historyValid = false;
    u32 m_parity = 0; // the scattering volume written this frame
    Vec3 m_previousPosition{0.0f};
    Vec3 m_previousForward{0.0f};
    f32 m_previousNear = 1.0f;
    f32 m_previousLogRange = 1.0f;
};

} // namespace illusion
