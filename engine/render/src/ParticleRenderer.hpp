#pragma once

// Internal: draws the live particles of every ParticlePool as camera-facing billboards (ADR 0047). Used by
// SceneRenderer after the blended geometry, in the HDR target. CPU-simulated emitters are uploaded each
// frame; GPU-simulated ones (ParticleSimulation::Gpu) are simulated here in compute shaders, one step for
// every step ParticleWorld published, and drawn from their state buffers with indirect draws.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Transform.hpp>
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
        nvrhi::ITexture* fogVolume = nullptr; // integrated volumetric fog (a placeholder without fog)
        Vec4 fog{0.0f};                       // ViewConstants::fog: x is 0 without fog
        Vec4 fogScreen{0.0f};                 // ViewConstants::fogScreen
    };

    bool initialize(nvrhi::IDevice* device, RenderResources& resources, nvrhi::Format colorFormat);

    // Gathers the live particles of the visible emitters: each emitter becomes one batch; alpha batches are
    // sorted back to front and so are the particles inside them. Allocates only when there are more
    // particles or emitters than ever before. Returns the number of particles to draw.
    u32 prepare(World& world, const View& view);

    // Uploads the particles and draws them into @p color. The scene @p depth is read (not attached) for the
    // depth test and the soft fade; View::fogVolume fogs them. Does nothing when prepare() found none.
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
        u32 gpu = ~0u;                      // index in m_gpuBatches for a GPU emitter
    };

    // What a GPU emitter needs this frame (gathered by prepare()).
    struct GpuBatch
    {
        u64 key = 0; // Entity::toBits()
        ParticleEmitter emitter;
        Transform transform;
        u32 capacity = 0;
        u32 step = 0;
        u32 births = 0;
        f32 seconds = 0.0f;
        u32 generation = 0;
        Vec3 gravity{0.0f};
        Vec3 wind{0.0f};
    };

    // The GPU state of one emitter, kept between frames.
    struct GpuEmitter
    {
        nvrhi::BufferHandle states[2];
        nvrhi::BufferHandle counters;        // uint[2]: live particles of states[0] and states[1]
        nvrhi::BufferHandle arguments;       // DrawIndirectArguments of the current buffer
        nvrhi::BindingSetHandle simulate[2]; // [current]: input states[current], output the other
        nvrhi::BindingSetHandle draw[2];     // [current]
        nvrhi::ITexture* drawSprite = nullptr;
        u32 capacity = 0;
        u32 current = 0;
        u32 step = 0;
        u32 generation = 0;
        bool valid = false; // simulated at least once since it was (re)created or reset
        u32 seenFrame = 0;
    };

    // A readback slot: the live counts of the GPU emitters drawn in one frame.
    struct CountReadback
    {
        nvrhi::BufferHandle buffer;
        u32 capacity = 0;
        std::vector<u64> keys;
        bool pending = false;
    };

    static constexpr u32 kReadbackSlots = 3;

    void simulateGpu(nvrhi::ICommandList* commands, const GpuBatch& batch, GpuEmitter& state);
    GpuEmitter& gpuEmitterFor(const GpuBatch& batch);
    void readCounts(World& world);

    nvrhi::IBindingSet* bindingSetFor(nvrhi::ITexture* texture);
    void ensureCapacity(u32 particles);
    void bindTargets(nvrhi::ITexture* color, nvrhi::ITexture* depth, nvrhi::ITexture* fog);

    nvrhi::IDevice* m_device = nullptr;
    RenderResources* m_resources = nullptr;
    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_fragmentShader;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::GraphicsPipelineHandle m_pipelines[2];    // [ParticleBlend]
    nvrhi::GraphicsPipelineHandle m_gpuPipelines[2]; // [ParticleBlend], drawing GPU state buffers
    nvrhi::ShaderHandle m_gpuVertexShader;
    nvrhi::ShaderHandle m_gpuFragmentShader;
    nvrhi::BindingLayoutHandle m_simulateLayout;
    nvrhi::ComputePipelineHandle m_reset;
    nvrhi::ComputePipelineHandle m_simulate;
    nvrhi::ComputePipelineHandle m_spawn;
    nvrhi::ComputePipelineHandle m_finish;
    nvrhi::BufferHandle m_simulateConstants;
    std::unordered_map<u64, GpuEmitter> m_gpuEmitters;
    std::vector<GpuBatch> m_gpuBatches;
    CountReadback m_readbacks[kReadbackSlots];
    u32 m_frame = 0;
    u64 m_gpuAlive = 0;
    nvrhi::BufferHandle m_constants;
    nvrhi::BufferHandle m_instances;
    u32 m_capacity = 0;
    nvrhi::TextureHandle m_color;
    nvrhi::TextureHandle m_depth;
    nvrhi::TextureHandle m_fog;
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
