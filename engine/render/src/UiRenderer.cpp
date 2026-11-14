#include <illusion/assets/Font.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/BuiltinShaders.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/UiRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

struct Push
{
    Vec2 scale{0.0f};
    f32 sdfSpread = 0.0f;
    u32 flags = 0;
};

constexpr u32 kEncodeSrgb = 1;

// A registered texture: a font atlas the renderer owns, or an image of the RenderResources.
struct TextureSlot
{
    nvrhi::TextureHandle owned;
    TextureHandle image{};
    nvrhi::BindingSetHandle bindings;
    bool live = false;
};

// GPU objects of a removed texture, kept until the GPU has finished the frames that drew it.
struct Retired
{
    nvrhi::TextureHandle texture;
    nvrhi::BindingSetHandle bindings;
};

struct RetireBatch
{
    nvrhi::EventQueryHandle query;
    std::vector<Retired> retired;
};

} // namespace

struct UiRenderer::Impl
{
    nvrhi::IDevice* device = nullptr;
    RenderResources* resources = nullptr;
    UiRendererDesc desc{};
    bool encodeSrgb = false;
    nvrhi::CommandListHandle uploads;
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle fragmentShader;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::SamplerHandle sampler;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BufferHandle vertices;
    nvrhi::BufferHandle indices;
    nvrhi::FramebufferHandle framebuffer;
    nvrhi::ITexture* boundTarget = nullptr;

    // Slot 0 is the white texture of solid colours; ids are slot indices.
    std::vector<TextureSlot> slots;
    std::vector<u32> free; // reused last in, first out
    std::vector<Retired> releasing;
    std::vector<RetireBatch> batches;
    std::vector<u32> retiring; // indices in `batches`, oldest first
    std::vector<u32> spare;    // indices in `batches`

    [[nodiscard]] nvrhi::BindingSetHandle bind(nvrhi::ITexture* texture) const
    {
        nvrhi::BindingSetDesc set;
        set.bindings = {nvrhi::BindingSetItem::Texture_SRV(0, texture),
                        nvrhi::BindingSetItem::Sampler(0, sampler),
                        nvrhi::BindingSetItem::PushConstants(0, sizeof(Push))};
        return device->createBindingSet(set, layout);
    }

    // A free slot for @p texture (owned when @p owned) and its id; none when the table is full.
    UiTextureId store(nvrhi::ITexture* texture, nvrhi::TextureHandle owned, TextureHandle image)
    {
        if (free.empty())
        {
            return {};
        }
        nvrhi::BindingSetHandle bindings = bind(texture);
        if (!bindings)
        {
            return {};
        }
        const u32 index = free.back();
        free.pop_back();
        slots[index] = TextureSlot{std::move(owned), image, std::move(bindings), true};
        return UiTextureId{index};
    }

    // Moves what was removed since the last call behind an event query, and frees the batches the GPU has
    // finished.
    void collect()
    {
        if (!releasing.empty())
        {
            u32 index = 0;
            if (!spare.empty())
            {
                index = spare.back();
                spare.pop_back();
                device->resetEventQuery(batches[index].query);
            }
            else
            {
                index = static_cast<u32>(batches.size());
                batches.emplace_back().query = device->createEventQuery();
            }
            RetireBatch& batch = batches[index];
            device->setEventQuery(batch.query, nvrhi::CommandQueue::Graphics);
            batch.retired.swap(releasing);
            retiring.push_back(index);
        }
        usize done = 0;
        while (done < retiring.size() && device->pollEventQuery(batches[retiring[done]].query))
        {
            batches[retiring[done]].retired.clear();
            spare.push_back(retiring[done]);
            ++done;
        }
        retiring.erase(retiring.begin(), retiring.begin() + static_cast<std::ptrdiff_t>(done));
    }
};

UiRenderer::UiRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

UiRenderer::~UiRenderer() = default;

