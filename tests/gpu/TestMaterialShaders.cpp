// Material shaders in the renderer (ADR 0063): the built-in standard and error shaders, custom shaders in the
// bound and bindless paths, worldOffset() in every pass, pipelines built once, hot reload and release. The
// test shaders in data/shaders are compiled at build time (illusion_add_material_shaders in
// tests/CMakeLists.txt).

#include "TestSupport.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/ShaderAsset.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace illusion::test_shaders
{
std::span<const std::byte> glowShader() noexcept;
std::span<const std::byte> glowEditedShader() noexcept;
std::span<const std::byte> swayShader() noexcept;
std::span<const std::byte> unlitShader() noexcept;
std::span<const std::byte> skinShader() noexcept;
std::span<const std::byte> coatShader() noexcept;
} // namespace illusion::test_shaders

using namespace illusion;

namespace
{

constexpr u32 kSize = 64;

ShaderAsset loadShader(std::span<const std::byte> bytes)
{
    std::optional<ShaderAsset> shader = deserializeShader(bytes);
    REQUIRE(shader.has_value());
    return *shader;
}

SceneRendererDesc description(bool bindless)
{
    SceneRendererDesc desc;
    desc.colorFormat = nvrhi::Format::RGBA8_UNORM;
    desc.entityIds = true;
    desc.bindless = bindless;
    desc.clearColor = Vec4(0, 0, 0, 1);
    desc.ambient = Vec3(0.05f);
    desc.shadows.resolution = 256;
    desc.post.temporalAntiAliasing.enabled = false;
    desc.post.bloom.enabled = false;
    desc.post.ambientOcclusion.enabled = false;
    return desc;
}

// A device, resources and a 64x64 target seen from (0, 0, 5) looking down -z.
struct Fixture
{
    std::unique_ptr<RenderDevice> device =
        RenderDevice::create({.validation = ValidationMode::Enabled, .headless = true});
    std::unique_ptr<RenderResources> resources;
    std::unique_ptr<ReadbackRing> ring;
    nvrhi::TextureHandle target;
    RenderView view;

    Fixture()
    {
        REQUIRE(device != nullptr);
        resources = std::make_unique<RenderResources>(*device);
        ring = ReadbackRing::create(*device);
        REQUIRE(ring != nullptr);
        nvrhi::TextureDesc desc;
        desc.width = kSize;
        desc.height = kSize;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.isRenderTarget = true;
        desc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        target = device->device()->createTexture(desc);
        Transform camera;
        camera.position = Vec3(0, 0, 5);
        view = RenderView::fromCamera(camera, Camera{}, 1);
    }

