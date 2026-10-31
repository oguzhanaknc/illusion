// Benchmarks of the RenderAssetResolver. It needs RenderResources (and so a Vulkan device), but measures
// only CPU work: checking a World whose renderers are all resolved.

#include "Budget.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>

#include <memory>

using namespace illusion;

// Budgets are roughly 5-10x a desktop result (see Budget.hpp).

ILL_BENCHMARK("render/asset-resolve-steady", 5.0)
{
    constexpr u32 kCount = 100'000;
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Disabled;
    const std::unique_ptr<RenderDevice> device = RenderDevice::create(desc);
    ILL_CHECK(device != nullptr, "the GPU benchmarks need a Vulkan 1.3 device");
    {
        JobSystem jobs;
        VirtualFileSystem files;
        AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""});
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer);
        RenderAssetResolver resolver(cache);
        World world;
        const AssetRef meshes[] = {AssetRef("builtin:/mesh/cube"), AssetRef("builtin:/mesh/sphere")};
        for (u32 i = 0; i < kCount; ++i)
        {
            Transform transform;
            transform.position = Vec3(static_cast<f32>(i % 100), 0.0f, static_cast<f32>(i / 100));
            world.create(transform, MeshRenderer{.mesh = meshes[i % 2]});
        }
        resolver.waitIdle(world, &jobs);
        resolver.update(world, &jobs);
        bench.batch(kCount)
            .unit("renderer")
            .minEpochIterations(200)
            .run("100k resolved MeshRenderers checked on the Job System",
                 [&] { resolver.update(world, &jobs); });
    }
    device->waitIdle();
}

ILL_BENCHMARK("render/asset-cache-steady", 50.0)
{
    // The per-frame cost of a cache with nothing to load or reload: RenderResources::collect() included.
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Disabled;
    const std::unique_ptr<RenderDevice> device = RenderDevice::create(desc);
    ILL_CHECK(device != nullptr, "the GPU benchmarks need a Vulkan 1.3 device");
    {
        JobSystem jobs;
        VirtualFileSystem files;
        AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""});
        RenderResources resources(*device);
        RenderAssetCache cache(resources, streamer);
        RenderAssetResolver resolver(cache);
        World world;
        world.create(Transform{}, MeshRenderer{.mesh = AssetRef("builtin:/mesh/cube")});
        resolver.waitIdle(world, &jobs);
        bench.unit("update").minEpochIterations(10'000).run("RenderAssetCache::update with nothing to do",
                                                            [&] { cache.update(); });
    }
    device->waitIdle();
}

ILL_BENCHMARK("render/update-material", 500'000.0)
{
    // Live material editing: one updateMaterial() per frame with new factors (no new binding set).
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Disabled;
    const std::unique_ptr<RenderDevice> device = RenderDevice::create(desc);
    ILL_CHECK(device != nullptr, "the GPU benchmarks need a Vulkan 1.3 device");
    {
        RenderResources resources(*device);
        MaterialDesc material;
        const MaterialHandle handle = resources.createMaterial(material);
        f32 roughness = 0.0f;
        bench.unit("update").minEpochIterations(200).run("RenderResources::updateMaterial", [&] {
            roughness = roughness >= 1.0f ? 0.0f : roughness + 0.01f;
            material.parameters.roughnessFactor = roughness;
            resources.updateMaterial(handle, material);
        });
    }
    device->waitIdle();
}
