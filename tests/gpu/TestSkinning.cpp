// GPU skinning of animated meshes (ADR 0059): posed golden images, CPU equivalence, motion vectors, culling
// by the pose's bounds, shadows of the posed shape, allocation and the cost of a crowd.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/packing.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kRigged = "game:/rigged.gltf";
constexpr std::string_view kCrowd = "game:/crowd.gltf";
constexpr u32 kGoldenSize = 64;
// GPU time of the skinning pass for 300 instances of 10,240 vertices: ADR 0059 asks for under 1.5 ms on a
// mid-range desktop GPU. An RTX 4070 SUPER measures 0.66 ms; the budget is about 5x that. CPU
// implementations (lavapipe in CI) get a budget of their own.
constexpr f64 kCrowdBudgetMs = 3.0;
constexpr f64 kSoftwareCrowdBudgetMs = 2000.0;

SceneRendererDesc description()
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.entityIds = true;
    desc.bindless = true;
    desc.gpuTimers = true;
    desc.clearColor = Vec4(0, 0, 0, 1);
    desc.shadows.resolution = 256;
    desc.post.temporalAntiAliasing.enabled = false;
    desc.post.bloom.enabled = false;
    desc.post.ambientOcclusion.enabled = false;
    return desc;
}

nvrhi::TextureHandle targetFor(RenderDevice& device, u32 size)
{
    nvrhi::TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.isRenderTarget = true;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
    return device.device()->createTexture(desc);
}

// An orthographic view of @p height world units centred on @p centre, looking down -z.
RenderView orthographicView(Vec2 centre, f32 height)
{
    Camera camera;
    camera.projection = CameraProjection::Orthographic;
    camera.orthoHeight = height;
    camera.farPlane = 30;
    Transform transform;
    transform.position = Vec3(centre, 10);
    return RenderView::fromCamera(transform, camera, 1);
}

// The pixel of a square target of @p size showing world point @p point in orthographicView(centre, height).
UVec2 pixelOf(Vec2 point, Vec2 centre, f32 height, u32 size)
{
    const f32 scale = static_cast<f32>(size) / height;
    return UVec2(static_cast<u32>((point.x - centre.x + height * 0.5f) * scale),
                 static_cast<u32>((centre.y + height * 0.5f - point.y) * scale));
}

template <class T>
T readPixel(ReadbackRing& ring, nvrhi::ITexture* texture, UVec2 pixel)
{
    T value{};
    const auto ticket = ring.request(texture, pixel, UVec2(1));
    REQUIRE(ticket.has_value());
    REQUIRE(ring.read(*ticket, std::as_writable_bytes(std::span(&value, 1)), true) == ReadbackStatus::Ready);
    return value;
}

std::vector<std::byte> pixels(ReadbackRing& ring, nvrhi::ITexture* texture)
{
    std::vector<std::byte> result(static_cast<usize>(texture->getDesc().width) * texture->getDesc().height *
                                  4);
    const auto ticket = ring.request(texture);
    REQUIRE(ticket.has_value());
    REQUIRE(ring.read(*ticket, result, true) == ReadbackStatus::Ready);
    return result;
}

u32 brightness(u32 rgba)
{
    return (rgba & 0xFFu) + ((rgba >> 8) & 0xFFu) + ((rgba >> 16) & 0xFFu);
}