    ~Fixture()
    {
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    // True if renderers made with SceneRendererDesc::bindless draw bindless here.
    [[nodiscard]] bool bindless() const { return device->gpu().bindless && device->gpu().multiDrawIndirect; }

    void render(SceneRenderer& renderer, World& world)
    {
        auto commands = device->device()->createCommandList();
        commands->open();
        renderer.render(commands, target, world, view);
        commands->close();
        device->device()->executeCommandList(commands);
    }

    std::vector<std::byte> pixels(nvrhi::ITexture* texture, UVec2 origin = UVec2(0),
                                  UVec2 size = UVec2(kSize), u32 texelBytes = 4)
    {
        std::vector<std::byte> result(static_cast<usize>(size.x) * size.y * texelBytes);
        const auto ticket = ring->request(texture, origin, size);
        REQUIRE(ticket.has_value());
        REQUIRE(ring->read(*ticket, result, true) == ReadbackStatus::Ready);
        return result;
    }

    std::vector<std::byte> image() { return pixels(target); }

    // The Entity ids of the last frame, row by row.
    std::vector<UVec2> ids(SceneRenderer& renderer)
    {
        const std::vector<std::byte> bytes =
            pixels(renderer.viewTargets().ids, UVec2(0), UVec2(kSize), sizeof(UVec2));
        std::vector<UVec2> result(static_cast<usize>(kSize) * kSize);
        std::memcpy(result.data(), bytes.data(), bytes.size());
        return result;
    }

    // The motion vector of pixel (@p x, @p y) of the last frame, in UV units.
    Vec2 motion(SceneRenderer& renderer, u32 x, u32 y)
    {
        const std::vector<std::byte> bytes = pixels(renderer.viewTargets().motion, UVec2(x, y), UVec2(1));
        u32 packed = 0;
        std::memcpy(&packed, bytes.data(), sizeof(packed));
        return glm::unpackHalf2x16(packed);
    }
};

int channel(const std::vector<std::byte>& image, u32 x, u32 y, u32 c)
{
    return std::to_integer<int>(image[(static_cast<usize>(y) * kSize + x) * 4 + c]);
}

// Pixels whose RGB differs by more than 12 in a channel.
usize differingPixels(const std::vector<std::byte>& a, const std::vector<std::byte>& b)
{
    REQUIRE(a.size() == b.size());
    usize different = 0;
    for (usize i = 0; i < a.size(); i += 4)
    {
        bool differs = false;
        for (usize c = 0; c < 3; ++c)
        {
            differs |= std::abs(std::to_integer<int>(a[i + c]) - std::to_integer<int>(b[i + c])) > 12;
        }
        different += differs ? 1u : 0u;
    }
    return different;
}

// Compares @p data with tests/data/render/<name>.png: at most 1% of the pixels may differ (by more than 12 in
// a channel), for edge pixels across Vulkan drivers. ILLUSION_UPDATE_GOLDENS=1 rewrites the golden.
void golden(std::string_view name, const std::vector<std::byte>& data)
{
    Image image;
    image.mips.push_back({kSize, kSize, 0, data.size()});
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
    if (update != nullptr && std::string_view(update) == "1")
    {
        write(reference);
    }
    std::ifstream stream(reference, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), reference.string());
    std::string bytes;
    bytes.assign(std::istreambuf_iterator<char>(stream.rdbuf()), std::istreambuf_iterator<char>());
    const auto expected = decodeImage(std::as_bytes(std::span(bytes.data(), bytes.size())), false);
    REQUIRE(expected.has_value());
    CHECK(differingPixels(data, expected->data) <= kSize * kSize / 100);
}

// A light from the upper right, in front of the scene, with shadows.
void addLight(World& world)
{
    Transform sun;
    sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.5f, -0.4f, -1.0f)), Vec3(0, 1, 0));
    DirectionalLight light{Vec3(1.0f), 3.0f};
    light.castShadows = true;
    world.create(sun, light);
}

MaterialDesc shaded(ShaderHandle shader, std::vector<ShaderParameterValue> parameters = {})
{
    MaterialDesc desc;
    desc.shader = shader;
    desc.shaderParameters = std::move(parameters);
    return desc;
}

} // namespace

TEST_CASE(
    "the standard, error and foliage shaders are built in and the default material uses the standard one")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    CHECK(resources.shaderCount() == 3);
    CHECK(resources.isValid(resources.standardShader()));
    CHECK(resources.isValid(resources.errorShader()));
    CHECK(resources.isValid(resources.foliageShader()));
    CHECK(resources.materialShader(resources.defaultMaterial()) == resources.standardShader());

    const ShaderAsset* standard = resources.shaderLayout(resources.standardShader());
    REQUIRE(standard != nullptr);
    CHECK(standard->source == "builtin:/shader/standard");
    CHECK(standard->shadingModel == ShadingModel::Default);
    CHECK(standard->worldOffset); // the scene's wind
    CHECK(standard->variants.empty());
    for (const std::string_view name :
         {"baseColorFactor", "emissiveFactor", "normalScale", "metallicFactor", "roughnessFactor",
          "occlusionStrength", "alphaCutoff", "diffuseTransmissionColor", "diffuseTransmission", "windSway",
          "windFlutter"})
    {
        CAPTURE(name);
        CHECK(standard->findParameter(name) != nullptr);
    }
    REQUIRE(standard->textures.size() == 5);
    CHECK(standard->textures[0].name == "baseColorTexture");
    CHECK(standard->textures[2].fallback == ShaderTextureFallback::FlatNormal);

    const ShaderAsset* error = resources.shaderLayout(resources.errorShader());
    REQUIRE(error != nullptr);
    CHECK(error->source == "builtin:/shader/error");
    CHECK(error->shadingModel == ShadingModel::Unlit);
    CHECK(resources.shaderLayout(ShaderHandle{}) == nullptr);

    // The foliage shader is the standard one in another shading model: the same parameters at the same
    // offsets.
    const ShaderAsset* foliage = resources.shaderLayout(resources.foliageShader());
    REQUIRE(foliage != nullptr);
    CHECK(foliage->source == "builtin:/shader/foliage");
    CHECK(foliage->shadingModel == ShadingModel::TwoSidedFoliage);
    CHECK(foliage->worldOffset);
    CHECK(foliage->parameterBytes == standard->parameterBytes);
    REQUIRE(foliage->parameters.size() == standard->parameters.size());
    for (usize i = 0; i < standard->parameters.size(); ++i)
    {
        CHECK(foliage->parameters[i].name == standard->parameters[i].name);
        CHECK(foliage->parameters[i].offset == standard->parameters[i].offset);
    }
    CHECK(foliage->textures.size() == standard->textures.size());