std::unique_ptr<UiRenderer> UiRenderer::create(RenderDevice& device, RenderResources& resources,
                                               const UiRendererDesc& desc)
{
    if (desc.maxVertices == 0 || desc.maxIndices == 0 || desc.maxTextures == 0)
    {
        ILL_LOG_ERROR("Render", "UiRenderer: the capacities must not be zero");
        return nullptr;
    }
    std::unique_ptr<UiRenderer> renderer(new UiRenderer());
    Impl& impl = *renderer->m_impl;
    impl.device = device.device();
    impl.resources = &resources;
    impl.desc = desc;
    impl.encodeSrgb = !nvrhi::getFormatInfo(desc.outputFormat).isSRGB;
    const auto shader = [&](nvrhi::ShaderType type, std::span<const std::byte> bytes) {
        return impl.device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName("Ui"),
                                         bytes.data(), bytes.size());
    };
    impl.vertexShader = shader(nvrhi::ShaderType::Vertex, shaders::uiVertex());
    impl.fragmentShader = shader(nvrhi::ShaderType::Pixel, shaders::uiFragment());

    const auto attribute = [](const char* name, nvrhi::Format format, usize offset) {
        return nvrhi::VertexAttributeDesc()
            .setName(name)
            .setFormat(format)
            .setOffset(static_cast<u32>(offset))
            .setElementStride(sizeof(UiVertex));
    };
    const nvrhi::VertexAttributeDesc attributes[] = {
        attribute("POSITION", nvrhi::Format::RG32_FLOAT, offsetof(UiVertex, position)),
        attribute("TEXCOORD", nvrhi::Format::RG32_FLOAT, offsetof(UiVertex, uv)),
        attribute("COLOR", nvrhi::Format::RGBA8_UNORM, offsetof(UiVertex, color)),
    };
    if (impl.vertexShader)
    {
        impl.inputLayout = impl.device->createInputLayout(attributes, static_cast<u32>(std::size(attributes)),
                                                          impl.vertexShader);
    }

    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::All;
    layout.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0),
                       nvrhi::BindingLayoutItem::PushConstants(0, sizeof(Push))};
    impl.layout = impl.device->createBindingLayout(layout);
    impl.sampler = impl.device->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

    nvrhi::BufferDesc vertices;
    vertices.byteSize = static_cast<u64>(desc.maxVertices) * sizeof(UiVertex);
    vertices.isVertexBuffer = true;
    vertices.debugName = "UI vertices";
    vertices.enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer);
    impl.vertices = impl.device->createBuffer(vertices);
    nvrhi::BufferDesc indices;
    indices.byteSize = static_cast<u64>(desc.maxIndices) * sizeof(u32);
    indices.isIndexBuffer = true;
    indices.debugName = "UI indices";
    indices.enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer);
    impl.indices = impl.device->createBuffer(indices);
    impl.uploads = impl.device->createCommandList();

    nvrhi::TextureDesc whiteDesc;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteDesc.format = nvrhi::Format::RGBA8_UNORM;
    whiteDesc.debugName = "UI white";
    whiteDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    nvrhi::TextureHandle white = impl.device->createTexture(whiteDesc);
    if (!impl.vertexShader || !impl.fragmentShader || !impl.inputLayout || !impl.layout || !impl.sampler ||
        !impl.vertices || !impl.indices || !impl.uploads || !white)
    {
        ILL_LOG_ERROR("Render", "UiRenderer: cannot create the GPU objects of the UI pass");
        return nullptr;
    }

    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.VS = impl.vertexShader;
    pipeline.PS = impl.fragmentShader;
    pipeline.inputLayout = impl.inputLayout;
    pipeline.primType = nvrhi::PrimitiveType::TriangleList;
    pipeline.bindingLayouts = {impl.layout};
    pipeline.renderState.rasterState.setCullNone().setScissorEnable(true);
    pipeline.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
    pipeline.renderState.blendState.targets[0]
        .setBlendEnable(true)
        .setSrcBlend(nvrhi::BlendFactor::One)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
    impl.pipeline = impl.device->createGraphicsPipeline(
        pipeline, nvrhi::FramebufferInfo().addColorFormat(desc.outputFormat));
    if (!impl.pipeline)
    {
        ILL_LOG_ERROR("Render", "UiRenderer: cannot create the pipeline for the output format");
        return nullptr;
    }

    constexpr u32 kWhite = 0xffffffffu;
    impl.uploads->open();
    impl.uploads->writeTexture(white, 0, 0, &kWhite, sizeof(kWhite));
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    impl.slots.resize(static_cast<usize>(desc.maxTextures) + 1);
    impl.slots[0] = TextureSlot{white, TextureHandle{}, impl.bind(white), true};
    if (!impl.slots[0].bindings)
    {
        return nullptr;
    }
    impl.free.reserve(desc.maxTextures);
    for (u32 index = desc.maxTextures; index > 0; --index)
    {
        impl.free.push_back(index);
    }
    return renderer;
}

UiTextureId UiRenderer::addFont(const Font& font)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.collect();
    if (font.empty() || impl.free.empty())
    {
        return {};
    }
    nvrhi::TextureDesc desc;
    desc.width = font.atlasSize().x;
    desc.height = font.atlasSize().y;
    desc.format = nvrhi::Format::R8_UNORM;
    desc.debugName = "UI font atlas";
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    nvrhi::TextureHandle atlas = impl.device->createTexture(desc);
    if (!atlas)
    {
        return {};
    }
    impl.uploads->open();
    impl.uploads->writeTexture(atlas, 0, 0, font.atlas().data(), desc.width);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);
    nvrhi::ITexture* texture = atlas.Get();
    return impl.store(texture, std::move(atlas), TextureHandle{});
}