// Compares with tests/data/render/<name>.png like TestRendererExtensions.cpp: at most 1% of the pixels may
// differ by more than 12 in a channel. ILLUSION_UPDATE_GOLDENS=1 rewrites the golden.
void golden(std::string_view name, const std::vector<std::byte>& data)
{
    Image image;
    image.mips.push_back({kGoldenSize, kGoldenSize, 0, data.size()});
    image.data = data;
    const auto encoded = encodePng(image);
    const auto write = [&](const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        REQUIRE(stream.good());
    };
    const auto filename = std::string(name) + ".png";
    write(std::filesystem::path("render-captures") / filename);
    const auto reference = utf8Path(ILLUSION_TEST_DATA_DIR) / "render" / filename;
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    if (update && std::string_view(update) == "1")
    {
        write(reference);
    }
    std::ifstream stream(reference, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), reference.string());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto expected = decodeImage(std::as_bytes(std::span(bytes)), false);
    REQUIRE(expected.has_value());
    REQUIRE(expected->data.size() == data.size());
    usize different = 0;
    for (usize i = 0; i < data.size(); i += 4)
    {
        bool differs = false;
        for (usize channel = 0; channel < 3; ++channel)
        {
            differs |= std::abs(std::to_integer<int>(data[i + channel]) -
                                std::to_integer<int>(expected->data[i + channel])) > 12;
        }
        different += differs ? 1u : 0u;
    }
    CHECK(different <= kGoldenSize * kGoldenSize / 100);
}

std::vector<std::byte> readBuffer(RenderDevice& device, nvrhi::IBuffer* buffer, u64 offset, u64 size)
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::BufferDesc desc;
    desc.byteSize = size;
    desc.cpuAccess = nvrhi::CpuAccessMode::Read;
    desc.debugName = "Skinning readback";
    nvrhi::BufferHandle staging = gpu->createBuffer(desc);
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    commands->copyBuffer(staging, 0, buffer, offset, size);
    commands->close();
    gpu->executeCommandList(commands);
    device.waitIdle();
    std::vector<std::byte> result(size);
    const void* mapped = gpu->mapBuffer(staging, nvrhi::CpuAccessMode::Read);
    REQUIRE(mapped != nullptr);
    std::memcpy(result.data(), mapped, size);
    gpu->unmapBuffer(staging);
    return result;
}

// What Skinning.slang computes: the weighted skin matrices applied to a point (w = 1) or a direction (w = 0).
Vec3 skinned(std::span<const SkinMatrix> matrices, const SkinVertex& skin, Vec3 value, f32 w)
{
    Vec3 result(0.0f);
    for (usize i = 0; i < kMaxJointInfluences; ++i)
    {
        const f32 weight = static_cast<f32>(skin.weights[i]) / 65535.0f;
        const SkinMatrix& matrix = matrices[skin.joints[i]];
        for (glm::length_t row = 0; row < 3; ++row)
        {
            result[row] += weight * glm::dot(matrix.rows[static_cast<usize>(row)], Vec4(value, w));
        }
    }
    return result;
}