#if ILL_ASSERTS_ENABLED
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS_AS(resources.releaseShader(resources.standardShader()), test::AssertionFailure);
    CHECK_THROWS_AS(resources.releaseShader(resources.errorShader()), test::AssertionFailure);
    CHECK_THROWS_AS(resources.releaseShader(resources.foliageShader()), test::AssertionFailure);
    CHECK(resources.shaderCount() == 3);
#endif
}

TEST_CASE("a custom emissive shader renders its golden image in the bound and the bindless path")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle glow = resources.createShader(loadShader(test_shaders::glowShader()), "Glow");
    REQUIRE(resources.isValid(glow));
    CHECK(resources.shaderCount() == 4);
    const ShaderAsset* layout = resources.shaderLayout(glow);
    REQUIRE(layout != nullptr);
    CHECK(layout->source == "game:/shaders/glow.shader");
    CHECK(layout->findParameter("glowColor") != nullptr);

    const MeshHandle cube = resources.createMesh(makeCube(1.4f), "Glow cube");
    const MeshHandle sphere = resources.createMesh(makeSphere(0.6f), "Glow sphere");
    // Two materials of the shader, one with its defaults and one set by name (unknown names are ignored).
    const MaterialHandle orange = resources.createMaterial(shaded(glow));
    const MaterialHandle cyan =
        resources.createMaterial(shaded(glow, {{"glowColor", {0.1f, 0.8f, 1.0f, 0.0f}},
                                               {"stripes", {3.0f, 0.0f, 0.0f, 0.0f}},
                                               {"doesNotExist", {1.0f, 0.0f, 0.0f, 0.0f}}}));
    CHECK(resources.materialShader(cyan) == glow);

    std::vector<std::byte> bound;
    for (const bool bindless : {false, true})
    {
        CAPTURE(bindless);
        auto renderer = SceneRenderer::create(*fixture.device, resources, description(bindless));
        REQUIRE(renderer != nullptr);
        World world;
        Transform left;
        left.position = Vec3(-0.9f, 0.0f, 0.0f);
        left.rotation = glm::angleAxis(0.5f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f)));
        world.create(left, RenderMesh{cube, 0, orange});
        Transform right;
        right.position = Vec3(1.1f, 0.0f, 0.0f);
        world.create(right, RenderMesh{sphere, 0, cyan});
        addLight(world);

        fixture.render(*renderer, world);
        CHECK(renderer->stats().pipelinesBuilt > 0);
        fixture.render(*renderer, world);
        CHECK(renderer->stats().pipelinesBuilt == 0); // cached
        const std::vector<std::byte> image = fixture.image();
        if (!bindless)
        {
            golden("material-glow", image);
            bound = image;
        }
        else if (renderer->usesBindless())
        {
            // The material table holds each material's parameters in its shader's layout.
            CHECK(differingPixels(image, bound) <= kSize * kSize / 100);
        }
        else
        {
            MESSAGE("this device draws without bindless resources; only the bound path was checked");
        }
    }
}

TEST_CASE("an alpha-masked custom shader cuts colour, Entity ids and shadows in both paths")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle glow = resources.createShader(loadShader(test_shaders::glowShader()), "Glow");
    const MeshHandle quad = resources.createMesh(makePlane(3.0f), "Panel");
    MaterialDesc desc = shaded(glow, {{"cutout", {1.0f, 0.0f, 0.0f, 0.0f}}, {"stripes", {2.0f, 0, 0, 0}}});
    desc.parameters.alphaMode = AlphaMode::Mask;
    desc.parameters.doubleSided = true;
    const MaterialHandle cutout = resources.createMaterial(desc);
    for (const bool bindless : {false, true})
    {
        CAPTURE(bindless);
        auto renderer = SceneRenderer::create(*fixture.device, resources, description(bindless));
        REQUIRE(renderer != nullptr);
        World world;
        Transform panel; // the plane faces +y; turned to face the camera
        panel.rotation = glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        const Entity entity = world.create(panel, RenderMesh{quad, 0, cutout});
        addLight(world);
        fixture.render(*renderer, world);
        // Two stripes over the panel's height: rows inside a stripe show the panel, rows between show
        // nothing.
        const std::vector<UVec2> ids = fixture.ids(*renderer);
        usize covered = 0;
        usize uncovered = 0;
        for (u32 y = 12; y < 52; ++y)
        {
            const UVec2 id = ids[static_cast<usize>(y) * kSize + 32];
            covered += id == UVec2(entity.index, entity.generation) ? 1u : 0u;
            uncovered += id == UVec2(~0u) ? 1u : 0u;
        }
        CHECK(covered > 8);
        CHECK(uncovered > 8);
    }
}

