#include "ParticleRenderer.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/BuiltinShaders.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderResources.hpp>

#include <nvrhi/utils.h>

#include <algorithm>

namespace illusion
{

namespace
{

// Layouts match Particles.slang.
struct GpuParticleConstants
{
    Mat4 viewProjection;
    Mat4 inverseProjection;
    Vec4 cameraRight;
    Vec4 cameraUp;
    Vec4 cameraForward;
    Vec4 cameraPosition;
    Vec4 params;
    Vec4 fog;       // ViewConstants::fog (FogSample.slang)
    Vec4 fogScreen; // ViewConstants::fogScreen
};

struct ParticlePush
{
    u32 firstInstance;
    u32 textured;
    u32 additive;
    u32 padding;
    Vec4 colorStart;
    Vec4 colorEnd;
    Vec4 size;          // x: width at birth, y: at death
    Vec4 modelPosition; // w: 1 for local-space particles
    Vec4 modelRotation; // quaternion xyzw
    Vec4 modelScale;
};

// One GPU particle (ParticleState in ParticleState.slang).
struct GpuParticleState
{
    Vec4 positionAge;
    Vec4 velocityLife;
};

// Layout matches SimulateConstants in ParticleSimulate.slang.
struct GpuSimulateConstants
{
    Vec4 acceleration;
    Vec4 timing;
    Vec4 speed;
    Vec4 shape;
    Vec4 position;
    Vec4 rotation;
    Vec4 scale;
    u32 counts[4];
    u32 state[4];
    Vec4 wind; // xyz: wind velocity, w: ParticleEmitter::windFactor
};

constexpr u32 kSimulateGroup = 64;
// After this many steps without a view, a GPU emitter is simulated as if one step passed (its particles
// do not catch up on every missed frame at once).
constexpr u32 kMaxCatchUpSteps = 8;

nvrhi::BufferHandle createGpuBuffer(nvrhi::IDevice* device, u64 bytes, u32 stride, bool raw, bool arguments,
                                    const char* name)
{
    nvrhi::BufferDesc desc;
    desc.byteSize = std::max<u64>(bytes, 16);
    desc.structStride = stride;
    desc.canHaveUAVs = true;
    desc.canHaveRawViews = raw;
    desc.isDrawIndirectArgs = arguments;
    desc.debugName = name;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::UnorderedAccess);
    return device->createBuffer(desc);
}

nvrhi::ComputePipelineHandle createCompute(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout,
                                           std::span<const std::byte> code, const char* name)
{
    nvrhi::ShaderHandle shader =
        device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute).setDebugName(name),
                             code.data(), code.size());
    if (!shader)
    {
        return nullptr;
    }
    return device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(layout));
}

nvrhi::ShaderHandle createShader(nvrhi::IDevice* device, nvrhi::ShaderType type,
                                 std::span<const std::byte> code, const char* name)
{
    return device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName(name), code.data(),
                                code.size());
}

} // namespace

