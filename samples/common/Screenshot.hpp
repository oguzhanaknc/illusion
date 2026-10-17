#pragma once

// Offscreen screenshot shared by the samples: renders the World at 1280x720 and saves it as PNG.

#include <illusion/assets/Image.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace sample
{

using namespace illusion;

inline bool saveScreenshot(RenderDevice& device, RenderResources& resources, World& world,
                           const Transform& camera, JobSystem& jobs, const std::string& path)
{
    constexpr u32 kWidth = 1280;
    constexpr u32 kHeight = 720;
    nvrhi::IDevice* gpu = device.device();
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM; // the shader encodes sRGB for UNORM targets
    auto renderer = SceneRenderer::create(device, resources, desc);
    if (!renderer)
    {
        return false;
    }
    nvrhi::TextureDesc targetDesc;
    targetDesc.width = kWidth;
    targetDesc.height = kHeight;
    targetDesc.format = desc.colorFormat;
    targetDesc.isRenderTarget = true;
    targetDesc.debugName = "Screenshot";
    targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
    nvrhi::TextureHandle target = gpu->createTexture(targetDesc);
    nvrhi::TextureDesc readbackDesc = targetDesc;
    readbackDesc.isRenderTarget = false;
    nvrhi::StagingTextureHandle readback =
        gpu->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    renderer->render(commands, target, world, RenderView::fromCamera(camera, Camera{}, 1280.0f / 720.0f),
                     &jobs);
    commands->copyTexture(readback, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
    commands->close();
    gpu->executeCommandList(commands);
    gpu->waitForIdle();

    Image image = makeSolidImage(kWidth, kHeight, Vec4(0.0f), ImageFormat::RGBA8Srgb);
    size_t rowPitch = 0;
    const auto* bytes = static_cast<const std::byte*>(
        gpu->mapStagingTexture(readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
    for (u32 y = 0; y < kHeight; ++y)
    {
        std::memcpy(image.data.data() + static_cast<usize>(y) * kWidth * 4, bytes + y * rowPitch, kWidth * 4);
    }
    gpu->unmapStagingTexture(readback);

    const std::vector<std::byte> png = encodePng(image);
    std::ofstream file(utf8Path(path), std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    ILL_LOG_INFO("Sample", "saved {}", path);
    return static_cast<bool>(file);
}

} // namespace sample
