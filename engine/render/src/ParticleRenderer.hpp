#pragma once

// Internal: draws the live particles of every ParticlePool as camera-facing billboards (ADR 0047). Used by
// SceneRenderer after the blended geometry, in the HDR target.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/particles/ParticleComponents.hpp>

#include <nvrhi/nvrhi.h>

#include <unordered_map>
#include <vector>

namespace illusion
{

class RenderResources;
class World;

class ParticleRenderer
{
public:
    // One particle as the shader reads it (layout matches GpuParticle in Particles.slang).
    struct Instance
    {
        Vec4 positionSize; // world position, width
        Vec4 color;        // linear colour and alpha
    };

    // The camera of the frame.
    struct View
    {
        Mat4 viewProjection; // jittered like the scene
        Mat4 projection;     // to turn scene depth into a view distance
        Vec3 position;
        Vec3 forward;
        Vec3 right;
        Vec3 up;
    };

    bool initialize(nvrhi::IDevice* device, RenderResources& resources, nvrhi::Format colorFormat);

    // Gathers the live particles of the visible emitters: each emitter becomes one batch; alpha batches are
    // sorted back to front and so are the particles inside them. Allocates only when there are more
    // particles or emitters than ever before. Returns the number of particles to draw.
    u32 prepare(World& world, const View& view);

    // Uploads the particles and draws them into @p color. The scene @p depth is read (not attached) for the
    // depth test and the soft fade. Does nothing when prepare() found none.
    void draw(nvrhi::ICommandList* commands, nvrhi::ITexture* color, nvrhi::ITexture* depth,
              const nvrhi::Viewport& viewport, const View& view);

    // Draw calls of the last draw().
    [[nodiscard]] u32 drawCalls() const noexcept { return m_drawCalls; }

    // Soft particles fade over this distance in metres where they meet the scene (0: off).
    void setSoftDistance(f32 metres) noexcept { m_softDistance = metres; }

private:
    struct Batch
    {
        u32 first = 0;
        u32 count = 0;
        ParticleBlend blend = ParticleBlend::Additive;
        nvrhi::ITexture* texture = nullptr; // null: the round dot
        f32 distance = 0.0f;                // of the emitter from the camera, for sorting
    };

    nvrhi::IBindingSet* bindingSetFor(nvrhi::ITexture* texture);
    void ensureCapacity(u32 particles);
    void bindTargets(nvrhi::ITexture* color, nvrhi::ITexture* depth);

    nvrhi::IDevice* m_device = nullptr;
    RenderResources* m_resources = nullptr;
    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_fragmentShader;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::GraphicsPipelineHandle m_pipelines[2]; // [ParticleBlend]
    nvrhi::BufferHandle m_constants;
    nvrhi::BufferHandle m_instances;
    u32 m_capacity = 0;
    nvrhi::TextureHandle m_color;
    nvrhi::TextureHandle m_depth;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::TextureHandle m_white; // bound when a batch has no sprite
    nvrhi::SamplerHandle m_sampler;
    std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_bindingSets;
    std::vector<Instance> m_particles;
    std::vector<Batch> m_batches;
    std::vector<Instance> m_sortScratch;
    std::vector<std::pair<f32, u32>> m_order;
    f32 m_softDistance = 0.3f;
    u32 m_drawCalls = 0;
};

} // namespace illusion