bool ParticleRenderer::initialize(nvrhi::IDevice* device, RenderResources& resources,
                                  nvrhi::Format colorFormat)
{
    m_device = device;
    m_resources = &resources;
    m_vertexShader =
        createShader(device, nvrhi::ShaderType::Vertex, shaders::particlesVertex(), "Particles VS");
    m_fragmentShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::particlesFragment(), "Particles FS");
    if (!m_vertexShader || !m_fragmentShader)
    {
        return false;
    }
    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.registerSpace = 0;
    layout.registerSpaceIsDescriptorSet = true;
    layout.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1), // sprite
        nvrhi::BindingLayoutItem::Texture_SRV(2), // scene depth
        nvrhi::BindingLayoutItem::Texture_SRV(3), // volumetric fog
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(ParticlePush)),
    };
    m_layout = device->createBindingLayout(layout);
    m_constants = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(GpuParticleConstants), "Particle constants", 16));
    m_sampler = device->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    m_white = resources.texture(
        resources.createTexture(makeSolidImage(1, 1, Vec4(1.0f), ImageFormat::RGBA8Srgb), "Particle white"));
    if (!m_layout || !m_constants || !m_sampler || m_white == nullptr)
    {
        return false;
    }
    m_gpuVertexShader =
        createShader(device, nvrhi::ShaderType::Vertex, shaders::particlesGpuVertex(), "Particles GPU VS");
    m_gpuFragmentShader =
        createShader(device, nvrhi::ShaderType::Pixel, shaders::particlesGpuFragment(), "Particles GPU FS");
    nvrhi::BindingLayoutDesc simulate;
    simulate.visibility = nvrhi::ShaderType::Compute;
    simulate.registerSpace = 0;
    simulate.registerSpaceIsDescriptorSet = true;
    simulate.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::RawBuffer_UAV(1),
        nvrhi::BindingLayoutItem::RawBuffer_UAV(2),
    };
    m_simulateLayout = device->createBindingLayout(simulate);
    m_simulateConstants = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(GpuSimulateConstants), "Particle simulation constants", 64));
    if (!m_gpuVertexShader || !m_gpuFragmentShader || !m_simulateLayout || !m_simulateConstants)
    {
        return false;
    }
    m_reset = createCompute(device, m_simulateLayout, shaders::particleReset(), "Particles reset CS");
    m_simulate =
        createCompute(device, m_simulateLayout, shaders::particleSimulate(), "Particles simulate CS");
    m_spawn = createCompute(device, m_simulateLayout, shaders::particleSpawn(), "Particles spawn CS");
    m_finish = createCompute(device, m_simulateLayout, shaders::particleFinish(), "Particles finish CS");
    if (!m_reset || !m_simulate || !m_spawn || !m_finish)
    {
        return false;
    }
    const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo().addColorFormat(colorFormat);
    for (u32 blend = 0; blend < 2; ++blend)
    {
        nvrhi::GraphicsPipelineDesc pipeline;
        pipeline.primType = nvrhi::PrimitiveType::TriangleList;
        pipeline.VS = m_vertexShader;
        pipeline.PS = m_fragmentShader;
        pipeline.bindingLayouts = {m_layout};
        pipeline.renderState.rasterState.setCullNone();
        // The shader tests against the scene depth it reads.
        pipeline.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
        // Colours arrive premultiplied: additive adds them, alpha covers what is behind by their alpha.
        pipeline.renderState.blendState.targets[0]
            .setBlendEnable(true)
            .setSrcBlend(nvrhi::BlendFactor::One)
            .setDestBlend(blend == 0 ? nvrhi::BlendFactor::One : nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
            .setDestBlendAlpha(nvrhi::BlendFactor::One);
        m_pipelines[blend] = device->createGraphicsPipeline(pipeline, info);
        pipeline.VS = m_gpuVertexShader;
        pipeline.PS = m_gpuFragmentShader;
        m_gpuPipelines[blend] = device->createGraphicsPipeline(pipeline, info);
        if (!m_pipelines[blend] || !m_gpuPipelines[blend])
        {
            return false;
        }
    }
    return true;
}

