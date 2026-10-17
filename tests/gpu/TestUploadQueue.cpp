#include "assets/AssetTestProject.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/UploadQueue.hpp>

#include <doctest/doctest.h>

using namespace illusion;

TEST_CASE("the upload queue uploads streamed models within a byte budget")
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    auto device = RenderDevice::create(desc);
    REQUIRE(device != nullptr);
    {
        test::AssetProject project;
        JobSystem jobs(JobSystemDesc{2, 256});
        AssetStreamer streamer(project.files, jobs);
        RenderResources resources(*device);
        UploadQueue queue(resources);

        const std::shared_ptr<const ModelUpload> first =
            queue.upload(streamer.loadModel("game:/scene/two_nodes.gltf"));
        const AssetHandle<ModelData> glb = streamer.loadModel("game:/binary.glb");
        const std::shared_ptr<const ModelUpload> second = queue.upload(glb);
        const std::shared_ptr<const ModelUpload> broken =
            queue.upload(streamer.loadModel("game:/missing.gltf"));
        CHECK(queue.process(u64{1} << 30) == 0); // nothing has finished loading yet
        CHECK(queue.pending() == 3);

        streamer.waitIdle();
        // A tiny budget still uploads one model per call, in request order (failures finish immediately).
        CHECK(queue.process(1) == 1);
        CHECK(first->uploaded);
        CHECK_FALSE(second->uploaded);
        CHECK(queue.process(1) == 2);
        CHECK(second->uploaded);
        CHECK(broken->failed);
        CHECK_FALSE(broken->uploaded);
        CHECK(queue.pending() == 0);

        REQUIRE(first->handles.meshes.size() == 1);
        CHECK(resources.submeshCount(first->handles.meshes[0]) == 1);
        CHECK(first->handles.materials.size() == 1);
        CHECK(UploadQueue::uploadSize(glb.value()) > 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