TEST_CASE("a wind-offset shader moves colour, depth, Entity ids and motion vectors alike")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderAsset sway = loadShader(test_shaders::swayShader());
    CHECK(sway.worldOffset);
    const ShaderHandle shader = resources.createShader(sway, "Sway");
    const MeshHandle cube = resources.createMesh(makeCube(1.0f), "Stalk");
    const MaterialHandle bending = resources.createMaterial(shaded(shader));
    const MaterialHandle straight = resources.createMaterial(shaded(shader, {{"bend", {0.0f, 0, 0, 0}}}));
    const MaterialHandle wallMaterial = resources.createMaterial(MaterialDesc{});

    for (const bool bindless : {false, true})
    {
        CAPTURE(bindless);
        auto renderer = SceneRenderer::create(*fixture.device, resources, description(bindless));
        REQUIRE(renderer != nullptr);
        World world;
        Transform stalkAt;
        stalkAt.scale = Vec3(0.6f, 3.0f, 0.6f);
        const Entity stalk = world.create(stalkAt, RenderMesh{cube, 0, straight});
        Transform wallAt;
        wallAt.position = Vec3(0.0f, 0.0f, -1.5f);
        wallAt.scale = Vec3(8.0f, 8.0f, 0.2f);
        const Entity wall = world.create(wallAt, RenderMesh{cube, 0, wallMaterial});
        addLight(world);
        // A deterministic clock: worldOffset() sees time 1 and, for motion vectors, 0.
        world.create(Wind{}, WindState{1.0f, 0.0f});

        // The mean column of the stalk's Entity ids in row @p y.
        const UVec2 stalkId(stalk.index, stalk.generation);
        const auto stalkColumn = [&](const std::vector<UVec2>& ids, u32 y) {
            f32 sum = 0.0f;
            u32 count = 0;
            for (u32 x = 0; x < kSize; ++x)
            {
                if (ids[static_cast<usize>(y) * kSize + x] == stalkId)
                {
                    sum += static_cast<f32>(x);
                    ++count;
                }
            }
            REQUIRE(count > 0);
            return sum / static_cast<f32>(count);
        };
        fixture.render(*renderer, world);
        fixture.render(*renderer, world);
        const std::vector<std::byte> still = fixture.image();
        const std::vector<UVec2> stillIds = fixture.ids(*renderer);
        const f32 straightColumn = stalkColumn(stillIds, 18);
        CHECK(fixture.motion(*renderer, 32, 18) == Vec2(0.0f));

        world.get<RenderMesh>(stalk).material = bending;
        fixture.render(*renderer, world);
        fixture.render(*renderer, world);
        const std::vector<std::byte> bent = fixture.image();
        const std::vector<UVec2> bentIds = fixture.ids(*renderer);
        // The prepass (depth and ids) moved the top of the stalk to the right, by about 7 pixels.
        const f32 bentColumn = stalkColumn(bentIds, 18);
        CHECK(bentColumn > straightColumn + 4.0f);
        // The bottom stays put: the offset grows with the square of the height above the origin.
        CHECK(stalkColumn(bentIds, 44) == doctest::Approx(stalkColumn(stillIds, 44)).epsilon(0.05));
        // Its time differs from the previous frame's, so the moving top has motion vectors.
        CHECK(std::abs(fixture.motion(*renderer, static_cast<u32>(bentColumn + 0.5f), 18).x) > 5e-3f);
        // Its shadow moved with it: wall pixels in both images changed colour.
        const UVec2 wallId(wall.index, wall.generation);
        usize shadowMoved = 0;
        for (usize i = 0; i < stillIds.size(); ++i)
        {
            if (stillIds[i] != wallId || bentIds[i] != wallId)
            {
                continue;
            }
            for (usize c = 0; c < 3; ++c)
            {
                if (std::abs(std::to_integer<int>(still[i * 4 + c]) - std::to_integer<int>(bent[i * 4 + c])) >
                    12)
                {
                    ++shadowMoved;
                    break;
                }
            }
        }
        CHECK(shadowMoved > 5);
        if (!bindless)
        {
            golden("material-sway", bent);
        }
    }
}

