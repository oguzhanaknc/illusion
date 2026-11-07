#include <illusion/core/Json.hpp>
#include <illusion/core/VirtualPath.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/PostProcess.hpp>
#include <illusion/render/Shadows.hpp>

#include <format>
#include <optional>
#include <string>

namespace illusion
{
namespace
{

constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};

} // namespace

void reflectRenderComponents(ComponentRegistry& registry)
{
    StructRegistry& structs = registry.structs();
    reflectStruct<ShadowSettings>("illusion.ShadowSettings", 1, structs)
        .meta(
            {.displayName = "Shadows",
             .description = "Soft shadows of the first shadow-casting directional light and of point lights.",
             .docsAnchor = "manual/rendering.md#shadows"})
        .field("enabled", &ShadowSettings::enabled)
        .field("cascadeCount", &ShadowSettings::cascadeCount,
               {.min = 1.0, .max = static_cast<f64>(kMaxShadowCascades), .hint = FieldHint::Slider})
        .field("resolution", &ShadowSettings::resolution, {.min = 16.0, .max = 16384.0})
        .field("maxDistance", &ShadowSettings::maxDistance, {.unit = Unit::Meters, .min = 0.0})
        .field("splitLambda", &ShadowSettings::splitLambda,
               {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("casterDistance", &ShadowSettings::casterDistance, {.unit = Unit::Meters, .min = 0.0})
        .field("depthBias", &ShadowSettings::depthBias, {.unit = Unit::Meters, .min = 0.0, .advanced = true})
        .field("normalBias", &ShadowSettings::normalBias,
               {.description = "Receiver offset along its normal, in shadow-map texels.",
                .min = 0.0,
                .advanced = true})
        .field("slopeBias", &ShadowSettings::slopeBias, {.min = 0.0, .advanced = true})
        .field("pointLightCount", &ShadowSettings::pointLightCount,
               {.min = 0.0, .max = static_cast<f64>(kMaxShadowedPointLights), .hint = FieldHint::Slider})
        .field("pointResolution", &ShadowSettings::pointResolution, {.min = 16.0, .max = 4096.0});
    reflectStruct<AmbientOcclusionSettings>("illusion.AmbientOcclusionSettings", 1, structs)
        .meta({.displayName = "Ambient occlusion"})
        .field("enabled", &AmbientOcclusionSettings::enabled)
        .field("radius", &AmbientOcclusionSettings::radius, {.unit = Unit::Meters, .min = 0.0})
        .field("intensity", &AmbientOcclusionSettings::intensity, {.min = 0.0, .softMax = 4.0});
    reflectStruct<BloomSettings>("illusion.BloomSettings", 1, structs)
        .meta({.displayName = "Bloom"})
        .field("enabled", &BloomSettings::enabled)
        .field("intensity", &BloomSettings::intensity, {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("maxLevels", &BloomSettings::maxLevels, {.min = 1.0, .max = 16.0});
    reflectStruct<TemporalAntiAliasingSettings>("illusion.TemporalAntiAliasingSettings", 1, structs)
        .meta({.displayName = "Temporal anti-aliasing"})
        .field("enabled", &TemporalAntiAliasingSettings::enabled)
        .field("currentFrameWeight", &TemporalAntiAliasingSettings::currentFrameWeight,
               {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider});
    reflectStruct<PostProcessSettings>("illusion.PostProcessSettings", 1, structs)
        .meta({.displayName = "Post-processing", .docsAnchor = "manual/rendering.md#hdr-and-post-processing"})
        .field("ambientOcclusion", &PostProcessSettings::ambientOcclusion)
        .field("bloom", &PostProcessSettings::bloom)
        .field("temporalAntiAliasing", &PostProcessSettings::temporalAntiAliasing);
    reflectStruct<EnvironmentDesc>("illusion.EnvironmentDesc", 1, structs)
        .meta({.displayName = "Environment baking", .docsAnchor = "manual/rendering.md#image-based-lighting"})
        .field("cubeSize", &EnvironmentDesc::cubeSize, {.min = 1.0, .max = 8192.0})
        .field("specularSize", &EnvironmentDesc::specularSize, {.min = 1.0, .max = 8192.0})
        .field("sampleCount", &EnvironmentDesc::sampleCount, {.min = 1.0, .max = 65536.0});

    reflect<MeshRenderer>("illusion.MeshRenderer", 1, registry)
        .meta({.displayName = "Mesh renderer",
               .category = "Rendering",
               .icon = "deployed_code",
               .description = "Draws a submesh of a mesh asset with a material asset.",
               .docsAnchor = "manual/rendering.md#meshrenderer-and-rendermesh",
               .requiredComponents = kRequiresTransform})
        .field("mesh", &MeshRenderer::mesh, {.assetKind = AssetKind::Mesh})
        .field("submesh", &MeshRenderer::submesh)
        .field("material", &MeshRenderer::material,
               {.description = "Empty uses the material the model assigns to the submesh.",
                .assetKind = AssetKind::Material})
        .field("layers", &MeshRenderer::layers, {.advanced = true})
        .field("castShadows", &MeshRenderer::castShadows)
        .field("visible", &MeshRenderer::visible);
    reflect<Camera>("illusion.Camera", 2, registry)
        .meta({.displayName = "Camera",
               .category = "Rendering",
               .icon = "videocam",
               .description = "Perspective or orthographic camera looking along the Transform's -z axis.",
               .docsAnchor = "manual/rendering.md#drawing-a-scene",
               .requiredComponents = kRequiresTransform})
        .field("fovY", &Camera::fovY,
               {.displayName = "Field of view",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.001,
                .max = 3.14,
                .hint = FieldHint::Angle})
        .field("nearPlane", &Camera::nearPlane, {.unit = Unit::Meters, .min = 0.0001})
        .field("exposure", &Camera::exposure, {.min = 0.0, .hint = FieldHint::LogSlider})
        .field("projection", &Camera::projection)
        .field("orthoHeight", &Camera::orthoHeight, {.unit = Unit::Meters, .min = 0.0001})
        .field("farPlane", &Camera::farPlane, {.unit = Unit::Meters, .min = 0.0002})
        .field("priority", &Camera::priority)
        // v1 cameras retain their perspective projection; new fields take their defaults.
        .migrate(1, [](JsonMutDocument&, JsonMutValue) {});
    reflect<DirectionalLight>("illusion.DirectionalLight", 1, registry)
        .meta({.displayName = "Directional light",
               .category = "Rendering/Lights",
               .icon = "light_mode",
               .description = "Sun-like light shining along the Transform's -z axis.",
               .docsAnchor = "manual/rendering.md#materials-and-shading",
               .requiredComponents = kRequiresTransform})
        .field("color", &DirectionalLight::color, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("intensity", &DirectionalLight::intensity, {.min = 0.0})
        .field("castShadows", &DirectionalLight::castShadows)
        .field("angularDiameter", &DirectionalLight::angularDiameter,
               {.unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 0.5,
                .hint = FieldHint::Angle});
    reflect<PointLight>("illusion.PointLight", 1, registry)
        .meta({.displayName = "Point light",
               .category = "Rendering/Lights",
               .icon = "lightbulb",
               .description = "Omnidirectional light at the Transform's position.",
               .docsAnchor = "manual/rendering.md#materials-and-shading",
               .requiredComponents = kRequiresTransform})
        .field("color", &PointLight::color, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("intensity", &PointLight::intensity, {.min = 0.0})
        .field("range", &PointLight::range, {.unit = Unit::Meters, .min = 0.0})
        .field("castShadows", &PointLight::castShadows)
        .field("sourceRadius", &PointLight::sourceRadius,
               {.description = "Size of the glowing body; larger gives softer shadows.",
                .unit = Unit::Meters,
                .min = 0.0,
                .softMax = 1.0})
        .field("shadowBias", &PointLight::shadowBias, {.unit = Unit::Meters, .min = 0.0, .advanced = true})
        .field("shadowNearPlane", &PointLight::shadowNearPlane,
               {.description = "Casters closer to the light than this are ignored.",
                .unit = Unit::Meters,
                .min = 0.001,
                .advanced = true});
    reflect<SkyLight>("illusion.SkyLight", 1, registry)
        .meta({.displayName = "Sky light",
               .category = "Rendering/Lights",
               .icon = "wb_twilight",
               .description = "Image-based lighting and background from an environment.",
               .docsAnchor = "manual/rendering.md#image-based-lighting",
               .requiredComponents = kRequiresTransform})
        .field("environment", &SkyLight::environment, {.assetKind = AssetKind::Environment})
        .field("intensity", &SkyLight::intensity, {.min = 0.0, .hint = FieldHint::LogSlider})
        .field("background", &SkyLight::background);
    reflect<ProceduralSky>("illusion.ProceduralSky", 1, registry)
        .meta({.displayName = "Procedural sky",
               .category = "Rendering/Lights",
               .icon = "partly_cloudy_day",
               .description = "Gradient sky for a SkyLight whose environment is builtin:/sky/procedural.",
               .docsAnchor = "manual/rendering.md#image-based-lighting"})
        .field("zenith", &ProceduralSky::zenith, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("horizon", &ProceduralSky::horizon, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("ground", &ProceduralSky::ground, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("sunDirection", &ProceduralSky::sunDirection)
        .field("sunRadiance", &ProceduralSky::sunRadiance, {.min = 0.0, .hint = FieldHint::HdrColor})
        .field("sunAngularRadius", &ProceduralSky::sunAngularRadius,
               {.unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 0.5,
                .hint = FieldHint::Angle})
        .field("resolution", &ProceduralSky::resolution, {.min = 8.0, .max = 8192.0, .advanced = true});
    reflect<PostEffects>("illusion.PostEffects", 1, registry)
        .meta(
            {.displayName = "Post effects",
             .category = "Rendering",
             .icon = "camera",
             .description = "Automatic exposure, tonemapping, colour grading and lens effects of the scene.",
             .docsAnchor = "manual/rendering.md#exposure-and-colour-grading",
             .singleton = true})
        .field("autoExposure", &PostEffects::autoExposure, {.category = "Exposure"})
        .field("exposureCompensation", &PostEffects::exposureCompensation,
               {.description = "Stops added to the exposure.",
                .category = "Exposure",
                .softMin = -4.0,
                .softMax = 4.0})
        .field("minExposure", &PostEffects::minExposure,
               {.description = "Darkest automatic exposure, in stops.",
                .category = "Exposure",
                .softMin = -8.0,
                .softMax = 8.0})
        .field("maxExposure", &PostEffects::maxExposure,
               {.description = "Brightest automatic exposure, in stops.",
                .category = "Exposure",
                .softMin = -8.0,
                .softMax = 8.0})
        .field("adaptationSpeed", &PostEffects::adaptationSpeed,
               {.description = "How fast the exposure follows the light, per second.",
                .category = "Exposure",
                .min = 0.0,
                .softMax = 10.0})
        .field("tonemapper", &PostEffects::tonemapper, {.category = "Colour"})
        .field("contrast", &PostEffects::contrast,
               {.category = "Colour", .min = 0.0, .softMax = 2.0, .hint = FieldHint::Slider})
        .field("saturation", &PostEffects::saturation,
               {.category = "Colour", .min = 0.0, .softMax = 2.0, .hint = FieldHint::Slider})
        .field("temperature", &PostEffects::temperature,
               {.description = "Negative cooler, positive warmer.",
                .category = "Colour",
                .min = -1.0,
                .max = 1.0,
                .hint = FieldHint::Slider})
        .field("tint", &PostEffects::tint,
               {.description = "Negative greener, positive more magenta.",
                .category = "Colour",
                .min = -1.0,
                .max = 1.0,
                .hint = FieldHint::Slider})
        .field("vignette", &PostEffects::vignette,
               {.category = "Lens", .min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("grain", &PostEffects::grain,
               {.category = "Lens", .min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("chromaticAberration", &PostEffects::chromaticAberration,
               {.description = "Colour fringes at the corners, in pixels.",
                .category = "Lens",
                .min = 0.0,
                .softMax = 8.0});
    reflect<VolumetricFog>("illusion.VolumetricFog", 1, registry)
        .meta({.displayName = "Volumetric fog",
               .category = "Rendering",
               .icon = "foggy",
               .description = "Height fog that scatters the sun, moon and point lights into light shafts.",
               .docsAnchor = "manual/rendering.md#volumetric-fog",
               .singleton = true})
        .field("density", &VolumetricFog::density,
               {.description = "Extinction per metre at the base height.",
                .category = "Medium",
                .min = 0.0,
                .softMax = 0.2,
                .hint = FieldHint::LogSlider})
        .field("heightFalloff", &VolumetricFog::heightFalloff,
               {.description = "How fast the fog thins above the base height, per metre.",
                .category = "Medium",
                .min = 0.0,
                .softMax = 2.0})
        .field("baseHeight", &VolumetricFog::baseHeight,
               {.description = "Height below which the fog has its full density.",
                .category = "Medium",
                .unit = Unit::Meters})
        .field("albedo", &VolumetricFog::albedo,
               {.description = "Colour of the scattered light.",
                .category = "Medium",
                .min = 0.0,
                .max = 1.0,
                .hint = FieldHint::Color})
        .field("anisotropy", &VolumetricFog::anisotropy,
               {.description = "Positive values glow towards the lights, negative away from them.",
                .category = "Lighting",
                .min = -0.9,
                .max = 0.9,
                .hint = FieldHint::Slider})
        .field("lightIntensity", &VolumetricFog::lightIntensity,
               {.description = "Multiplies the sun, moon and point light the fog scatters.",
                .category = "Lighting",
                .min = 0.0,
                .softMax = 4.0})
        .field("ambientIntensity", &VolumetricFog::ambientIntensity,
               {.description = "Multiplies the sky light the fog scatters.",
                .category = "Lighting",
                .min = 0.0,
                .softMax = 4.0})
        .field("maxDistance", &VolumetricFog::maxDistance,
               {.description = "Distance from the camera the fog reaches.",
                .unit = Unit::Meters,
                .min = 1.0,
                .softMax = 500.0})
        .field("noiseAmount", &VolumetricFog::noiseAmount,
               {.description = "How patchy the fog is.",
                .category = "Noise",
                .min = 0.0,
                .max = 1.0,
                .hint = FieldHint::Slider})
        .field("noiseSize", &VolumetricFog::noiseSize,
               {.description = "Size of the patches.", .category = "Noise", .unit = Unit::Meters, .min = 0.1})
        .field("windFactor", &VolumetricFog::windFactor,
               {.description = "How fast the Wind carries the patches.",
                .category = "Noise",
                .min = 0.0,
                .softMax = 4.0});
    reflect<SkyAtmosphere>("illusion.SkyAtmosphere", 1, registry)
        .meta({.displayName = "Sky atmosphere",
               .category = "Rendering/Lights",
               .icon = "routine",
               .description =
                   "A sky that follows the time of day, with its sun, moon and stars as the main light.",
               .docsAnchor = "manual/rendering.md#day-and-night",
               .singleton = true})
        .field("timeOfDay", &SkyAtmosphere::timeOfDay,
               {.description = "Hours since midnight: the sun rises at 6 and sets at 18.",
                .category = "Sun and moon",
                .min = 0.0,
                .max = 24.0,
                .hint = FieldHint::Slider})
        .field("sunAzimuth", &SkyAtmosphere::sunAzimuth,
               {.description = "Turn of the sun's path about the up axis.",
                .category = "Sun and moon",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .hint = FieldHint::Angle})
        .field("sunTilt", &SkyAtmosphere::sunTilt,
               {.description = "Angle of the noon sun from straight up.",
                .category = "Sun and moon",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = 1.5,
                .hint = FieldHint::Angle})
        .field("moonOffset", &SkyAtmosphere::moonOffset,
               {.description = "Hours the moon trails the point opposite the sun: 0 full moon, 12 new moon.",
                .category = "Sun and moon",
                .min = -12.0,
                .max = 12.0})
        .field("turbidity", &SkyAtmosphere::turbidity,
               {.description = "Haze: 1 clear mountain air, 10 thick haze.",
                .category = "Sky",
                .min = 1.0,
                .max = 10.0,
                .hint = FieldHint::Slider})
        .field("skyIntensity", &SkyAtmosphere::skyIntensity,
               {.category = "Sky", .min = 0.0, .hint = FieldHint::LogSlider})
        .field("sunIntensity", &SkyAtmosphere::sunIntensity,
               {.description = "Illuminance of the noon sunlight.", .category = "Sun and moon", .min = 0.0})
        .field("moonIntensity", &SkyAtmosphere::moonIntensity,
               {.description = "Illuminance of a full moon's light.", .category = "Sun and moon", .min = 0.0})
        .field("starIntensity", &SkyAtmosphere::starIntensity,
               {.category = "Sky", .min = 0.0, .softMax = 4.0})
        .field("groundAlbedo", &SkyAtmosphere::groundAlbedo,
               {.category = "Sky", .min = 0.0, .max = 1.0, .hint = FieldHint::Color})
        .field("castShadows", &SkyAtmosphere::castShadows, {.category = "Sun and moon"})
        .field("lightingResolution", &SkyAtmosphere::lightingResolution,
               {.description = "Face size of the reflection cube map.",
                .min = 16.0,
                .max = 256.0,
                .advanced = true});
    reflectRuntime<RenderMesh>(
        "illusion.RenderMesh",
        {.displayName = "Render mesh",
         .description = "GPU mesh and material drawn by the renderer (runtime state)."},
        registry);
    reflectRuntime<RenderSky>(
        "illusion.RenderSky",
        {.displayName = "Render sky", .description = "GPU environment of a SkyLight (runtime state)."},
        registry);
    reflectRuntime<RenderParticles>("illusion.RenderParticles",
                                    {.displayName = "Render particles",
                                     .description = "GPU sprite of a ParticleEmitter (runtime state)."},
                                    registry);
}

namespace
{

// Creates the entities of spawnModel(); @p addRenderer gives a mesh entity its renderer for (mesh, submesh).
template <class AddRenderer>
std::vector<Entity> spawnNodes(World& world, const ModelData& model, const Mat4& root,
                               AddRenderer addRenderer)
{
    // The model's roots go after the World's existing roots.
    u32 rootOrder = 0;
    world.query<const Hierarchy>().each([&](const Hierarchy& hierarchy) {
        if (hierarchy.parent.isNull() || !world.isAlive(hierarchy.parent))
        {
            ++rootOrder;
        }
    });

    const std::vector<Mat4> matrices = model.worldMatrices();
    std::vector<Entity> nodeEntities(model.nodes.size());
    std::vector<u32> childCounts(model.nodes.size(), 0);
    std::vector<Entity> entities;
    entities.reserve(model.nodes.size());
    for (usize n = 0; n < model.nodes.size(); ++n)
    {
        const ModelNode& node = model.nodes[n];
        const bool hasParent = node.parent >= 0 && static_cast<usize>(node.parent) < n; // parents come first
        const auto parent = static_cast<usize>(hasParent ? node.parent : 0);
        const Hierarchy hierarchy{hasParent ? nodeEntities[parent] : Entity{},
                                  hasParent ? childCounts[parent]++ : rootOrder++};
        const Transform transform = Transform::fromMatrix(root * matrices[n]);
        const LocalTransform local =
            LocalTransform::from(hasParent ? Transform::fromMatrix(node.localMatrix) : transform);
        const std::string name = makeValidEntityName(node.name);
        const Entity entity = world.create(Name{name}, hierarchy, local, transform);
        nodeEntities[n] = entity;
        entities.push_back(entity);

        const i32 meshIndex = node.mesh;
        if (meshIndex < 0 || static_cast<usize>(meshIndex) >= model.meshes.size())
        {
            continue;
        }
        const auto mesh = static_cast<u32>(meshIndex);
        const MeshData& data = model.meshes[mesh];
        if (data.submeshes.size() == 1)
        {
            addRenderer(entity, mesh, 0u);
            continue;
        }
        // Several submeshes: one child per submesh, at the node's origin.
        for (u32 s = 0; s < data.submeshes.size(); ++s)
        {
            const Entity child = world.create(Name{makeValidEntityName(std::format("{} ({})", name, s))},
                                              Hierarchy{entity, s}, LocalTransform{}, transform);
            entities.push_back(child);
            addRenderer(child, mesh, s);
        }
    }
    return entities;
}

} // namespace

std::vector<Entity> spawnModel(World& world, const ModelData& model, std::string_view modelPath,
                               const Mat4& root)
{
    const std::optional<std::string> path = normalizeVirtualPath(modelPath);
    ILL_ASSERT(path.has_value(), "spawnModel: '{}' is not a valid virtual path", modelPath);
    std::vector<AssetRef> meshes(model.meshes.size());
    for (usize m = 0; m < meshes.size(); ++m)
    {
        meshes[m] = AssetRef(std::format("{}#mesh/{}", path.value_or(""), m));
    }
    return spawnNodes(world, model, root, [&](Entity entity, u32 mesh, u32 submesh) {
        world.emplace<MeshRenderer>(entity, MeshRenderer{.mesh = meshes[mesh], .submesh = submesh});
    });
}

std::vector<Entity> spawnModel(World& world, const ModelData& model, const ModelHandles& handles,
                               const Mat4& root)
{
    return spawnNodes(world, model, root, [&](Entity entity, u32 mesh, u32 submesh) {
        if (mesh >= handles.meshes.size() || handles.meshes[mesh].index == 0)
        {
            return;
        }
        const i32 material = model.meshes[mesh].submeshes[submesh].material;
        RenderMesh renderer;
        renderer.mesh = handles.meshes[mesh];
        renderer.submesh = submesh;
        renderer.material = material >= 0 && static_cast<usize>(material) < handles.materials.size()
                                ? handles.materials[static_cast<usize>(material)]
                                : MaterialHandle{};
        world.emplace<RenderMesh>(entity, renderer);
    });
}

} // namespace illusion