u32 ParticleRenderer::prepare(World& world, const View& view)
{
    ILL_PROFILE_SCOPE();
    readCounts(world);
    m_particles.clear();
    m_batches.clear();
    m_gpuBatches.clear();
    m_gpuAlive = 0;
    world.query<const ParticleEmitter, const ParticlePool, const Transform>().without<Inactive>().each(
        [&](Entity entity, const ParticleEmitter& emitter, const ParticlePool& pool,
            const Transform& transform) {
            if (emitter.simulation == ParticleSimulation::Gpu)
            {
                GpuBatch gpu{entity.toBits(), emitter,        transform,       pool.capacity,
                             pool.gpuStep,    pool.gpuBirths, pool.gpuSeconds, pool.gpuGeneration,
                             pool.gpuGravity, pool.gpuWind};
                if (gpu.capacity == 0)
                {
                    return; // not published yet
                }
                Batch batch;
                batch.blend = emitter.blend;
                batch.distance = glm::dot(transform.position - view.position, view.forward);
                if (const RenderParticles* resolved = world.tryGet<RenderParticles>(entity))
                {
                    batch.texture = m_resources->texture(resolved->texture);
                }
                batch.gpu = static_cast<u32>(m_gpuBatches.size());
                m_gpuBatches.push_back(gpu);
                m_batches.push_back(batch);
                m_gpuAlive += pool.count;
                return;
            }
            if (pool.count == 0)
            {
                return;
            }
            Batch batch;
            batch.first = static_cast<u32>(m_particles.size());
            batch.count = pool.count;
            batch.blend = emitter.blend;
            batch.distance = glm::dot(transform.position - view.position, view.forward);
            if (const RenderParticles* resolved = world.tryGet<RenderParticles>(entity))
            {
                batch.texture = m_resources->texture(resolved->texture);
            }
            const bool local = emitter.space == ParticleSpace::Local;
            for (u32 i = 0; i < pool.count; ++i)
            {
                const f32 life = pool.lifeFraction(i);
                const Vec3 position = pool.position(i);
                const Vec3 placed =
                    local ? transform.position + transform.rotation * (position * transform.scale) : position;
                m_particles.push_back(
                    Instance{Vec4(placed, particleSize(emitter, life)), particleColor(emitter, life)});
            }
            m_batches.push_back(batch);
        });
    // Alpha particles cover what is behind them: the farthest emitters first, and the farthest particles
    // first inside each. Additive ones need no order.
    std::stable_sort(m_batches.begin(), m_batches.end(), [](const Batch& a, const Batch& b) {
        if (a.blend != b.blend)
        {
            return a.blend == ParticleBlend::Additive;
        }
        return a.blend == ParticleBlend::Alpha && a.distance > b.distance;
    });
    for (const Batch& batch : m_batches)
    {
        if (batch.blend != ParticleBlend::Alpha || batch.gpu != ~0u)
        {
            continue; // GPU particles are drawn in the order they live in
        }
        m_order.clear();
        for (u32 i = 0; i < batch.count; ++i)
        {
            const Vec3 position(m_particles[batch.first + i].positionSize);
            m_order.emplace_back(glm::dot(position - view.position, view.forward), batch.first + i);
        }
        std::sort(m_order.begin(), m_order.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        m_sortScratch.clear();
        for (const auto& [distance, index] : m_order)
        {
            m_sortScratch.push_back(m_particles[index]);
        }
        std::copy(m_sortScratch.begin(), m_sortScratch.end(), m_particles.begin() + batch.first);
    }
    return static_cast<u32>(std::min<u64>(m_particles.size() + m_gpuAlive, ~0u));
}

void ParticleRenderer::readCounts(World& world)
{
    // The slot written kReadbackSlots frames ago: mapping it waits only if the GPU is that far behind.
    CountReadback& slot = m_readbacks[m_frame % kReadbackSlots];
    if (!slot.pending)
    {
        return;
    }
    slot.pending = false;
    const auto* counts =
        static_cast<const u32*>(m_device->mapBuffer(slot.buffer, nvrhi::CpuAccessMode::Read));
    if (counts == nullptr)
    {
        return;
    }
    for (usize i = 0; i < slot.keys.size(); ++i)
    {
        const Entity entity{static_cast<u32>(slot.keys[i]), static_cast<u32>(slot.keys[i] >> 32)};
        if (world.isAlive(entity))
        {
            if (ParticlePool* pool = world.tryGet<ParticlePool>(entity))
            {
                pool->count = counts[i];
            }
        }
    }
    m_device->unmapBuffer(slot.buffer);
}

ParticleRenderer::GpuEmitter& ParticleRenderer::gpuEmitterFor(const GpuBatch& batch)
{
    GpuEmitter& state = m_gpuEmitters[batch.key];
    if (state.capacity != batch.capacity)
    {
        state = GpuEmitter{};
        state.capacity = batch.capacity;
        const u64 bytes = static_cast<u64>(batch.capacity) * sizeof(GpuParticleState);
        for (u32 i = 0; i < 2; ++i)
        {
            state.states[i] =
                createGpuBuffer(m_device, bytes, sizeof(GpuParticleState), false, false, "Particle states");
        }
        state.counters = createGpuBuffer(m_device, 8, 0, true, false, "Particle counts");
        state.arguments = createGpuBuffer(m_device, 16, 0, true, true, "Particle draw arguments");
        for (u32 current = 0; current < 2; ++current)
        {
            nvrhi::BindingSetDesc desc;
            desc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_simulateConstants),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(0, state.states[current]),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(0, state.states[1 - current]),
                nvrhi::BindingSetItem::RawBuffer_UAV(1, state.counters),
                nvrhi::BindingSetItem::RawBuffer_UAV(2, state.arguments),
            };
            state.simulate[current] = m_device->createBindingSet(desc, m_simulateLayout);
        }
    }
    return state;
}

