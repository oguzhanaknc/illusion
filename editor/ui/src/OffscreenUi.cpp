#include <illusion/core/Log.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiRenderer.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <cstring>

namespace illusion::editor::ui
{

namespace
{

nvrhi::TextureDesc targetDesc(UVec2 size, nvrhi::Format format, const char* name)
{
    nvrhi::TextureDesc desc;
    desc.width = size.x;
    desc.height = size.y;
    desc.format = format;
    desc.isRenderTarget = true;
    desc.isShaderResource = true;
    desc.debugName = name;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return desc;
}

} // namespace

std::unique_ptr<OffscreenUi> OffscreenUi::create(RenderDevice& device, UiContext& context, UVec2 size)
{
    std::unique_ptr<OffscreenUi> offscreen(new OffscreenUi());
    offscreen->m_device = &device;
    offscreen->m_size = size;
    offscreen->m_renderer = UiRenderer::create(device, context);
    nvrhi::IDevice* gpu = device.device();
    offscreen->m_target = gpu->createTexture(targetDesc(size, nvrhi::Format::RGBA8_UNORM, "UI target"));
    if (!offscreen->m_renderer || !offscreen->m_target)
    {
        ILL_LOG_ERROR("Gallery", "cannot create the offscreen UI target");
        return nullptr;
    }
    offscreen->m_framebuffer =
        gpu->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(offscreen->m_target));
    offscreen->m_commands = gpu->createCommandList();
    return offscreen;
}

OffscreenUi::~OffscreenUi() = default;

bool OffscreenUi::resize(UVec2 size)
{
    if (size == m_size && m_target)
    {
        return true;
    }
    nvrhi::IDevice* gpu = m_device->device();
    nvrhi::TextureHandle target =
        gpu->createTexture(targetDesc(size, nvrhi::Format::RGBA8_UNORM, "UI target"));
    if (!target)
    {
        return false;
    }
    m_target = std::move(target);
    m_framebuffer = gpu->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_target));
    m_size = size;
    return true;
}

void OffscreenUi::renderTo(ImDrawData* drawData, Vec4 clear, nvrhi::IFramebuffer* output)
{
    m_commands->open();
    draw(m_commands, drawData, clear);
    if (output != nullptr)
    {
        m_renderer->present(m_commands, m_target, output);
    }
    m_commands->close();
    m_device->device()->executeCommandList(m_commands);
}

std::optional<Image> OffscreenUi::capture()
{
    m_commands->open();
    return readBack(m_commands, m_target);
}

std::optional<Image> OffscreenUi::render(ImDrawData* drawData, Vec4 clear)
{
    m_commands->open();
    draw(m_commands, drawData, clear);
    return readBack(m_commands, m_target);
}

std::optional<Image> OffscreenUi::renderPresented(ImDrawData* drawData, Vec4 clear,
                                                  nvrhi::Format outputFormat)
{
    nvrhi::IDevice* gpu = m_device->device();
    nvrhi::TextureHandle output = gpu->createTexture(targetDesc(m_size, outputFormat, "UI present output"));
    nvrhi::FramebufferHandle framebuffer =
        gpu->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(output));
    m_commands->open();
    draw(m_commands, drawData, clear);
    m_renderer->present(m_commands, m_target, framebuffer);
    return readBack(m_commands, output);
}

void OffscreenUi::draw(nvrhi::ICommandList* commands, ImDrawData* drawData, Vec4 clear)
{
    commands->clearTextureFloat(m_target, nvrhi::AllSubresources,
                                nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
    m_renderer->render(commands, m_framebuffer, drawData);
}

std::optional<Image> OffscreenUi::readBack(nvrhi::ICommandList* commands, nvrhi::ITexture* texture)
{
    nvrhi::IDevice* gpu = m_device->device();
    nvrhi::TextureDesc stagingDesc = texture->getDesc();
    stagingDesc.isRenderTarget = false;
    stagingDesc.isShaderResource = false;
    stagingDesc.debugName = "UI readback";
    nvrhi::StagingTextureHandle staging = gpu->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
    if (!staging)
    {
        commands->close();
        return std::nullopt;
    }
    commands->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
    commands->close();
    gpu->executeCommandList(commands);
    gpu->waitForIdle();
    m_device->collectGarbage();

    usize rowPitch = 0;
    const auto* bytes = static_cast<const std::byte*>(
        gpu->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
    if (bytes == nullptr)
    {
        return std::nullopt;
    }
    Image image;
    image.format = ImageFormat::RGBA8Srgb;
    const usize row = static_cast<usize>(m_size.x) * 4;
    image.data.resize(row * m_size.y);
    image.mips.push_back(ImageMip{m_size.x, m_size.y, 0, image.data.size()});
    for (u32 y = 0; y < m_size.y; ++y)
    {
        std::memcpy(image.data.data() + row * y, bytes + rowPitch * y, row);
    }
    gpu->unmapStagingTexture(staging);
    return image;
}

} // namespace illusion::editor::ui