TEST_CASE("hot reload builds a shader's pipelines again and keeps material parameters by name")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderAsset original = loadShader(test_shaders::glowShader());
    const ShaderHandle glow = resources.createShader(original, "Glow");
    const MeshHandle cube = resources.createMesh(makeCube(2.0f), "Cube");
    // No stripes (stripes 0): the first version does not glow; the edited one glows everywhere.
    const MaterialHandle material = resources.createMaterial(
        shaded(glow, {{"glowColor", {0.0f, 1.0f, 0.0f, 0.0f}}, {"stripes", {0.0f, 0, 0, 0}}}));
    auto renderer = SceneRenderer::create(*fixture.device, resources, description(false));
    REQUIRE(renderer != nullptr);
    World world;
    world.create(Transform{}, RenderMesh{cube, 0, material});
    fixture.render(*renderer, world);
    const std::vector<std::byte> before = fixture.image();
    CHECK(channel(before, 32, 32, 1) < 60);

    const ShaderAsset edited = loadShader(test_shaders::glowEditedShader());
    REQUIRE(edited.findParameter("glowColor")->offset != original.findParameter("glowColor")->offset);
    CHECK(resources.updateShader(glow, edited));
    CHECK(resources.isValid(glow));
    CHECK(resources.materialShader(material) == glow);
    CHECK(resources.shaderLayout(glow)->findParameter("pulse") != nullptr);
    fixture.render(*renderer, world);
    CHECK(renderer->stats().pipelinesBuilt > 0);
    const std::vector<std::byte> after = fixture.image();
    // Green from the material's glowColor, now at another offset of the parameter block.
    CHECK(channel(after, 32, 32, 1) > 200);
    CHECK(channel(after, 32, 32, 0) < 100);
    fixture.render(*renderer, world);
    CHECK(renderer->stats().pipelinesBuilt == 0);

    // A broken version keeps the working one.
    ShaderAsset broken = edited;
    broken.variants.clear();
    CHECK_FALSE(resources.updateShader(glow, broken));
    CHECK(resources.createShader(broken, "Broken") == ShaderHandle{});
    fixture.render(*renderer, world);
    CHECK(differingPixels(fixture.image(), after) == 0);
}

TEST_CASE("released shaders fall back to the standard shader and the error shader draws a checker")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle glow = resources.createShader(loadShader(test_shaders::glowShader()), "Glow");
    const MeshHandle cube = resources.createMesh(makeCube(2.0f), "Cube");
    const MaterialHandle material = resources.createMaterial(
        shaded(glow, {{"glowColor", {0.0f, 1.0f, 0.0f, 0.0f}}, {"stripes", {1, 0, 0, 0}}}));
    const MaterialHandle error = resources.createMaterial(shaded(resources.errorShader()));
    auto renderer = SceneRenderer::create(*fixture.device, resources, description(false));
    REQUIRE(renderer != nullptr);
    World world;
    const Entity entity = world.create(Transform{}, RenderMesh{cube, 0, material});

    resources.releaseShader(glow);
    CHECK_FALSE(resources.isValid(glow));
    CHECK(resources.materialShader(material) == resources.standardShader());
    fixture.render(*renderer, world);
    // The standard shader's defaults: white and rough, lit only by the ambient light, with no green glow.
    const std::vector<std::byte> standard = fixture.image();
    CHECK(std::abs(channel(standard, 32, 32, 0) - channel(standard, 32, 32, 1)) < 8);

    // The error checker: magenta and near-black cells, whatever the light.
    world.get<RenderMesh>(entity).material = error;
    fixture.render(*renderer, world);
    const std::vector<std::byte> checker = fixture.image();
    usize magenta = 0;
    usize dark = 0;
    for (u32 y = 20; y < 44; ++y)
    {
        for (u32 x = 20; x < 44; ++x)
        {
            const int r = channel(checker, x, y, 0);
            const int g = channel(checker, x, y, 1);
            const int b = channel(checker, x, y, 2);
            magenta += r > 200 && g < 40 && b > 200 ? 1u : 0u;
            dark += r < 80 && g < 20 && b < 80 ? 1u : 0u;
        }
    }
    CHECK(magenta > 50);
    CHECK(dark > 50);
}