void ParticleRenderer::simulateGpu(nvrhi::ICommandList* commands, const GpuBatch& batch, GpuEmitter& state)
{
    const bool reset = !state.valid || state.generation != batch.generation;
    if (!reset && state.step == batch.step)
    {
        return; // this step was simulated already (another frame of the same step, or another pass)
    }
    const ParticleEmitter& emitter = batch.emitter;
    const u32 missed = reset ? 1u : std::min(batch.step - state.step, kMaxCatchUpSteps);
    GpuSimulateConstants constants{};
    constants.acceleration = Vec4(batch.gravity * emitter.gravityFactor, emitter.drag);
    constants.wind = Vec4(batch.wind, std::max(emitter.windFactor, 0.0f));
    constants.timing =
        Vec4(batch.seconds * static_cast<f32>(missed), emitter.lifetimeMin, emitter.lifetimeMax, 0.0f);
    constants.speed =
        Vec4(emitter.speedMin, emitter.speedMax, emitter.spread, static_cast<f32>(emitter.shape));
    constants.shape = Vec4(emitter.boxExtents, emitter.radius);
    const bool world = emitter.space == ParticleSpace::World;
    constants.position = Vec4(batch.transform.position, world ? 1.0f : 0.0f);
    constants.rotation = Vec4(batch.transform.rotation.x, batch.transform.rotation.y,
                              batch.transform.rotation.z, batch.transform.rotation.w);
    constants.scale = Vec4(batch.transform.scale, 0.0f);
    constants.counts[0] = batch.births;
    constants.counts[1] = batch.capacity;
    constants.counts[2] = emitter.seed;
    constants.counts[3] = batch.step;
    if (reset)
    {
        state.current = 0;
    }
    constants.state[0] = state.current;
    commands->writeBuffer(m_simulateConstants, &constants, sizeof(constants));
    nvrhi::ComputeState compute;
    compute.bindings = {state.simulate[state.current]};
    // Each pass reads what the previous one wrote. NVRHI does not transition bindings that stay the same,
    // so the UAV barriers between the passes are explicit.
    nvrhi::IBuffer* written[] = {state.states[1 - state.current], state.counters, state.arguments};
    const auto dispatch = [&](nvrhi::IComputePipeline* pipeline, u32 groups) {
        compute.pipeline = pipeline;
        commands->setComputeState(compute);
        commands->dispatch(groups);
        for (nvrhi::IBuffer* buffer : written)
        {
            commands->setBufferState(buffer, nvrhi::ResourceStates::UnorderedAccess);
        }
        commands->commitBarriers();
    };
    const auto groupsFor = [](u32 threads) {
        return std::max((threads + kSimulateGroup - 1) / kSimulateGroup, 1u);
    };
    if (reset)
    {
        dispatch(m_reset, 1);
    }
    dispatch(m_simulate, groupsFor(batch.capacity));
    if (batch.births > 0)
    {
        dispatch(m_spawn, groupsFor(batch.births));
    }
    dispatch(m_finish, 1);
    state.current = 1 - state.current;
    state.step = batch.step;
    state.generation = batch.generation;
    state.valid = true;
}

