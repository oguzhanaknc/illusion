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
        .meta({.displayName = "Shadows",
               .description = "Cascaded soft shadows of the first shadow-casting light.",
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
        .field("slopeBias", &ShadowSettings::slopeBias, {.min = 0.0, .advanced = true});
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

    reflect<Camera>("illusion.Camera", 1, registry)
        .meta({.displayName = "Camera",
               .category = "Rendering",
               .icon = "videocam",
               .description = "Perspective camera looking along the Transform's -z axis.",
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
        .field("exposure", &Camera::exposure, {.min = 0.0, .hint = FieldHint::LogSlider});
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
        .field("range", &PointLight::range, {.unit = Unit::Meters, .min = 0.0});
    reflectRuntime<MotionHistory>(
        "illusion.MotionHistory",
        {.displayName = "Motion history",
         .description = "Previous world matrix for motion vectors (runtime state)."},
        registry);
}

std::vector<Entity> spawnModel(World& world, const ModelData& model, const ModelHandles& handles,
                               const Mat4& root)
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
        if (meshIndex < 0 || static_cast<usize>(meshIndex) >= handles.meshes.size() ||
            handles.meshes[static_cast<usize>(meshIndex)].index == 0)
        {
            continue;
        }
        const MeshData& data = model.meshes[static_cast<usize>(meshIndex)];
        const auto rendererOf = [&](u32 submesh) {
            const i32 material = data.submeshes[submesh].material;
            MeshRenderer renderer;
            renderer.mesh = handles.meshes[static_cast<usize>(meshIndex)];
            renderer.submesh = submesh;
            renderer.material = material >= 0 && static_cast<usize>(material) < handles.materials.size()
                                    ? handles.materials[static_cast<usize>(material)]
                                    : MaterialHandle{};
            return renderer;
        };
        if (data.submeshes.size() == 1)
        {
            world.emplace<MeshRenderer>(entity, rendererOf(0));
            continue;
        }
        // Several submeshes: one child per submesh, at the node's origin.
        for (u32 s = 0; s < data.submeshes.size(); ++s)
        {
            entities.push_back(world.create(Name{makeValidEntityName(std::format("{} ({})", name, s))},
                                            Hierarchy{entity, s}, LocalTransform{}, transform,
                                            rendererOf(s)));
        }
    }
    return entities;
}

} // namespace illusion
