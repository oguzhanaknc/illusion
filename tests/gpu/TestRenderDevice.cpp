#include <illusion/render/DebugTriangle.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>
#include <utility>

using namespace illusion;

// GPU tests need a Vulkan 1.3 implementation. CI runs them on Linux with Mesa's lavapipe (software
// rendering); locally they run on the installed GPU. They are labelled "gpu" in CTest.

namespace
{

RenderDeviceDesc headlessDesc()
{
    RenderDeviceDesc desc;
    desc.applicationName = "illusion GPU tests";
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

struct Pixel
{
    int r;
    int g;
    int b;
    int a;
};

} // namespace

TEST_CASE("a headless RenderDevice selects a Vulkan 1.3 GPU")
{
    auto device = RenderDevice::create(headlessDesc());
    REQUIRE(device != nullptr);
    CHECK(device->device() != nullptr);
    CHECK_FALSE(device->canPresent());
    CHECK_FALSE(device->gpu().name.empty());
    CHECK(device->gpu().apiVersion >= ((1u << 22) | (3u << 12))); // VK_API_VERSION_1_3
    const std::string validation = device->isValidationEnabled() ? " with validation" : "";
    MESSAGE("GPU: ", device->gpu().name, " (", device->gpu().driverName, ")", validation);
    device->waitIdle();
    device->collectGarbage();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the debug triangle renders the expected pixels")
{
    auto device = RenderDevice::create(headlessDesc());
    REQUIRE(device != nullptr);
    nvrhi::IDevice* gpu = device->device();
    {
        constexpr u32 kSize = 64;
        nvrhi::TextureDesc targetDesc;
        targetDesc.width = kSize;
        targetDesc.height = kSize;
        targetDesc.format = nvrhi::Format::RGBA8_UNORM;
        targetDesc.isRenderTarget = true;
        targetDesc.debugName = "Test render target";
        targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        nvrhi::TextureHandle target = gpu->createTexture(targetDesc);
        REQUIRE(target);
        nvrhi::FramebufferHandle framebuffer =
            gpu->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));

        auto triangle = DebugTriangle::create(*device, framebuffer->getFramebufferInfo());
        REQUIRE(triangle != nullptr);

        nvrhi::TextureDesc readbackDesc = targetDesc;
        readbackDesc.isRenderTarget = false;
        readbackDesc.debugName = "Test readback";
        nvrhi::StagingTextureHandle readback =
            gpu->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);
        REQUIRE(readback);

        nvrhi::CommandListHandle commands = gpu->createCommandList();
        commands->open();
        commands->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0.1f, 0.2f, 0.3f, 1.0f));
        triangle->draw(commands, framebuffer);
        commands->copyTexture(readback, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
        commands->close();
        gpu->executeCommandList(commands);
        gpu->waitForIdle();

        size_t rowPitch = 0;
        const auto* bytes = static_cast<const u8*>(
            gpu->mapStagingTexture(readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
        REQUIRE(bytes != nullptr);
        const auto pixel = [&](u32 x, u32 y) {
            const u8* p = bytes + static_cast<usize>(y) * rowPitch + static_cast<usize>(x) * 4;
            return Pixel{p[0], p[1], p[2], p[3]};
        };

        // Corners keep the clear colour (0.1, 0.2, 0.3) -> (26, 51, 77) with rounding tolerance.
        for (const auto [x, y] :
             {std::pair{1u, 1u}, std::pair{62u, 1u}, std::pair{1u, 62u}, std::pair{62u, 62u}})
        {
            const Pixel p = pixel(x, y);
            CHECK(std::abs(p.r - 26) <= 1);
            CHECK(std::abs(p.g - 51) <= 1);
            CHECK(std::abs(p.b - 77) <= 1);
            CHECK(p.a == 255);
        }

        // Near each vertex its colour dominates: red at the top, green bottom right, blue bottom left.
        const Pixel top = pixel(32, 16);
        CHECK(top.r > 150);
        CHECK(top.r > top.g + 50);
        CHECK(top.r > top.b + 50);
        const Pixel bottomRight = pixel(47, 49);
        CHECK(bottomRight.g > bottomRight.r);
        CHECK(bottomRight.g > bottomRight.b);
        const Pixel bottomLeft = pixel(16, 49);
        CHECK(bottomLeft.b > bottomLeft.r);
        CHECK(bottomLeft.b > bottomLeft.g);

        gpu->unmapStagingTexture(readback);
    }
    device->waitIdle();
    device->collectGarbage();
    CHECK(device->validationErrorCount() == 0);
}