TEST_CASE("vertex colours reach the surface of material shaders, white for meshes without them")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle glow = resources.createShader(loadShader(test_shaders::glowShader()), "Glow");
    MeshData blueCube = makeCube(2.0f);
    blueCube.colors.assign(blueCube.vertices.size(), 0xFFFF0000u); // RGBA8, red in the lowest byte
    const MeshHandle blue = resources.createMesh(blueCube, "Blue cube");
    const MeshHandle plain = resources.createMesh(makeCube(2.0f), "Plain cube");
    // No glow: the base colour (0.1 times the vertex colour) under a bright light.
    const MaterialHandle material = resources.createMaterial(shaded(glow, {{"intensity", {0.0f, 0, 0, 0}}}));
    auto renderer = SceneRenderer::create(*fixture.device, resources, description(false));
    REQUIRE(renderer != nullptr);
    World world;
    Transform sun; // from the upper left, so that no highlight reaches the camera
    sun.rotation = glm::quatLookAt(glm::normalize(Vec3(0.4f, -0.4f, -1.0f)), Vec3(0, 1, 0));
    world.create(sun, DirectionalLight{Vec3(1.0f), 8.0f});
    const Entity entity = world.create(Transform{}, RenderMesh{blue, 0, material});
    fixture.render(*renderer, world);
    const std::vector<std::byte> tinted = fixture.image();
    CHECK(channel(tinted, 32, 32, 2) > channel(tinted, 32, 32, 0) + 30);
    CHECK(channel(tinted, 32, 32, 2) > channel(tinted, 32, 32, 1) + 30);

    world.get<RenderMesh>(entity).mesh = plain;
    fixture.render(*renderer, world);
    const std::vector<std::byte> white = fixture.image();
    CHECK(channel(white, 32, 32, 2) > 30);
    CHECK(std::abs(channel(white, 32, 32, 2) - channel(white, 32, 32, 0)) < 8);
}

TEST_CASE("translucent standard materials draw with the two-sided foliage shader and keep their image")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    MaterialDesc leafDesc;
    leafDesc.parameters.metallicFactor = 0.0f;
    leafDesc.parameters.roughnessFactor = 0.8f;
    leafDesc.parameters.baseColorFactor = Vec4(0.35f, 0.6f, 0.2f, 1.0f);
    leafDesc.parameters.alphaMode = AlphaMode::Mask;
    leafDesc.parameters.doubleSided = true;
    leafDesc.parameters.diffuseTransmission = 0.35f;
    leafDesc.parameters.diffuseTransmissionColor = Vec3(0.9f, 1.0f, 0.55f);
    const MaterialHandle leaf = resources.createMaterial(leafDesc);
    CHECK(resources.materialShader(leaf) == resources.foliageShader());
    // Without transmission the material moves to the standard shader, and back with it.
    MaterialDesc opaqueDesc = leafDesc;
    opaqueDesc.parameters.diffuseTransmission = 0.0f;
    resources.updateMaterial(leaf, opaqueDesc);
    CHECK(resources.materialShader(leaf) == resources.standardShader());
    resources.updateMaterial(leaf, leafDesc);
    CHECK(resources.materialShader(leaf) == resources.foliageShader());
    // A chosen shader stays, whatever the transmission.
    MaterialDesc chosen = leafDesc;
    chosen.shader = resources.standardShader();
    const MaterialHandle standardLeaf = resources.createMaterial(chosen);
    CHECK(resources.materialShader(standardLeaf) == resources.standardShader());
    const MeshHandle quad = resources.createMesh(makePlane(2.0f), "Leaf");
    const MeshHandle sphere = resources.createMesh(makeSphere(0.8f), "Bush");

    std::vector<std::byte> bound;
    for (const bool bindless : {false, true})
    {
        CAPTURE(bindless);
        auto renderer = SceneRenderer::create(*fixture.device, resources, description(bindless));
        REQUIRE(renderer != nullptr);
        World world;
        Transform left; // the plane faces +y; turned towards the camera and a little to the side
        left.position = Vec3(-1.0f, 0.0f, 0.0f);
        left.rotation = glm::angleAxis(0.4f, Vec3(0.0f, 1.0f, 0.0f)) *
                        glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        world.create(left, RenderMesh{quad, 0, leaf});
        Transform right;
        right.position = Vec3(1.1f, 0.0f, 0.0f);
        world.create(right, RenderMesh{sphere, 0, leaf});
        // The sun from behind and above, shining towards the camera.
        Transform sun;
        sun.rotation = glm::quatLookAt(glm::normalize(Vec3(0.3f, -0.5f, 1.0f)), Vec3(0, 1, 0));
        DirectionalLight light{Vec3(1.0f), 4.0f};
        light.castShadows = true;
        world.create(sun, light);

        fixture.render(*renderer, world);
        fixture.render(*renderer, world);
        const std::vector<std::byte> image = fixture.image();
        if (!bindless)
        {
            // Captured before the foliage moved from the standard shader to builtin:/shader/foliage: the
            // TwoSidedFoliage model draws GFX-2d's leaf translucency unchanged.
            golden("material-foliage", image);
            bound = image;
        }
        else if (renderer->usesBindless())
        {
            CHECK(differingPixels(image, bound) <= kSize * kSize / 100);
        }
    }
}

