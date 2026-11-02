#include "Budget.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/viewport/EditorCamera.hpp>
#include <illusion/editor/viewport/PickCandidates.hpp>
#include <illusion/editor/viewport/ThumbnailRenderer.hpp>
#include <illusion/editor/viewport/Visualisers.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::viewport;

// One frame of viewport navigation (an orbit drag) through EditorCamera::update(). Budget about 10x the
// 20 ns measured on a desktop (Release, MSVC).
ILL_BENCHMARK("viewport/camera-update", 200.0)
{
    EditorCamera camera;
    ViewportInput input;
    input.hovered = true;
    input.alt = true;
    input.buttons[0] = true;
    input.cursorDelta = Vec2(1.5f, -0.5f);
    input.deltaSeconds = 1.0f / 60.0f;
    bench.run("orbit frame",
              [&] { ankerl::nanobench::doNotOptimizeAway(camera.update(input, UVec2(1920, 1080))); });
}

// Ordering the entities of a 5 x 5 pick window with four distinct entities. Budget about 8x the 127 ns
// measured on a desktop (Release, MSVC).
ILL_BENCHMARK("viewport/pick-candidates", 1'000.0)
{
    std::vector<EntityIdTexel> texels(25);
    for (u32 i = 0; i < 25; ++i)
    {
        texels[i] = EntityIdTexel{i % 5 == 0 ? ~0u : i % 4, 1};
    }
    std::vector<Entity> candidates;
    candidates.reserve(25);
    bench.run("5x5 window", [&] {
        candidates.clear();
        appendVisibleCandidates(texels, UVec2(5, 5), UVec2(2, 2), candidates);
        ankerl::nanobench::doNotOptimizeAway(candidates.data());
    });
}

// Recording the DebugDraw visualisers of 1,200 objects: box, sphere and capsule colliders, point lights,
// cameras and hinge joints in equal numbers, about 36 lines per object. Budget per object from the M7o plan;
// measured 396 ns on a desktop, most of it DebugDraw::line itself (render/debugdraw-record, ~8 ns per line).
ILL_BENCHMARK("viewport/visualisers-record", 500.0)
{
    constexpr u32 kPerKind = 200;
    World world;
    for (u32 i = 0; i < kPerKind; ++i)
    {
        const f32 x = static_cast<f32>(i);
        const auto spawn = [&](f32 z) {
            const Entity entity = world.create();
            world.set(entity, Transform{Vec3(x, 0.0f, z), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(1.0f)});
            return entity;
        };
        for (const ColliderShape shape : {ColliderShape::Box, ColliderShape::Sphere, ColliderShape::Capsule})
        {
            Collider collider;
            collider.shape = shape;
            world.set(spawn(static_cast<f32>(shape)), collider);
        }
        world.set(spawn(4.0f), PointLight{});
        world.set(spawn(5.0f), Camera{});
        Joint joint;
        joint.type = JointType::Hinge;
        joint.limited = true;
        joint.minLimit = -1.0f;
        joint.maxLimit = 1.0f;
        world.set(spawn(6.0f), joint);
    }
    DebugDraw draw(1u << 20);
    VisualiserOptions options;
    options.all = true;
    bench.batch(kPerKind * 6).unit("object").run("1,200 objects", [&] {
        draw.clear();
        ankerl::nanobench::doNotOptimizeAway(recordVisualisers(world, draw, options));
    });
}

// The thumbnail of a 1,024 x 1,024 sRGB texture with a full mip chain (scaled from its 256 x 256 mip over the
// checkerboard), per thumbnail: the CPU step of a texture thumbnail in its frame (the plan allows 2 ms).
// Budget about 8x the 0.64 ms measured on a desktop (Release, MSVC).
ILL_BENCHMARK("thumbnail/texture", 5'000'000.0)
{
    Image image;
    image.format = ImageFormat::RGBA8Srgb;
    usize offset = 0;
    for (u32 size = 1024; size >= 1; size /= 2)
    {
        const usize bytes = static_cast<usize>(size) * size * 4;
        image.mips.push_back(ImageMip{size, size, offset, bytes});
        offset += bytes;
    }
    image.data.resize(offset);
    for (usize i = 0; i < image.data.size(); ++i)
    {
        image.data[i] = static_cast<std::byte>(i * 7);
    }
    bench.unit("thumbnail").run("textureThumbnail (1024 x 1024 with mips)", [&] {
        ankerl::nanobench::doNotOptimizeAway(textureThumbnail(image));
    });
}
