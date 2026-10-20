#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiRenderer.hpp>
#include <illusion/editor/ui/UiShaders.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <imgui.h>
#include <utility>
#include <vector>

namespace illusion::editor::ui
{

namespace
{

// Texture ids: a tag in the top two bits (Dear ImGui's own textures or registered ones), a generation that
// changes when a slot is reused, and the slot index.
constexpr u64 kTagImGui = u64{1} << 62;
constexpr u64 kTagRegistered = u64{2} << 62;
constexpr u64 kTagMask = u64{3} << 62;

constexpr u32 kMaxTextureSide = 8192;

// ImGui::GetPlatformIO().DrawCallback_ResetRenderState. render() sets the whole graphics state for every
// draw command, so a reset request needs no work.
void resetRenderState(const ImDrawList*, const ImDrawCmd*)
{
}
constexpr usize kMinVertices = 1 << 14;
constexpr usize kMinIndices = 1 << 15;

struct UiPush
{
    f32 scale[2];
    f32 translate[2];
};

struct PresentPush
{
    u32 decodeSrgb;
    u32 padding[3];
};

usize grownCapacity(usize needed, usize minimum) noexcept
{
    return std::bit_ceil(std::max(needed, minimum));
}

} // namespace

struct UiRenderer::Impl
{
    struct Slot
    {
        nvrhi::TextureHandle texture;
        nvrhi::BindingSetHandle bindings;
        u32 generation = 0;
        bool live = false;
    };

    struct FrameBuffers
    {
        nvrhi::BufferHandle vertices;
        nvrhi::BufferHandle indices;
        usize vertexCapacity = 0;
        usize indexCapacity = 0;
    };

    struct PipelineEntry
    {
        nvrhi::Format format;
        nvrhi::GraphicsPipelineHandle pipeline;
    };

    struct PresentSet
    {
        nvrhi::ITexture* source = nullptr;
        nvrhi::BindingSetHandle bindings;
    };

    RenderDevice* device = nullptr;
    nvrhi::IDevice* gpu = nullptr;
    UiContext* context = nullptr;
    UiRendererDesc desc;
    nvrhi::ShaderHandle uiVertex;
    nvrhi::ShaderHandle uiFragment;
    nvrhi::ShaderHandle presentVertex;
    nvrhi::ShaderHandle presentFragment;
    nvrhi::InputLayoutHandle inputLayout;
    nvrhi::BindingLayoutHandle uiLayout;
    nvrhi::BindingLayoutHandle presentLayout;
    nvrhi::SamplerHandle sampler;
    std::vector<PipelineEntry> uiPipelines;
    std::vector<PipelineEntry> presentPipelines;
    std::vector<FrameBuffers> frames;
    u32 frameIndex = 0;
    std::vector<Slot> slots;
    std::vector<u32> freeSlots;
    std::vector<u8> expanded; // Alpha8 textures expanded to RGBA8
    PresentSet presentSets[2];
    u32 nextPresentSet = 0;

    bool initialize()
    {
        const auto shader = [this](nvrhi::ShaderType type, std::span<const std::byte> code,
                                   const char* name) {
            return gpu->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName(name), code.data(),
                                     code.size());
        };
        uiVertex = shader(nvrhi::ShaderType::Vertex, shaders::uiVertex(), "UI VS");
        uiFragment = shader(nvrhi::ShaderType::Pixel, shaders::uiFragment(), "UI FS");
        presentVertex = shader(nvrhi::ShaderType::Vertex, shaders::presentVertex(), "UI present VS");
        presentFragment = shader(nvrhi::ShaderType::Pixel, shaders::presentFragment(), "UI present FS");
        if (!uiVertex || !uiFragment || !presentVertex || !presentFragment)
        {
            return false;
        }

        const auto attribute = [](const char* name, nvrhi::Format format, usize offset) {
            return nvrhi::VertexAttributeDesc()
                .setName(name)
                .setFormat(format)
                .setOffset(static_cast<u32>(offset))
                .setElementStride(sizeof(ImDrawVert));
        };
        const nvrhi::VertexAttributeDesc attributes[] = {
            attribute("POSITION", nvrhi::Format::RG32_FLOAT, offsetof(ImDrawVert, pos)),
            attribute("TEXCOORD", nvrhi::Format::RG32_FLOAT, offsetof(ImDrawVert, uv)),
            attribute("COLOR", nvrhi::Format::RGBA8_UNORM, offsetof(ImDrawVert, col)),
        };
        inputLayout = gpu->createInputLayout(attributes, static_cast<u32>(std::size(attributes)), uiVertex);

