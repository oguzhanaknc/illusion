// The image of a saved slot from a GPU frame (ADR 0058): FrameCapture reads a rendered target back, scales it
// to 320 x 180, and the save thread writes it as the slot's PNG.

#include "TestSupport.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/FrameCapture.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>
#include <illusion/save/SaveSystem.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <vector>

using namespace illusion;

namespace
{

RenderDeviceDesc headlessDesc()
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

// A 640 x 360 frame cleared to red, its right half drawn blue by a second clear of a copy.
nvrhi::TextureHandle renderFrame(RenderDevice& device)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::TextureDesc desc;
    desc.width = 640;
    desc.height = 360;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.isRenderTarget = true;
    desc.debugName = "Save thumbnail frame";
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    nvrhi::TextureHandle frame = gpu->createTexture(desc);
    nvrhi::TextureDesc halfDesc = desc;
    halfDesc.width = 320;
    halfDesc.debugName = "Save thumbnail half";
    nvrhi::TextureHandle half = gpu->createTexture(halfDesc);
    REQUIRE(frame != nullptr);
    REQUIRE(half != nullptr);
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    commands->clearTextureFloat(frame, nvrhi::AllSubresources, nvrhi::Color(1.0f, 0.0f, 0.0f, 1.0f));
    commands->clearTextureFloat(half, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 1.0f, 1.0f));
    commands->copyTexture(frame, nvrhi::TextureSlice().setOrigin(320, 0).setSize(320, 360, 1), half,
                          nvrhi::TextureSlice().setSize(320, 360, 1));
    commands->close();
    gpu->executeCommandList(commands);
    return frame;
}

} // namespace

TEST_CASE("a slot saved with a rendered frame gets a 320 x 180 PNG of it")
{
    auto device = RenderDevice::create(headlessDesc());
    REQUIRE(device != nullptr);
    const std::unique_ptr<FrameCapture> capture = FrameCapture::create(*device);
    REQUIRE(capture != nullptr);
    nvrhi::TextureHandle frame = renderFrame(*device);

    reflectCoreComponents();
    registerSaveComponents();
    test::TempDirectory directory;
    VirtualFileSystem files;
    REQUIRE(files.mount("user", directory.path, MountAccess::ReadWrite) == FileError::None);
    World world;
    world.create(Name{"Hero"}, Transform{}, Saved{});
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());

    SaveSystemDesc desc;
    desc.files = &files;
    desc.thumbnail = [&capture, &frame](SaveThumbnail& thumbnail) {
        thumbnail.width = kSaveThumbnailWidth;
        thumbnail.height = kSaveThumbnailHeight;
        return capture->readScaled(frame, {kSaveThumbnailWidth, kSaveThumbnailHeight}, thumbnail.rgba);
    };
    {
        SaveSystem saves(desc);
        REQUIRE(saves.write("slot1", SaveMetadata{"GPU", 0.0, 0, {}}, 1, captured->world).hasValue());
        saves.waitIdle();
        std::vector<SaveSlotRecord> slots;
        saves.copySlots(slots);
        REQUIRE(slots.size() == 1);
        CHECK(slots[0].screenshot == "user:/saves/slot1.png");
    }

    std::vector<std::byte> png;
    REQUIRE(files.readFile("user:/saves/slot1.png", png) == FileError::None);
    const std::optional<Image> image = decodeImage(png, true);
    REQUIRE(image.has_value());
    REQUIRE(image->width() == kSaveThumbnailWidth);
    REQUIRE(image->height() == kSaveThumbnailHeight);
    const auto pixel = [&image](u32 x, u32 y, u32 channel) {
        return static_cast<u8>(image->data.at((static_cast<usize>(y) * image->width() + x) * 4 + channel));
    };
    CHECK(pixel(10, 90, 0) == 255);
    CHECK(pixel(10, 90, 2) == 0);
    CHECK(pixel(310, 90, 0) == 0);
    CHECK(pixel(310, 90, 2) == 255);
    CHECK(pixel(160, 0, 3) == 255);
    CHECK(device->validationErrorCount() == 0);
}