namespace
{

// A scene of one sphere of radius 1.5 at the origin lit by a sun shining along @p sunDirection (without
// shadows unless @p shadows); returns the image of the second frame.
std::vector<std::byte> renderSphere(Fixture& fixture, MaterialHandle material, Vec3 sunDirection,
                                    bool bindless, bool shadows = false)
{
    RenderResources& resources = *fixture.resources;
    auto renderer = SceneRenderer::create(*fixture.device, resources, description(bindless));
    REQUIRE(renderer != nullptr);
    World world;
    world.create(Transform{}, RenderMesh{resources.createMesh(makeSphere(1.5f), "Sphere"), 0, material});
    Transform sun;
    sun.rotation = glm::quatLookAt(glm::normalize(sunDirection), Vec3(0, 1, 0));
    DirectionalLight light{Vec3(1.0f), 3.0f};
    light.castShadows = shadows;
    world.create(sun, light);
    fixture.render(*renderer, world);
    fixture.render(*renderer, world);
    return fixture.image();
}

// Sum of channel @p c over the columns [@p x0, @p x1) of the middle rows.
int bandSum(const std::vector<std::byte>& image, u32 x0, u32 x1, u32 c)
{
    int sum = 0;
    for (u32 y = 24; y < 40; ++y)
    {
        for (u32 x = x0; x < x1; ++x)
        {
            sum += channel(image, x, y, c);
        }
    }
    return sum;
}

int brightest(const std::vector<std::byte>& image)
{
    int result = 0;
    for (u32 y = 0; y < kSize; ++y)
    {
        for (u32 x = 0; x < kSize; ++x)
        {
            result =
                std::max(result, channel(image, x, y, 0) + channel(image, x, y, 1) + channel(image, x, y, 2));
        }
    }
    return result;
}

} // namespace

TEST_CASE("the Unlit shading model shows its base colour whatever the light")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle unlit = resources.createShader(loadShader(test_shaders::unlitShader()), "Unlit");
    REQUIRE(resources.isValid(unlit));
    CHECK(resources.shaderLayout(unlit)->shadingModel == ShadingModel::Unlit);
    const MaterialHandle material = resources.createMaterial(shaded(unlit));
    const std::vector<std::byte> lit =
        renderSphere(fixture, material, Vec3(-0.5f, -0.4f, -1.0f), false, true);
    golden("material-unlit", lit);
    // Lit from behind, the same colours.
    CHECK(differingPixels(renderSphere(fixture, material, Vec3(0.0f, 0.0f, 1.0f), false), lit) == 0);
    // The gradient's middle: half of the top colour and half of the bottom one, in sRGB.
    CHECK(channel(lit, 32, 32, 0) > channel(lit, 32, 32, 1));
    CHECK(channel(lit, 32, 32, 2) > channel(lit, 32, 32, 1));
}