        nvrhi::BindingLayoutDesc ui;
        ui.visibility = nvrhi::ShaderType::All;
        ui.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::PushConstants(0, sizeof(UiPush)),
        };
        uiLayout = gpu->createBindingLayout(ui);
        nvrhi::BindingLayoutDesc present;
        present.visibility = nvrhi::ShaderType::All;
        present.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PresentPush)),
        };
        presentLayout = gpu->createBindingLayout(present);
        sampler = gpu->createSampler(
            nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
        frames.resize(std::max<u32>(desc.framesInFlight, 1));
        return inputLayout && uiLayout && presentLayout && sampler;
    }

    nvrhi::IGraphicsPipeline* uiPipeline(nvrhi::IFramebuffer* framebuffer)
    {
        const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
        const nvrhi::Format format =
            info.colorFormats.empty() ? nvrhi::Format::UNKNOWN : info.colorFormats[0];
        for (const PipelineEntry& entry : uiPipelines)
        {
            if (entry.format == format)
            {
                return entry.pipeline;
            }
        }
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.VS = uiVertex;
        pipelineDesc.PS = uiFragment;
        pipelineDesc.bindingLayouts = {uiLayout};
        pipelineDesc.renderState.rasterState.setCullNone().setScissorEnable(true);
        pipelineDesc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
        nvrhi::BlendState::RenderTarget& blend = pipelineDesc.renderState.blendState.targets[0];
        blend.setBlendEnable(true)
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
            .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::One)
            .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
        nvrhi::GraphicsPipelineHandle pipeline = gpu->createGraphicsPipeline(pipelineDesc, info);
        uiPipelines.push_back(PipelineEntry{format, pipeline});
        return pipeline;
    }

    nvrhi::IGraphicsPipeline* presentPipeline(nvrhi::IFramebuffer* framebuffer)
    {
        const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
        const nvrhi::Format format =
            info.colorFormats.empty() ? nvrhi::Format::UNKNOWN : info.colorFormats[0];
        for (const PipelineEntry& entry : presentPipelines)
        {
            if (entry.format == format)
            {
                return entry.pipeline;
            }
        }
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
        pipelineDesc.VS = presentVertex;
        pipelineDesc.PS = presentFragment;
        pipelineDesc.bindingLayouts = {presentLayout};
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
        nvrhi::GraphicsPipelineHandle pipeline = gpu->createGraphicsPipeline(pipelineDesc, info);
        presentPipelines.push_back(PipelineEntry{format, pipeline});
        return pipeline;
    }

    u64 allocateSlot(nvrhi::ITexture* texture, u64 tag)
    {
        u32 index = 0;
        if (!freeSlots.empty())
        {
            index = freeSlots.back();
            freeSlots.pop_back();
        }
        else
        {
            index = static_cast<u32>(slots.size());
            slots.emplace_back();
        }
        Slot& slot = slots[index];
        slot.texture = texture;
        slot.bindings = nullptr;
        slot.live = true;
        ++slot.generation;
        return tag | (u64{slot.generation & 0x3FFFFFFFu} << 32) | index;
    }

    Slot* slotOf(u64 id) noexcept
    {
        const auto index = static_cast<u32>(id & 0xFFFFFFFFu);
        const auto generation = static_cast<u32>((id >> 32) & 0x3FFFFFFFu);
        if (index >= slots.size() || !slots[index].live ||
            (slots[index].generation & 0x3FFFFFFFu) != generation)
        {
            return nullptr;
        }
        return &slots[index];
    }

    void releaseSlot(u64 id) noexcept
    {
        if (Slot* slot = slotOf(id))
        {
            // NVRHI keeps the texture alive until command lists that use it have finished.
            slot->texture = nullptr;
            slot->bindings = nullptr;
            slot->live = false;
            freeSlots.push_back(static_cast<u32>(id & 0xFFFFFFFFu));
        }
    }

    nvrhi::IBindingSet* bindingsOf(u64 id)
    {
        Slot* slot = slotOf(id);
        if (slot == nullptr)
        {
            return nullptr;
        }
        if (!slot->bindings)
        {
            nvrhi::BindingSetDesc set;
            set.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(0, slot->texture),
                nvrhi::BindingSetItem::Sampler(0, sampler),
                nvrhi::BindingSetItem::PushConstants(0, sizeof(UiPush)),
            };
            slot->bindings = gpu->createBindingSet(set, uiLayout);
        }
        return slot->bindings;
    }

    const void* rgbaPixels(ImTextureData& texture)
    {
        if (texture.Format == ImTextureFormat_RGBA32)
        {
            return texture.GetPixels();
        }
        // Alpha-only textures become white with that alpha.
        const usize count = static_cast<usize>(texture.Width) * static_cast<usize>(texture.Height);
        expanded.resize(count * 4);
        const auto* alpha = static_cast<const u8*>(texture.GetPixels());
        for (usize i = 0; i < count; ++i)
        {
            expanded[i * 4 + 0] = 255;
            expanded[i * 4 + 1] = 255;
            expanded[i * 4 + 2] = 255;
            expanded[i * 4 + 3] = alpha[i];
        }
        return expanded.data();
    }

    void upload(nvrhi::ICommandList* commands, nvrhi::ITexture* target, ImTextureData& texture)
    {
        // The whole texture is written: updates are rare (new glyphs), and NVRHI writes whole subresources.
        commands->writeTexture(target, 0, 0, rgbaPixels(texture), static_cast<usize>(texture.Width) * 4);
    }

    void updateTexture(nvrhi::ICommandList* commands, ImTextureData& texture)
    {
        switch (texture.Status)
        {
        case ImTextureStatus_WantCreate:
        {
            nvrhi::TextureDesc textureDesc;
            textureDesc.width = static_cast<u32>(texture.Width);
            textureDesc.height = static_cast<u32>(texture.Height);
            textureDesc.format = nvrhi::Format::RGBA8_UNORM;
            textureDesc.debugName = "UI texture";
            textureDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
            nvrhi::TextureHandle created = gpu->createTexture(textureDesc);
            ILL_CHECK(created != nullptr, "cannot create a {}x{} UI texture", texture.Width, texture.Height);
            upload(commands, created, texture);
            texture.SetTexID(allocateSlot(created, kTagImGui));
            texture.SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantUpdates:
            if (Slot* slot = slotOf(texture.GetTexID()))
            {
                upload(commands, slot->texture, texture);
            }
            texture.SetStatus(ImTextureStatus_OK);
            break;
        case ImTextureStatus_WantDestroy:
            // Frames in flight may still sample it.
            if (static_cast<u32>(texture.UnusedFrames) >= frames.size())
            {
                releaseSlot(texture.GetTexID());
                texture.SetTexID(ImTextureID_Invalid);
                texture.SetStatus(ImTextureStatus_Destroyed);
            }
            break;
        case ImTextureStatus_OK:
        case ImTextureStatus_Destroyed:
            break;
        }
    }

    void ensureCapacity(FrameBuffers& frame, usize vertices, usize indices)
    {
        if (vertices > frame.vertexCapacity)
        {
            frame.vertexCapacity = grownCapacity(vertices, kMinVertices);
            nvrhi::BufferDesc bufferDesc;
            bufferDesc.byteSize = frame.vertexCapacity * sizeof(ImDrawVert);
            bufferDesc.isVertexBuffer = true;
            bufferDesc.debugName = "UI vertices";
            bufferDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer);
            frame.vertices = gpu->createBuffer(bufferDesc);
            ILL_CHECK(frame.vertices != nullptr, "cannot create the UI vertex buffer");
        }
        if (indices > frame.indexCapacity)
        {
            frame.indexCapacity = grownCapacity(indices, kMinIndices);
            nvrhi::BufferDesc bufferDesc;
            bufferDesc.byteSize = frame.indexCapacity * sizeof(ImDrawIdx);
            bufferDesc.isIndexBuffer = true;
            bufferDesc.debugName = "UI indices";
            bufferDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer);
            frame.indices = gpu->createBuffer(bufferDesc);
            ILL_CHECK(frame.indices != nullptr, "cannot create the UI index buffer");
        }
    }
};