UiTextureId UiRenderer::addImage(TextureHandle texture)
{
    Impl& impl = *m_impl;
    impl.collect();
    nvrhi::ITexture* gpu = impl.resources->texture(texture);
    if (gpu == nullptr)
    {
        return {};
    }
    return impl.store(gpu, nullptr, texture);
}

void UiRenderer::remove(UiTextureId texture)
{
    Impl& impl = *m_impl;
    if (texture.value == 0 || texture.value >= impl.slots.size() || !impl.slots[texture.value].live)
    {
        return;
    }
    TextureSlot& slot = impl.slots[texture.value];
    impl.releasing.push_back(Retired{std::move(slot.owned), std::move(slot.bindings)});
    slot = TextureSlot{};
    impl.free.push_back(texture.value);
}

bool UiRenderer::render(nvrhi::ICommandList* commands, nvrhi::ITexture* target, const UiDrawData& data)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (commands == nullptr || target == nullptr)
    {
        return false;
    }
    const nvrhi::TextureDesc& targetDesc = target->getDesc();
    const UiCanvasMetrics& canvas = data.canvas;
    if (targetDesc.format != impl.desc.outputFormat || targetDesc.width == 0 || targetDesc.height == 0 ||
        data.vertices.size() > impl.desc.maxVertices || data.indices.size() > impl.desc.maxIndices ||
        !std::isfinite(canvas.pixelsPerUnit) || canvas.pixelsPerUnit <= 0.0f ||
        !std::isfinite(canvas.size.x) || !std::isfinite(canvas.size.y) || canvas.size.x <= 0.0f ||
        canvas.size.y <= 0.0f)
    {
        return false;
    }
    impl.collect();
    if (data.commands.empty() || data.indices.empty() || data.vertices.empty())
    {
        return true;
    }
    if (!impl.framebuffer || impl.boundTarget != target)
    {
        impl.framebuffer =
            impl.device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
        impl.boundTarget = impl.framebuffer ? target : nullptr;
        if (!impl.framebuffer)
        {
            return false;
        }
    }
    commands->writeBuffer(impl.vertices, data.vertices.data(), data.vertices.size_bytes());
    commands->writeBuffer(impl.indices, data.indices.data(), data.indices.size_bytes());

    const f32 width = static_cast<f32>(targetDesc.width);
    const f32 height = static_cast<f32>(targetDesc.height);
    const nvrhi::Viewport viewport(width, height);
    Push push;
    push.scale = Vec2(2.0f) / canvas.size;
    push.flags = impl.encodeSrgb ? kEncodeSrgb : 0u;
    const auto pixel = [&](f32 units, f32 limit, bool up) {
        const f32 scaled = units * canvas.pixelsPerUnit;
        const f32 rounded = up ? std::ceil(scaled) : std::floor(scaled);
        return static_cast<int>(std::isfinite(rounded) ? std::clamp(rounded, 0.0f, limit) : 0.0f);
    };
    const usize indexCount = data.indices.size();
    for (const UiDrawCommand& command : data.commands)
    {
        const u32 id = command.texture.value;
        if (id >= impl.slots.size() || !impl.slots[id].live || command.indexCount == 0 ||
            command.firstIndex > indexCount || command.indexCount > indexCount - command.firstIndex)
        {
            continue;
        }
        const TextureSlot& slot = impl.slots[id];
        if (slot.image.index != 0 && !impl.resources->isValid(slot.image))
        {
            continue;
        }
        const nvrhi::Rect scissor(
            pixel(command.clip.min.x, width, false), pixel(command.clip.max.x, width, true),
            pixel(command.clip.min.y, height, false), pixel(command.clip.max.y, height, true));
        if (scissor.maxX <= scissor.minX || scissor.maxY <= scissor.minY)
        {
            continue;
        }
        nvrhi::GraphicsState state;
        state.pipeline = impl.pipeline;
        state.framebuffer = impl.framebuffer;
        state.bindings = {slot.bindings};
        state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(impl.vertices).setSlot(0).setOffset(0)};
        state.indexBuffer =
            nvrhi::IndexBufferBinding().setBuffer(impl.indices).setFormat(nvrhi::Format::R32_UINT);
        state.viewport.addViewport(viewport).addScissorRect(scissor);
        commands->setGraphicsState(state);
        push.sdfSpread = std::isfinite(command.sdfSpread) ? std::max(command.sdfSpread, 0.0f) : 0.0f;
        commands->setPushConstants(&push, sizeof(push));
        commands->drawIndexed(nvrhi::DrawArguments()
                                  .setVertexCount(command.indexCount)
                                  .setStartIndexLocation(command.firstIndex));
    }
    return true;
}

} // namespace illusion