TEST_CASE("the Subsurface shading model wraps light past the terminator in its scatter colour")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle skin = resources.createShader(loadShader(test_shaders::skinShader()), "Skin");
    REQUIRE(resources.isValid(skin));
    CHECK(resources.shaderLayout(skin)->shadingModel == ShadingModel::Subsurface);
    const MaterialHandle thin = resources.createMaterial(shaded(skin));
    const MaterialHandle thick = resources.createMaterial(shaded(skin, {{"thickness", {1.0f, 0, 0, 0}}}));
    MaterialDesc standardDesc;
    standardDesc.parameters.baseColorFactor = Vec4(0.9f, 0.7f, 0.6f, 1.0f);
    standardDesc.parameters.metallicFactor = 0.0f;
    standardDesc.parameters.roughnessFactor = 0.6f;
    const MaterialHandle standard = resources.createMaterial(standardDesc);

    // The sun from the right: the terminator runs down the middle of the sphere.
    const Vec3 sun(-1.0f, 0.0f, 0.0f);
    const std::vector<std::byte> wrapped = renderSphere(fixture, thin, sun, false);
    golden("material-subsurface", wrapped);
    const std::vector<std::byte> lambert = renderSphere(fixture, thick, sun, false);
    // The unlit half receives red light; the lit half keeps its colour.
    CHECK(bandSum(wrapped, 16, 30, 0) > bandSum(lambert, 16, 30, 0) + 2000);
    CHECK(bandSum(wrapped, 16, 30, 0) - bandSum(lambert, 16, 30, 0) >
          2 * (bandSum(wrapped, 16, 30, 2) - bandSum(lambert, 16, 30, 2)));
    // Thickness 1 is the Default model.
    CHECK(differingPixels(lambert, renderSphere(fixture, standard, sun, false)) <= kSize * kSize / 100);
}

TEST_CASE("the ClearCoat shading model adds a sharp highlight over a rough base")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle coat = resources.createShader(loadShader(test_shaders::coatShader()), "Coat");
    REQUIRE(resources.isValid(coat));
    CHECK(resources.shaderLayout(coat)->shadingModel == ShadingModel::ClearCoat);
    const MaterialHandle coated = resources.createMaterial(shaded(coat));
    const MaterialHandle bare = resources.createMaterial(shaded(coat, {{"coat", {0.0f, 0, 0, 0}}}));
    MaterialDesc standardDesc;
    standardDesc.parameters.baseColorFactor = Vec4(0.6f, 0.05f, 0.05f, 1.0f);
    standardDesc.parameters.metallicFactor = 0.0f;
    standardDesc.parameters.roughnessFactor = 0.7f;
    const MaterialHandle standard = resources.createMaterial(standardDesc);

    const Vec3 sun(-0.3f, -0.3f, -1.0f);
    const std::vector<std::byte> glossy = renderSphere(fixture, coated, sun, false);
    golden("material-clearcoat", glossy);
    const std::vector<std::byte> rough = renderSphere(fixture, bare, sun, false);
    CHECK(brightest(glossy) > brightest(rough) + 150);
    // Coat 0 is the Default model.
    CHECK(differingPixels(rough, renderSphere(fixture, standard, sun, false)) <= kSize * kSize / 100);
}

TEST_CASE("materials of every shading model draw alike in one frame of the bound and the bindless path")
{
    Fixture fixture;
    RenderResources& resources = *fixture.resources;
    const ShaderHandle unlit = resources.createShader(loadShader(test_shaders::unlitShader()), "Unlit");
    const ShaderHandle skin = resources.createShader(loadShader(test_shaders::skinShader()), "Skin");
    const ShaderHandle coat = resources.createShader(loadShader(test_shaders::coatShader()), "Coat");
    MaterialDesc leafDesc;
    leafDesc.parameters.metallicFactor = 0.0f;
    leafDesc.parameters.diffuseTransmission = 0.5f;
    leafDesc.parameters.doubleSided = true;
    const std::vector<MaterialHandle> materials = {
        resources.createMaterial(MaterialDesc{}), resources.createMaterial(leafDesc),
        resources.createMaterial(shaded(unlit)), resources.createMaterial(shaded(skin)),
        resources.createMaterial(shaded(coat))};
    const MeshHandle sphere = resources.createMesh(makeSphere(0.45f), "Sphere");

    std::vector<std::byte> bound;
    for (const bool bindless : {false, true})
    {
        CAPTURE(bindless);
        auto renderer = SceneRenderer::create(*fixture.device, resources, description(bindless));
        REQUIRE(renderer != nullptr);
        World world;
        for (usize i = 0; i < materials.size(); ++i)
        {
            Transform at;
            at.position = Vec3(-2.0f + static_cast<f32>(i), (i % 2 == 0) ? 0.5f : -0.5f, 0.0f);
            world.create(at, RenderMesh{sphere, 0, materials[i]});
        }
        addLight(world);
        fixture.render(*renderer, world);
        CHECK(renderer->stats().pipelinesBuilt > 0);
        fixture.render(*renderer, world);
        CHECK(renderer->stats().pipelinesBuilt == 0);
        const std::vector<std::byte> image = fixture.image();
        if (!bindless)
        {
            bound = image;
        }
        else if (renderer->usesBindless())
        {
            CHECK(differingPixels(image, bound) <= kSize * kSize / 100);
        }
    }
}