void ParticleRenderer::ensureCapacity(u32 particles)
{
    if (particles <= m_capacity)
    {
        return;
    }
    m_capacity = std::max(particles, std::max(m_capacity * 2, 1024u));
    nvrhi::BufferDesc desc;
    desc.byteSize = static_cast<u64>(m_capacity) * sizeof(Instance);
    desc.structStride = sizeof(Instance);
    desc.debugName = "Particle instances";
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    m_instances = m_device->createBuffer(desc);
    m_bindingSets.clear();
}

void ParticleRenderer::bindTargets(nvrhi::ITexture* color, nvrhi::ITexture* depth, nvrhi::ITexture* fog)
{
    if (m_color.Get() == color && m_depth.Get() == depth && m_fog.Get() == fog)
    {
        return;
    }
    m_color = color;
    m_depth = depth;
    m_fog = fog;
    m_framebuffer = m_device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(color));
    m_bindingSets.clear();
    for (auto& [key, emitter] : m_gpuEmitters)
    {
        emitter.draw[0] = nullptr; // they bind the depth and the fog
        emitter.draw[1] = nullptr;
    }
}

nvrhi::IBindingSet* ParticleRenderer::bindingSetFor(nvrhi::ITexture* texture)
{
    nvrhi::ITexture* sprite = texture != nullptr ? texture : m_white.Get();
    nvrhi::BindingSetHandle& set = m_bindingSets[sprite];
    if (!set)
    {
        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_constants),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_instances),
            nvrhi::BindingSetItem::Texture_SRV(1, sprite),
            nvrhi::BindingSetItem::Texture_SRV(2, m_depth),
            nvrhi::BindingSetItem::Texture_SRV(3, m_fog),
            nvrhi::BindingSetItem::Sampler(0, m_sampler),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(ParticlePush)),
        };
        set = m_device->createBindingSet(desc, m_layout);
    }
    return set;
}

