#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>

namespace illusion
{

void reflectRenderComponents(ComponentRegistry& registry)
{
    reflect<Camera>("illusion.Camera", 1, registry)
        .field("fovY", &Camera::fovY)
        .field("nearPlane", &Camera::nearPlane)
        .field("exposure", &Camera::exposure);
    reflect<DirectionalLight>("illusion.DirectionalLight", 1, registry)
        .field("color", &DirectionalLight::color)
        .field("intensity", &DirectionalLight::intensity)
        .field("castShadows", &DirectionalLight::castShadows)
        .field("angularDiameter", &DirectionalLight::angularDiameter);
    reflect<PointLight>("illusion.PointLight", 1, registry)
        .field("color", &PointLight::color)
        .field("intensity", &PointLight::intensity)
        .field("range", &PointLight::range);
}

std::vector<Entity> spawnModel(World& world, const ModelData& model, const ModelHandles& handles,
                               const Mat4& root)
{
    std::vector<Entity> entities;
    const std::vector<Mat4> matrices = model.worldMatrices();
    for (usize n = 0; n < model.nodes.size(); ++n)
    {
        const i32 meshIndex = model.nodes[n].mesh;
        if (meshIndex < 0 || static_cast<usize>(meshIndex) >= handles.meshes.size())
        {
            continue;
        }
        const MeshHandle mesh = handles.meshes[static_cast<usize>(meshIndex)];
        if (mesh.index == 0)
        {
            continue;
        }
        const Transform transform = Transform::fromMatrix(root * matrices[n]);
        const MeshData& data = model.meshes[static_cast<usize>(meshIndex)];
        for (u32 s = 0; s < data.submeshes.size(); ++s)
        {
            const i32 material = data.submeshes[s].material;
            MeshRenderer renderer;
            renderer.mesh = mesh;
            renderer.submesh = s;
            renderer.material = material >= 0 && static_cast<usize>(material) < handles.materials.size()
                                    ? handles.materials[static_cast<usize>(material)]
                                    : MaterialHandle{};
            entities.push_back(world.create(transform, renderer));
        }
    }
    return entities;
}

} // namespace illusion
