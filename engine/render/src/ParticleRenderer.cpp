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
};

struct ParticlePush
{
    u32 firstInstance;
    u32 textured;
    u32 padding[2];
};

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
        if (!m_pipelines[blend])
        {
            return false;
        }
    }
    return true;
}

u32 ParticleRenderer::prepare(World& world, const View& view)
{
    ILL_PROFILE_SCOPE();
    m_particles.clear();
    m_batches.clear();
    world.query<const ParticleEmitter, const ParticlePool, const Transform>().without<Inactive>().each(
        [&](Entity entity, const ParticleEmitter& emitter, const ParticlePool& pool,
            const Transform& transform) {
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
        if (batch.blend != ParticleBlend::Alpha)
        {
            continue;
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
    return static_cast<u32>(m_particles.size());
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

void ParticleRenderer::bindTargets(nvrhi::ITexture* color, nvrhi::ITexture* depth)
{
    if (m_color.Get() == color && m_depth.Get() == depth)
    {
        return;
    }
    m_color = color;
    m_depth = depth;
    m_framebuffer = m_device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(color));
    m_bindingSets.clear();
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
    if (m_particles.empty())
    {
        return;
    }
    ILL_PROFILE_SCOPE();
    commands->beginMarker("Particles");
    ensureCapacity(static_cast<u32>(m_particles.size()));
    bindTargets(color, depth);
    commands->writeBuffer(m_instances, m_particles.data(), m_particles.size() * sizeof(Instance));
    GpuParticleConstants constants{};
    constants.viewProjection = view.viewProjection;
    constants.inverseProjection = glm::inverse(view.projection);
    constants.cameraRight = Vec4(view.right, 0.0f);
    constants.cameraUp = Vec4(view.up, 0.0f);
    constants.cameraForward = Vec4(view.forward, 0.0f);
    constants.cameraPosition = Vec4(view.position, 1.0f);
    constants.params = Vec4(m_softDistance > 0.0f ? 1.0f / m_softDistance : 0.0f, 0.0f, 0.0f, 0.0f);
    commands->writeBuffer(m_constants, &constants, sizeof(constants));
    for (const Batch& batch : m_batches)
    {
        nvrhi::GraphicsState state;
        state.pipeline = m_pipelines[batch.blend == ParticleBlend::Additive ? 0 : 1];
        state.framebuffer = m_framebuffer;
        state.bindings = {bindingSetFor(batch.texture)};
        state.viewport.addViewportAndScissorRect(viewport);
        commands->setGraphicsState(state);
        const ParticlePush push{batch.first, batch.texture != nullptr ? 1u : 0u, {0, 0}};
        commands->setPushConstants(&push, sizeof(push));
        commands->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(batch.count));
        ++m_drawCalls;
    }
    commands->endMarker();
}

} // namespace illusion