void ParticleRenderer::draw(nvrhi::ICommandList* commands, nvrhi::ITexture* color, nvrhi::ITexture* depth,
                            const nvrhi::Viewport& viewport, const View& view)
{
    m_drawCalls = 0;
    if (m_batches.empty())
    {
        return;
    }
    ILL_PROFILE_SCOPE();
    commands->beginMarker("Particles");
    ensureCapacity(static_cast<u32>(std::max<usize>(m_particles.size(), 1)));
    bindTargets(color, depth, view.fogVolume);
    if (!m_particles.empty())
    {
        commands->writeBuffer(m_instances, m_particles.data(), m_particles.size() * sizeof(Instance));
    }
    // GPU emitters: one simulation step for each new step, then their live counts to the readback slot.
    CountReadback& slot = m_readbacks[m_frame % kReadbackSlots];
    if (!m_gpuBatches.empty())
    {
        commands->beginMarker("GPU particle simulation");
        const u32 needed = static_cast<u32>(m_gpuBatches.size());
        if (slot.capacity < needed)
        {
            slot.capacity = std::max(needed, slot.capacity * 2);
            nvrhi::BufferDesc desc;
            desc.byteSize = static_cast<u64>(slot.capacity) * sizeof(u32);
            desc.cpuAccess = nvrhi::CpuAccessMode::Read;
            desc.debugName = "Particle count readback";
            desc.enableAutomaticStateTracking(nvrhi::ResourceStates::CopyDest);
            slot.buffer = m_device->createBuffer(desc);
        }
        slot.keys.clear();
        for (const GpuBatch& batch : m_gpuBatches)
        {
            GpuEmitter& state = gpuEmitterFor(batch);
            simulateGpu(commands, batch, state);
            state.seenFrame = m_frame;
            commands->copyBuffer(slot.buffer, slot.keys.size() * sizeof(u32), state.counters,
                                 state.current * sizeof(u32), sizeof(u32));
            slot.keys.push_back(batch.key);
        }
        slot.pending = true;
        commands->endMarker();
    }
    GpuParticleConstants constants{};
    constants.viewProjection = view.viewProjection;
    constants.inverseProjection = glm::inverse(view.projection);
    constants.cameraRight = Vec4(view.right, 0.0f);
    constants.cameraUp = Vec4(view.up, 0.0f);
    constants.cameraForward = Vec4(view.forward, 0.0f);
    constants.cameraPosition = Vec4(view.position, 1.0f);
    constants.params = Vec4(m_softDistance > 0.0f ? 1.0f / m_softDistance : 0.0f, 0.0f, 0.0f, 0.0f);
    constants.fog = view.fog;
    constants.fogScreen = view.fogScreen;
    commands->writeBuffer(m_constants, &constants, sizeof(constants));
    for (const Batch& batch : m_batches)
    {
        const u32 blend = batch.blend == ParticleBlend::Additive ? 0 : 1;
        nvrhi::GraphicsState state;
        state.framebuffer = m_framebuffer;
        state.viewport.addViewportAndScissorRect(viewport);
        ParticlePush push{};
        push.firstInstance = batch.first;
        push.textured = batch.texture != nullptr ? 1u : 0u;
        push.additive = batch.blend == ParticleBlend::Additive ? 1u : 0u;
        if (batch.gpu == ~0u)
        {
            state.pipeline = m_pipelines[blend];
            state.bindings = {bindingSetFor(batch.texture)};
            commands->setGraphicsState(state);
            commands->setPushConstants(&push, sizeof(push));
            commands->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(batch.count));
            ++m_drawCalls;
            continue;
        }
        const GpuBatch& gpu = m_gpuBatches[batch.gpu];
        GpuEmitter& emitter = m_gpuEmitters.at(gpu.key);
        nvrhi::ITexture* sprite = batch.texture != nullptr ? batch.texture : m_white.Get();
        if (emitter.drawSprite != sprite || !emitter.draw[0])
        {
            emitter.drawSprite = sprite;
            for (u32 current = 0; current < 2; ++current)
            {
                nvrhi::BindingSetDesc desc;
                desc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_constants),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, emitter.states[current]),
                    nvrhi::BindingSetItem::Texture_SRV(1, sprite),
                    nvrhi::BindingSetItem::Texture_SRV(2, m_depth),
                    nvrhi::BindingSetItem::Texture_SRV(3, m_fog),
                    nvrhi::BindingSetItem::Sampler(0, m_sampler),
                    nvrhi::BindingSetItem::PushConstants(1, sizeof(ParticlePush)),
                };
                emitter.draw[current] = m_device->createBindingSet(desc, m_layout);
            }
        }
        push.colorStart = gpu.emitter.colorStart;
        push.colorEnd = gpu.emitter.colorEnd;
        push.size = Vec4(gpu.emitter.sizeStart, gpu.emitter.sizeEnd, 0.0f, 0.0f);
        const bool local = gpu.emitter.space == ParticleSpace::Local;
        push.modelPosition = Vec4(gpu.transform.position, local ? 1.0f : 0.0f);
        push.modelRotation = Vec4(gpu.transform.rotation.x, gpu.transform.rotation.y,
                                  gpu.transform.rotation.z, gpu.transform.rotation.w);
        push.modelScale = Vec4(gpu.transform.scale, 0.0f);
        state.pipeline = m_gpuPipelines[blend];
        state.bindings = {emitter.draw[emitter.current]};
        state.indirectParams = emitter.arguments;
        commands->setGraphicsState(state);
        commands->setPushConstants(&push, sizeof(push));
        commands->drawIndirect(0);
        ++m_drawCalls;
    }
    // GPU state of emitters that were not drawn for a while is freed.
    std::erase_if(m_gpuEmitters, [this](const auto& item) { return m_frame - item.second.seenFrame > 120; });
    ++m_frame;
    commands->endMarker();
}

} // namespace illusion