// A UV sphere of 10,240 vertices on a chain of three joints along y (-1, 0, 1) with the clip "bend" (the
// middle joint turns 45 degrees about z in one second), written next to the test models.
void writeCrowdModel(VirtualFileSystem& files)
{
    const MeshData sphere = makeSphere(1.0f, 127, 79);
    const usize count = sphere.vertices.size();
    std::vector<std::byte> data;
    std::string views;
    const auto append = [&](const void* bytes, usize size) {
        const usize offset = data.size();
        data.resize(offset + size);
        std::memcpy(data.data() + offset, bytes, size);
        views += std::format(R"({}{{"buffer": 0, "byteOffset": {}, "byteLength": {}}})",
                             views.empty() ? "" : ",", offset, size);
    };
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::array<u16, 4>> joints;
    std::vector<Vec4> weights;
    for (const Vertex& vertex : sphere.vertices)
    {
        positions.push_back(vertex.position);
        normals.push_back(vertex.normal);
        const f32 y = vertex.position.y;
        joints.push_back({0, 1, 2, 0});
        weights.push_back(y < 0.0f ? Vec4(-y, 1.0f + y, 0.0f, 0.0f) : Vec4(0.0f, 1.0f - y, y, 0.0f));
    }
    append(positions.data(), count * sizeof(Vec3));
    append(normals.data(), count * sizeof(Vec3));
    append(joints.data(), count * sizeof(joints[0]));
    append(weights.data(), count * sizeof(Vec4));
    append(sphere.indices.data(), sphere.indices.size() * sizeof(u32));
    const std::array<Mat4, 3> inverseBinds{glm::translate(Mat4(1.0f), Vec3(0, 1, 0)), Mat4(1.0f),
                                           glm::translate(Mat4(1.0f), Vec3(0, -1, 0))};
    append(inverseBinds.data(), sizeof(inverseBinds));
    const std::array<f32, 2> times{0.0f, 1.0f};
    append(times.data(), sizeof(times));
    const f32 half = 0.3926991f; // 22.5 degrees
    const std::array<Vec4, 2> rotations{Vec4(0, 0, 0, 1), Vec4(0, 0, std::sin(half), std::cos(half))};
    append(rotations.data(), sizeof(rotations));
    REQUIRE(files.writeFile("game:/crowd.bin", data) == FileError::None);
    const std::string gltf = std::format(
        R"({{"asset": {{"version": "2.0"}}, "scene": 0, "scenes": [{{"nodes": [0, 3]}}],
"nodes": [{{"name": "j0", "translation": [0, -1, 0], "children": [1]}}, {{"name": "j1", "translation": [0, 1, 0], "children": [2]}},
{{"name": "j2", "translation": [0, 1, 0]}}, {{"name": "body", "mesh": 0, "skin": 0}}],
"skins": [{{"joints": [0, 1, 2], "inverseBindMatrices": 5}}],
"meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3}}, "indices": 4}}]}}],
"animations": [{{"name": "bend", "samplers": [{{"input": 6, "output": 7, "interpolation": "LINEAR"}}],
"channels": [{{"sampler": 0, "target": {{"node": 1, "path": "rotation"}}}}]}}],
"buffers": [{{"uri": "crowd.bin", "byteLength": {}}}],
"bufferViews": [{}],
"accessors": [{{"bufferView": 0, "componentType": 5126, "count": {}, "type": "VEC3", "min": [-1, -1, -1], "max": [1, 1, 1]}},
{{"bufferView": 1, "componentType": 5126, "count": {}, "type": "VEC3"}},
{{"bufferView": 2, "componentType": 5123, "count": {}, "type": "VEC4"}},
{{"bufferView": 3, "componentType": 5126, "count": {}, "type": "VEC4"}},
{{"bufferView": 4, "componentType": 5125, "count": {}, "type": "SCALAR"}},
{{"bufferView": 5, "componentType": 5126, "count": 3, "type": "MAT4"}},
{{"bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]}},
{{"bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC4"}}]}})",
        data.size(), views, count, count, count, count, sphere.indices.size());
    REQUIRE(files.writeText("game:/crowd.gltf", gltf) == FileError::None);
}

// Assets, animation and a renderer for one test; @p device must outlive it.
struct Rig
{
    explicit Rig(RenderDevice& device)
        : gpu(device)
        , resources(device)
        , cache(resources, streamer)
        , resolver(cache)
        , renderer(SceneRenderer::create(device, resources, description()))
    {
        REQUIRE(renderer);
    }

    // The GPU may still run the last frame, whose binding sets the renderer owns.
    ~Rig() { gpu.waitIdle(); }

    // A skinned renderer of @p model's mesh 0 playing @p clip from @p time (not advancing unless asked).
    Entity character(std::string_view model, std::string_view clip, f32 time, Transform transform = {})
    {
        MeshRenderer mesh;
        mesh.mesh = AssetRef(std::format("{}#mesh/0", model));
        Animator animator;
        animator.skeleton = AssetRef(std::format("{}#skin/0", model));
        animator.clip = AssetRef(std::format("{}#animation/{}", model, clip));
        animator.time = time;
        return world.create(transform, mesh, animator);
    }

    void load()
    {
        resolver.waitIdle(world, &jobs);
        animation.waitIdle();
    }