UiRenderer::UiRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

std::unique_ptr<UiRenderer> UiRenderer::create(RenderDevice& device, UiContext& context,
                                               const UiRendererDesc& desc)
{
    std::unique_ptr<UiRenderer> renderer(new UiRenderer());
    Impl& impl = *renderer->m_impl;
    impl.device = &device;
    impl.gpu = device.device();
    impl.context = &context;
    impl.desc = desc;
    if (!impl.initialize())
    {
        ILL_LOG_ERROR("Editor", "cannot create the UI renderer");
        return nullptr;
    }
    context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    ILL_ASSERT(io.BackendRendererUserData == nullptr, "the context already has a renderer backend");
    io.BackendRendererUserData = renderer.get();
    io.BackendRendererName = "illusion_nvrhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = static_cast<int>(kMaxTextureSide);
    platform.Renderer_TextureMaxHeight = static_cast<int>(kMaxTextureSide);
    platform.DrawCallback_ResetRenderState = resetRenderState;
    return renderer;
}

UiRenderer::~UiRenderer()
{
    m_impl->context->makeCurrent();
    // Textures still known to Dear ImGui are recreated by the next renderer, if any.
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
    {
        if (texture->RefCount == 1 && (texture->GetTexID() & kTagMask) == kTagImGui)
        {
            m_impl->releaseSlot(texture->GetTexID());
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    ImGui::GetPlatformIO().DrawCallback_ResetRenderState = nullptr;
}

void UiRenderer::render(nvrhi::ICommandList* commands, nvrhi::IFramebuffer* framebuffer, ImDrawData* drawData)
{
    ILL_PROFILE_SCOPE();
    if (drawData == nullptr)
    {
        return;
    }
    Impl& impl = *m_impl;
    if (drawData->Textures != nullptr)
    {
        for (ImTextureData* texture : *drawData->Textures)
        {
            if (texture->Status != ImTextureStatus_OK)
            {
                impl.updateTexture(commands, *texture);
            }
        }
    }
    const f32 width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const f32 height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (width <= 0.0f || height <= 0.0f || drawData->TotalVtxCount == 0)
    {
        return;
    }

    Impl::FrameBuffers& frame = impl.frames[impl.frameIndex];
    impl.frameIndex = (impl.frameIndex + 1) % static_cast<u32>(impl.frames.size());
    impl.ensureCapacity(frame, static_cast<usize>(drawData->TotalVtxCount),
                        static_cast<usize>(drawData->TotalIdxCount));
    usize vertexOffset = 0;
    usize indexOffset = 0;
    for (const ImDrawList* list : drawData->CmdLists)
    {
        const usize vertexBytes = static_cast<usize>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const usize indexBytes = static_cast<usize>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        commands->writeBuffer(frame.vertices, list->VtxBuffer.Data, vertexBytes,
                              vertexOffset * sizeof(ImDrawVert));
        commands->writeBuffer(frame.indices, list->IdxBuffer.Data, indexBytes,
                              indexOffset * sizeof(ImDrawIdx));
        vertexOffset += static_cast<usize>(list->VtxBuffer.Size);
        indexOffset += static_cast<usize>(list->IdxBuffer.Size);
    }

    // Window coordinates to clip space (y up: NVRHI flips the Vulkan viewport).
    UiPush push{};
    push.scale[0] = 2.0f / drawData->DisplaySize.x;
    push.scale[1] = -2.0f / drawData->DisplaySize.y;
    push.translate[0] = -1.0f - drawData->DisplayPos.x * push.scale[0];
    push.translate[1] = 1.0f - drawData->DisplayPos.y * push.scale[1];

    nvrhi::GraphicsState state;
    state.pipeline = impl.uiPipeline(framebuffer);
    state.framebuffer = framebuffer;
    state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(frame.vertices).setSlot(0).setOffset(0)};
    state.indexBuffer =
        nvrhi::IndexBufferBinding()
            .setBuffer(frame.indices)
            .setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT)
            .setOffset(0);
    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    u32 globalVertex = 0;
    u32 globalIndex = 0;
    for (const ImDrawList* list : drawData->CmdLists)
    {
        for (const ImDrawCmd& command : list->CmdBuffer)
        {
            if (command.UserCallback != nullptr)
            {
                if (command.UserCallback != resetRenderState)
                {
                    command.UserCallback(list, &command);
                }
                continue;
            }
            const f32 x0 = std::max((command.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f);
            const f32 y0 = std::max((command.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f);
            const f32 x1 = std::min((command.ClipRect.z - clipOffset.x) * clipScale.x, width);
            const f32 y1 = std::min((command.ClipRect.w - clipOffset.y) * clipScale.y, height);
            if (x1 <= x0 || y1 <= y0)
            {
                continue;
            }
            nvrhi::IBindingSet* bindings = impl.bindingsOf(command.GetTexID());
            if (bindings == nullptr)
            {
                continue; // a texture that was unregistered while still drawn
            }
            state.bindings = {bindings};
            state.viewport = nvrhi::ViewportState()
                                 .addViewport(nvrhi::Viewport(width, height))
                                 .addScissorRect(nvrhi::Rect(static_cast<int>(x0), static_cast<int>(x1),
                                                             static_cast<int>(y0), static_cast<int>(y1)));
            commands->setGraphicsState(state);
            commands->setPushConstants(&push, sizeof(push));
            commands->drawIndexed(nvrhi::DrawArguments()
                                      .setVertexCount(command.ElemCount)
                                      .setStartIndexLocation(globalIndex + command.IdxOffset)
                                      .setStartVertexLocation(globalVertex + command.VtxOffset));
        }
        globalVertex += static_cast<u32>(list->VtxBuffer.Size);
        globalIndex += static_cast<u32>(list->IdxBuffer.Size);
    }
}

void UiRenderer::present(nvrhi::ICommandList* commands, nvrhi::ITexture* uiTarget,
                         nvrhi::IFramebuffer* output)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    nvrhi::IBindingSet* bindings = nullptr;
    for (const Impl::PresentSet& set : impl.presentSets)
    {
        if (set.source == uiTarget && set.bindings)
        {
            bindings = set.bindings;
        }
    }
    if (bindings == nullptr)
    {
        nvrhi::BindingSetDesc set;
        set.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, uiTarget),
            nvrhi::BindingSetItem::PushConstants(0, sizeof(PresentPush)),
        };
        Impl::PresentSet& slot = impl.presentSets[impl.nextPresentSet];
        impl.nextPresentSet = (impl.nextPresentSet + 1) % 2;
        slot.source = uiTarget;
        slot.bindings = impl.gpu->createBindingSet(set, impl.presentLayout);
        bindings = slot.bindings;
    }
    const nvrhi::FramebufferInfoEx& info = output->getFramebufferInfo();
    PresentPush push{};
    push.decodeSrgb =
        !info.colorFormats.empty() && nvrhi::getFormatInfo(info.colorFormats[0]).isSRGB ? 1u : 0u;
    nvrhi::GraphicsState state;
    state.pipeline = impl.presentPipeline(output);
    state.framebuffer = output;
    state.bindings = {bindings};
    state.viewport.addViewportAndScissorRect(info.getViewport());
    commands->setGraphicsState(state);
    commands->setPushConstants(&push, sizeof(push));
    commands->draw(nvrhi::DrawArguments().setVertexCount(3));
}

u64 UiRenderer::registerTexture(nvrhi::ITexture* texture)
{
    ILL_ASSERT(texture != nullptr, "registerTexture() with a null texture");
    return m_impl->allocateSlot(texture, kTagRegistered);
}

void UiRenderer::unregisterTexture(u64 textureId)
{
    ILL_ASSERT((textureId & kTagMask) == kTagRegistered, "unregisterTexture() with an id it did not return");
    m_impl->releaseSlot(textureId);
}

u32 UiRenderer::textureCount() const noexcept
{
    return static_cast<u32>(std::count_if(m_impl->slots.begin(), m_impl->slots.end(),
                                          [](const Impl::Slot& slot) { return slot.live; }));
}

} // namespace illusion::editor::ui