    // Poses the World (advancing time by @p seconds when @p advance) and renders it, skinned unless @p
    // bindPose.
    void frame(nvrhi::ITexture* target, RenderView view, f32 seconds = 0.0f, bool advance = false,
               bool bindPose = false)
    {
        streamer.update();
        cache.update();
        resolver.update(world, &jobs);
        animation.update(seconds, &jobs, advance);
        view.animation = bindPose ? nullptr : &animation;
        commands->open();
        renderer->render(commands, target, world, view, &jobs);
        commands->close();
        gpu.device()->executeCommandList(commands);
    }

    RenderDevice& gpu;
    struct Reflected
    {
        Reflected()
        {
            reflectCoreComponents();
            reflectRenderComponents();
            registerAnimationComponents(ComponentRegistry::instance());
        }
    } reflected;
    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    RenderResources resources;
    RenderAssetCache cache;
    RenderAssetResolver resolver;
    std::unique_ptr<SceneRenderer> renderer;
    nvrhi::CommandListHandle commands = gpu.device()->createCommandList();
    World world;
    AnimationWorld animation{world, streamer};
};

std::unique_ptr<RenderDevice> createDevice()
{
    return RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
}

} // namespace

TEST_CASE("a rigged mesh is drawn in its pose, and two clip times give different images")
{
    auto device = createDevice();
    REQUIRE(device);
    {
        Rig rig(*device);
        auto ring = ReadbackRing::create(*device);
        REQUIRE(ring);
        auto target = targetFor(*device, kGoldenSize);
        Transform sun;
        sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.3f, -0.4f, -1.0f)), Vec3(0, 1, 0));
        rig.world.create(sun, DirectionalLight{});
        const Entity character = rig.character(kRigged, "linear", 0.0f);
        rig.load();
        const Vec2 centre(0.0f, 1.0f);
        const RenderView view = orthographicView(centre, 5.0f);
        rig.frame(target, view);
        CHECK(rig.renderer->stats().skinnedInstances == 1);
        CHECK(rig.renderer->stats().visible == 1);
        const std::vector<std::byte> rest = pixels(*ring, target);
        golden("skinned-rigged-0s", rest);
        // Picking reads the posed shape: the entity ID covers the strip.
        CHECK(
            readPixel<UVec2>(*ring, rig.renderer->viewTargets().ids, pixelOf(Vec2(0, 1.5f), centre, 5, 64)) ==
            UVec2(character.index, character.generation));

        rig.world.get<Animator>(character).time = 1.0f;
        rig.frame(target, view);
        const std::vector<std::byte> bent = pixels(*ring, target);
        golden("skinned-rigged-1s", bent);
        usize changed = 0;
        for (usize i = 0; i < rest.size(); i += 4)
        {
            changed +=
                rest[i] != bent[i] || rest[i + 1] != bent[i + 1] || rest[i + 2] != bent[i + 2] ? 1u : 0u;
        }
        CHECK(changed > 100);
        const RenderGpuTimings& timings = rig.renderer->gpuTimings();
        if (timings.available && timings.valid)
        {
            CHECK(timings.skinning >= 0.0f);
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("skinned vertices written on the GPU equal CPU skinning of the same vertices")
{
    auto device = createDevice();
    REQUIRE(device);
    {
        Rig rig(*device);
        auto target = targetFor(*device, kGoldenSize);
        const Entity character = rig.character(kRigged, "linear", 0.75f);
        rig.load();
        rig.frame(target, orthographicView(Vec2(0, 1), 5));
        rig.world.get<Animator>(character).time = 1.25f;
        rig.frame(target, orthographicView(Vec2(0, 1), 5));
        const SkinnedVertexRange range = rig.renderer->skinnedVertices(character);
        REQUIRE(range.vertices != nullptr);

        const std::optional<ModelData> model = loadGltf(rig.project.files, std::string(kRigged));
        REQUIRE(model.has_value());
        const MeshData& mesh = model->meshes.at(0);
        REQUIRE(range.vertexCount == mesh.vertices.size());
        REQUIRE(mesh.skin.size() == mesh.vertices.size());
        const std::vector<std::byte> vertexBytes =
            readBuffer(*device, range.vertices, static_cast<u64>(range.firstVertex) * sizeof(Vertex),
                       range.vertexCount * sizeof(Vertex));
        const std::vector<std::byte> previousBytes =
            readBuffer(*device, range.previousPositions, static_cast<u64>(range.firstVertex) * sizeof(Vec4),
                       range.vertexCount * sizeof(Vec4));
        std::vector<Vertex> gpu(range.vertexCount);
        std::vector<Vec4> previous(range.vertexCount);
        std::memcpy(gpu.data(), vertexBytes.data(), vertexBytes.size());
        std::memcpy(previous.data(), previousBytes.data(), previousBytes.size());

        const AnimationPose& pose = rig.world.get<AnimationPose>(character);
        const std::span<const SkinMatrix> current = rig.animation.skinMatrices(pose.slot);
        const std::span<const SkinMatrix> before = rig.animation.previousSkinMatrices(pose.slot);
        bool moved = false;
        for (usize i = 0; i < mesh.vertices.size(); ++i)
        {
            CAPTURE(i);
            const Vertex& bind = mesh.vertices[i];
            const Vec3 position = skinned(current, mesh.skin[i], bind.position, 1.0f);
            const Vec3 normal = glm::normalize(skinned(current, mesh.skin[i], bind.normal, 0.0f));
            CHECK(glm::length(gpu[i].position - position) < 1e-4f);
            CHECK(glm::length(gpu[i].normal - normal) < 1e-4f);
            CHECK(gpu[i].uv == bind.uv);
            CHECK(gpu[i].tangent.w == bind.tangent.w);
            const Vec3 previousPosition = skinned(before, mesh.skin[i], bind.position, 1.0f);
            CHECK(glm::length(Vec3(previous[i]) - previousPosition) < 1e-4f);
            moved |= glm::length(position - previousPosition) > 1e-3f;
        }
        CHECK(moved);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("motion vectors follow a moving joint and stay zero over a still one")
{
    auto device = createDevice();
    REQUIRE(device);
    {
        Rig rig(*device);
        auto ring = ReadbackRing::create(*device);
        REQUIRE(ring);
        auto target = targetFor(*device, kGoldenSize);
        // The clip "cubic" scales the joint "side": the side triangle moves, the strip of root, mid and tip
        // does not.
        const Entity character = rig.character(kRigged, "cubic", 0.5f);
        rig.load();
        const Vec2 centre(0.5f, 1.0f);
        const RenderView view = orthographicView(centre, 4.0f);
        rig.frame(target, view);
        rig.world.get<Animator>(character).time = 1.0f;
        rig.frame(target, view);
        nvrhi::ITexture* motion = rig.renderer->motionVectors();
        const Vec2 still =
            glm::unpackHalf2x16(readPixel<u32>(*ring, motion, pixelOf(Vec2(0, 1.5f), centre, 4, 64)));
        const Vec2 moving =
            glm::unpackHalf2x16(readPixel<u32>(*ring, motion, pixelOf(Vec2(1.0f, 0.33f), centre, 4, 64)));
        CHECK(still == Vec2(0.0f));
        CHECK(std::abs(moving.x) > 1e-3f);
        CHECK(readPixel<UVec2>(*ring, rig.renderer->viewTargets().ids,
                               pixelOf(Vec2(1.0f, 0.33f), centre, 4, 64)) ==
              UVec2(character.index, character.generation));

        // Without a change of time the previous pose equals the current one.
        rig.frame(target, view);
        CHECK(glm::unpackHalf2x16(readPixel<u32>(*ring, motion, pixelOf(Vec2(1.0f, 0.33f), centre, 4, 64))) ==
              Vec2(0.0f));
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a skinned instance outside the frustum is not skinned, and its range lasts as long as its pose")
{
    auto device = createDevice();
    REQUIRE(device);
    {
        Rig rig(*device);
        auto target = targetFor(*device, kGoldenSize);
        Transform away;
        away.position.x = 100.0f;
        const Entity character = rig.character(kRigged, "linear", 0.5f, away);
        rig.load();
        const RenderView view = orthographicView(Vec2(0, 1), 5);
        rig.frame(target, view);
        CHECK(rig.renderer->stats().candidates == 1);
        CHECK(rig.renderer->stats().visible == 0);
        CHECK(rig.renderer->stats().skinnedInstances == 0);
        CHECK(rig.renderer->skinnedVertices(character).vertices == nullptr);

        rig.world.get<Transform>(character).position.x = 0.0f;
        rig.frame(target, view);
        CHECK(rig.renderer->stats().visible == 1);
        CHECK(rig.renderer->stats().skinnedInstances == 1);
        CHECK(rig.renderer->stats().skinnedVertices == rig.renderer->skinnedVertices(character).vertexCount);
        const u32 first = rig.renderer->skinnedVertices(character).firstVertex;

        // Out of view again: not skinned, but the range stays while the pose does.
        rig.world.get<Transform>(character).position.x = -100.0f;
        rig.frame(target, view);
        CHECK(rig.renderer->stats().skinnedInstances == 0);
        CHECK(rig.renderer->skinnedVertices(character).firstVertex == first);
        CHECK(rig.renderer->skinnedVertices(character).vertices != nullptr);

        // Without poses the mesh draws its bind pose and the range is returned.
        rig.world.get<Transform>(character).position.x = 0.0f;
        rig.frame(target, view, 0.0f, false, true);
        CHECK(rig.renderer->stats().visible == 1);
        CHECK(rig.renderer->stats().skinnedInstances == 0);
        CHECK(rig.renderer->skinnedVertices(character).vertices == nullptr);
        rig.world.destroy(character);
        rig.frame(target, view);
        CHECK(rig.renderer->stats().candidates == 0);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("shadows of a skinned mesh show its posed shape")
{
    auto device = createDevice();
    REQUIRE(device);
    {
        Rig rig(*device);
        auto ring = ReadbackRing::create(*device);
        REQUIRE(ring);
        constexpr u32 kSize = 128;
        auto target = targetFor(*device, kSize);
        // Light travelling along (-1, 0, -1): the shadow on the wall at z = 0 lies 2 units left of the strip.
        Transform sun;
        sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-1.0f, 0.0f, -1.0f)), Vec3(0, 1, 0));
        rig.world.create(sun, DirectionalLight{});
        Transform wall;
        wall.rotation = glm::angleAxis(glm::radians(90.0f), Vec3(1, 0, 0));
        rig.world.create(wall, RenderMesh{rig.resources.createMesh(makePlane(16.0f), "Wall")});
        // At 1 s the clip "linear" has turned the root 90 degrees: the strip lies along -x.
        rig.character(kRigged, "linear", 1.0f);
        rig.load();
        const Vec2 centre(-2.0f, 0.5f);
        const RenderView view = orthographicView(centre, 7.0f);
        const UVec2 bindShadow = pixelOf(Vec2(-1.8f, 1.8f), centre, 7, kSize);
        const UVec2 posedShadow = pixelOf(Vec2(-3.6f, 0.0f), centre, 7, kSize);

        rig.frame(target, view);
        CHECK(rig.renderer->stats().shadowCasters > 0);
        const u32 posedAtBind = brightness(readPixel<u32>(*ring, target, bindShadow));
        const u32 posedAtPosed = brightness(readPixel<u32>(*ring, target, posedShadow));
        rig.frame(target, view, 0.0f, false, true);
        const u32 bindAtBind = brightness(readPixel<u32>(*ring, target, bindShadow));
        const u32 bindAtPosed = brightness(readPixel<u32>(*ring, target, posedShadow));
        CHECK(posedAtPosed + 60 < posedAtBind);
        CHECK(bindAtBind + 60 < bindAtPosed);
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the skinning pass allocates nothing in the steady state")
{
    auto device = RenderDevice::create({.validation = ValidationMode::Disabled, .headless = true});
    REQUIRE(device);
    {
        Rig rig(*device);
        auto target = targetFor(*device, kGoldenSize);
        std::vector<Entity> characters;
        for (u32 i = 0; i < 4; ++i)
        {
            Transform transform;
            transform.position.x = static_cast<f32>(i) * 0.25f;
            characters.push_back(rig.character(kRigged, "linear", 0.0f, transform));
        }
        rig.load();
        const RenderView view = orthographicView(Vec2(0, 1), 6);
        nvrhi::IDevice* gpu = device->device();
        // Allocations of render() per frame, all NVRHI's, with `active` instances posed (others hidden).
        const auto steady = [&](u32 active) {
            for (u32 i = 0; i < characters.size(); ++i)
            {
                rig.world.get<RenderMesh>(characters[i]).flags = i < active ? RenderMesh::kVisible : 0u;
            }
            std::vector<usize> counts;
            constexpr usize kFrames = 16;
            counts.reserve(kFrames);
            for (usize frame = 0; frame < kFrames; ++frame)
            {
                rig.streamer.update();
                rig.cache.update();
                rig.resolver.update(rig.world, &rig.jobs);
                rig.animation.update(1.0f / 60.0f, &rig.jobs, true);
                RenderView posed = view;
                posed.animation = &rig.animation;
                rig.commands->open();
                usize allocations = 0;
                {
                    const test::ScopedAllocationCounter counter;
                    rig.renderer->render(rig.commands, target, rig.world, posed);
                    allocations = counter.count();
                }
                CHECK(rig.renderer->stats().skinnedInstances == active);
                counts.push_back(allocations);
                rig.commands->close();
                gpu->executeCommandList(rig.commands);
                device->waitIdle();
                gpu->runGarbageCollection();
            }
            CHECK(counts[kFrames - 3] == counts[kFrames - 2]);
            CHECK(counts[kFrames - 2] == counts[kFrames - 1]);
            return counts.back();
        };
        const usize one = steady(1);
        const usize four = steady(4);
        MESSAGE("allocations of a steady render(): " << one << " with one skinned instance, " << four
                                                     << " with four");
        CHECK(four == one);
    }
}

TEST_CASE("skinning 300 instances of 10,240 vertices stays within its GPU budget")
{
    auto device = createDevice();
    REQUIRE(device);
    {
        Rig rig(*device);
        writeCrowdModel(rig.project.files);
        auto target = targetFor(*device, 256);
        for (u32 i = 0; i < 300; ++i)
        {
            Transform transform;
            transform.position =
                Vec3(static_cast<f32>(i % 20) * 2.0f - 19.0f, static_cast<f32>(i / 20) * 2.0f - 14.0f, 0);
            transform.scale = Vec3(0.8f);
            rig.character(kCrowd, "bend", static_cast<f32>(i % 10) * 0.1f, transform);
        }
        rig.load();
        const RenderView view = orthographicView(Vec2(0), 40);
        for (int frame = 0; frame < 6; ++frame)
        {
            rig.frame(target, view, 1.0f / 60.0f, true);
        }
        CHECK(rig.renderer->stats().skinnedInstances == 300);
        CHECK(rig.renderer->stats().skinnedVertices == 300u * 10240u);
        device->waitIdle();
        const RenderGpuTimings& timings = rig.renderer->gpuTimings();
        if (timings.available)
        {
            REQUIRE(timings.valid);
            const f64 budget = device->gpu().software ? kSoftwareCrowdBudgetMs : kCrowdBudgetMs;
            MESSAGE("skinning 300 x 10,240 vertices on " << device->gpu().name << ": " << timings.skinning
                                                         << " ms (budget " << budget << " ms)");
            CHECK(timings.skinning < budget);
        }
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}
