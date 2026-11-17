#include <illusion/assets/Model.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <array>
#include <cgltf.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <numeric>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

// --- File access through the VirtualFileSystem -------------------------------------------------------------

struct FileContext
{
    const VirtualFileSystem* files;
    std::vector<std::string>* dependencies; // may be null
};

cgltf_result readFile(const cgltf_memory_options* /*memory*/, const cgltf_file_options* file,
                      const char* path, cgltf_size* size, void** data)
{
    const auto* context = static_cast<const FileContext*>(file->user_data);
    const auto normalized = VirtualFileSystem::normalizePath(path);
    if (!normalized)
    {
        return cgltf_result_file_not_found;
    }
    std::vector<std::byte> bytes;
    if (context->files->readFile(*normalized, bytes) != FileError::None)
    {
        return cgltf_result_file_not_found;
    }
    if (context->dependencies != nullptr)
    {
        context->dependencies->push_back(*normalized);
    }
    void* copy = std::malloc(std::max<usize>(bytes.size(), 1));
    if (copy == nullptr)
    {
        return cgltf_result_out_of_memory;
    }
    std::memcpy(copy, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = copy;
    return cgltf_result_success;
}

void releaseFile(const cgltf_memory_options* /*memory*/, const cgltf_file_options* /*file*/, void* data)
{
    std::free(data);
}

struct DataDeleter
{
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};

std::string_view resultName(cgltf_result result) noexcept
{
    switch (result)
    {
    case cgltf_result_success:
        return "success";
    case cgltf_result_data_too_short:
        return "data too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid JSON";
    case cgltf_result_invalid_gltf:
        return "invalid glTF";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "I/O error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "glTF 1.0 is not supported";
    default:
        return "error";
    }
}

std::string directoryOf(std::string_view virtualPath)
{
    const usize slash = virtualPath.rfind('/');
    return std::string(slash == std::string_view::npos ? virtualPath : virtualPath.substr(0, slash + 1));
}

TextureWrap wrapFromGl(cgltf_int mode) noexcept
{
    switch (mode)
    {
    case 33071: // GL_CLAMP_TO_EDGE
        return TextureWrap::ClampToEdge;
    case 33648: // GL_MIRRORED_REPEAT
        return TextureWrap::MirrorRepeat;
    default:
        return TextureWrap::Repeat;
    }
}

// --- Conversion --------------------------------------------------------------------------------------------

// A glTF skin as imported: its index in ModelData::skins (-1 if skipped) and the Skeleton joint index of each
// glTF joint.
struct SkinImport
{
    i32 skeleton = -1;
    std::vector<i32> remap;
};

// One joint moving a vertex, before the four largest are kept.
struct Influence
{
    f32 weight = 0.0f;
    u32 joint = 0;
};

class Importer
{
public:
    Importer(const VirtualFileSystem& files, const cgltf_data& data, std::string_view path,
             std::vector<std::string>* dependencies)
        : m_files(files)
        , m_data(data)
        , m_directory(directoryOf(path))
        , m_dependencies(dependencies)
    {
    }

    bool run(ModelData& model, std::string& error)
    {
        model.materials.reserve(m_data.materials_count);
        for (cgltf_size i = 0; i < m_data.materials_count; ++i)
        {
            model.materials.push_back(convertMaterial(m_data.materials[i]));
        }
        convertSkins(model);
        model.meshes.reserve(m_data.meshes_count);
        for (cgltf_size i = 0; i < m_data.meshes_count; ++i)
        {
            MeshData mesh;
            if (!convertMesh(m_data.meshes[i], meshSkin(i), mesh, error))
            {
                return false;
            }
            model.meshes.push_back(std::move(mesh));
        }
        if (!loadImages(model, error))
        {
            return false;
        }
        convertNodes(model);
        convertAnimations(model);
        return true;
    }

private:
    TextureRef textureRef(const cgltf_texture_view& view, bool srgb)
    {
        TextureRef ref;
        if (view.texture == nullptr || view.texture->image == nullptr)
        {
            return ref;
        }
        const auto image = static_cast<usize>(view.texture->image - m_data.images);
        // An image used as colour data anywhere is decoded as sRGB.
        auto [it, inserted] = m_imageSrgb.emplace(image, srgb);
        if (!inserted && srgb)
        {
            it->second = true;
        }
        ref.image = static_cast<i32>(image);
        if (const cgltf_sampler* sampler = view.texture->sampler)
        {
            ref.wrapU = wrapFromGl(sampler->wrap_s);
            ref.wrapV = wrapFromGl(sampler->wrap_t);
        }
        if (view.texcoord != 0)
        {
            ILL_LOG_WARN("Assets", "texture uses TEXCOORD_{}; only TEXCOORD_0 is supported", view.texcoord);
        }
        return ref;
    }

    MaterialData convertMaterial(const cgltf_material& source)
    {
        MaterialData material;
        material.name = source.name != nullptr ? source.name : "";
        if (source.has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
            material.baseColorFactor = Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                            pbr.base_color_factor[2], pbr.base_color_factor[3]);
            material.metallicFactor = pbr.metallic_factor;
            material.roughnessFactor = pbr.roughness_factor;
            material.baseColorTexture = textureRef(pbr.base_color_texture, true);
            material.metallicRoughnessTexture = textureRef(pbr.metallic_roughness_texture, false);
        }
        material.emissiveFactor =
            Vec3(source.emissive_factor[0], source.emissive_factor[1], source.emissive_factor[2]);
        if (source.has_emissive_strength)
        {
            material.emissiveFactor *= source.emissive_strength.emissive_strength;
        }
        if (source.has_diffuse_transmission)
        {
            const cgltf_diffuse_transmission& transmission = source.diffuse_transmission;
            material.diffuseTransmission = transmission.diffuse_transmission_factor;
            material.diffuseTransmissionColor = Vec3(transmission.diffuse_transmission_color_factor[0],
                                                     transmission.diffuse_transmission_color_factor[1],
                                                     transmission.diffuse_transmission_color_factor[2]);
        }
        material.normalTexture = textureRef(source.normal_texture, false);
        material.normalScale = source.normal_texture.scale;
        material.occlusionTexture = textureRef(source.occlusion_texture, false);
        material.occlusionStrength =
            source.occlusion_texture.texture != nullptr ? source.occlusion_texture.scale : 1.0f;
        material.emissiveTexture = textureRef(source.emissive_texture, true);
        material.alphaCutoff = source.alpha_cutoff;
        material.doubleSided = source.double_sided;
        switch (source.alpha_mode)
        {
        case cgltf_alpha_mode_mask:
            material.alphaMode = AlphaMode::Mask;
            break;
        case cgltf_alpha_mode_blend:
            material.alphaMode = AlphaMode::Blend;
            break;
        default:
            material.alphaMode = AlphaMode::Opaque;
            break;
        }
        return material;
    }

    bool convertMesh(const cgltf_mesh& source, const SkinImport* skin, MeshData& mesh, std::string& error)
    {
        std::vector<SkinVertex> skinStream;
        bool skinned = skin != nullptr;
        for (cgltf_size p = 0; p < source.primitives_count; ++p)
        {
            const cgltf_primitive& primitive = source.primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles)
            {
                ILL_LOG_WARN("Assets", "skipping non-triangle primitive in mesh '{}'",
                             source.name ? source.name : "");
                continue;
            }
            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* tangents = nullptr;
            const cgltf_accessor* uvs = nullptr;
            std::array<const cgltf_accessor*, 2> joints{};
            std::array<const cgltf_accessor*, 2> weights{};
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a)
            {
                const cgltf_attribute& attribute = primitive.attributes[a];
                switch (attribute.type)
                {
                case cgltf_attribute_type_position:
                    positions = attribute.data;
                    break;
                case cgltf_attribute_type_normal:
                    normals = attribute.data;
                    break;
                case cgltf_attribute_type_tangent:
                    tangents = attribute.data;
                    break;
                case cgltf_attribute_type_texcoord:
                    if (attribute.index == 0)
                    {
                        uvs = attribute.data;
                    }
                    break;
                case cgltf_attribute_type_joints:
                    if (attribute.index >= 0 && attribute.index < 2)
                    {
                        joints.at(static_cast<usize>(attribute.index)) = attribute.data;
                    }
                    break;
                case cgltf_attribute_type_weights:
                    if (attribute.index >= 0 && attribute.index < 2)
                    {
                        weights.at(static_cast<usize>(attribute.index)) = attribute.data;
                    }
                    break;
                default:
                    break;
                }
            }
            if (positions == nullptr)
            {
                error = "primitive without POSITION";
                return false;
            }

            const auto baseVertex = static_cast<u32>(mesh.vertices.size());
            const cgltf_size count = positions->count;
            mesh.vertices.resize(mesh.vertices.size() + count);
            Vertex* vertices = mesh.vertices.data() + baseVertex;
            // One buffer for the widest attribute, zeroed before each unpack so that accessors with fewer
            // components than expected read as zero. (A plain array also avoids a GCC 13 -O3
            // -Wnull-dereference false positive in std::vector::resize.)
            const std::unique_ptr<f32[]> scratch = std::make_unique<f32[]>(count * 4);
            const auto unpack = [&](const cgltf_accessor* accessor, cgltf_size components) {
                std::fill_n(scratch.get(), count * 4, 0.0f);
                cgltf_accessor_unpack_floats(accessor, scratch.get(), accessor->count * components);
            };
            unpack(positions, 3);
            for (cgltf_size v = 0; v < count; ++v)
            {
                vertices[v].position = Vec3(scratch[v * 3], scratch[v * 3 + 1], scratch[v * 3 + 2]);
            }
            if (normals != nullptr && normals->count == count)
            {
                unpack(normals, 3);
                for (cgltf_size v = 0; v < count; ++v)
                {
                    vertices[v].normal = Vec3(scratch[v * 3], scratch[v * 3 + 1], scratch[v * 3 + 2]);
                }
            }
            if (tangents != nullptr && tangents->count == count)
            {
                unpack(tangents, 4);
                for (cgltf_size v = 0; v < count; ++v)
                {
                    vertices[v].tangent =
                        Vec4(scratch[v * 4], scratch[v * 4 + 1], scratch[v * 4 + 2], scratch[v * 4 + 3]);
                }
            }
            if (uvs != nullptr && uvs->count == count)
            {
                unpack(uvs, 2);
                for (cgltf_size v = 0; v < count; ++v)
                {
                    vertices[v].uv = Vec2(scratch[v * 2], scratch[v * 2 + 1]);
                }
            }
            if (skinned && !readSkinVertices(joints, weights, count, skin->remap, skinStream))
            {
                ILL_LOG_WARN("Assets", "mesh '{}' has invalid or missing JOINTS_0/WEIGHTS_0; drawn rigid",
                             source.name ? source.name : "");
                skinned = false;
            }

            Submesh submesh;
            submesh.indexOffset = static_cast<u32>(mesh.indices.size());
            if (primitive.indices != nullptr)
            {
                for (cgltf_size i = 0; i < primitive.indices->count; ++i)
                {
                    const auto index = static_cast<u32>(cgltf_accessor_read_index(primitive.indices, i));
                    if (index >= count)
                    {
                        error = "index out of range";
                        return false;
                    }
                    mesh.indices.push_back(baseVertex + index);
                }
            }
            else
            {
                for (cgltf_size i = 0; i < count; ++i)
                {
                    mesh.indices.push_back(baseVertex + static_cast<u32>(i));
                }
            }
            submesh.indexCount = static_cast<u32>(mesh.indices.size()) - submesh.indexOffset;
            submesh.indexCount -= submesh.indexCount % 3;
            mesh.indices.resize(submesh.indexOffset + submesh.indexCount);
            submesh.material =
                primitive.material != nullptr ? static_cast<i32>(primitive.material - m_data.materials) : -1;

            if (normals == nullptr)
            {
                // Compute normals for just this primitive's vertices.
                MeshData part;
                part.vertices.assign(vertices, vertices + count);
                for (u32 i = 0; i < submesh.indexCount; ++i)
                {
                    part.indices.push_back(mesh.indices[submesh.indexOffset + i] - baseVertex);
                }
                computeNormals(part);
                for (cgltf_size v = 0; v < count; ++v)
                {
                    mesh.vertices[baseVertex + v].normal = part.vertices[v].normal;
                }
            }
            mesh.submeshes.push_back(submesh);
        }
        mesh.computeBounds();
        if (skinned)
        {
            mesh.skin = std::move(skinStream);
            mesh.skeleton = skin->skeleton;
            mesh.jointBounds.assign(skin->remap.size(), Aabb{});
            for (usize v = 0; v < mesh.skin.size(); ++v)
            {
                const SkinVertex& influences = mesh.skin[v];
                for (u32 i = 0; i < kMaxJointInfluences; ++i)
                {
                    if (influences.weights.at(i) > 0)
                    {
                        mesh.jointBounds.at(influences.joints.at(i)).expand(mesh.vertices[v].position);
                    }
                }
            }
        }
        return true;
    }

    // Appends the influences of @p count vertices (JOINTS_0/WEIGHTS_0 and optionally JOINTS_1/WEIGHTS_1)
    // with joint indices remapped to the Skeleton's order. False if they are missing or out of range.
    static bool readSkinVertices(std::span<const cgltf_accessor* const, 2> joints,
                                 std::span<const cgltf_accessor* const, 2> weights, cgltf_size count,
                                 std::span<const i32> remap, std::vector<SkinVertex>& out)
    {
        const auto usable = [&](usize set) {
            return joints[set] != nullptr && weights[set] != nullptr && joints[set]->count == count &&
                   weights[set]->count == count;
        };
        if (!usable(0))
        {
            return false;
        }
        const usize sets = usable(1) ? 2 : 1;
        for (cgltf_size v = 0; v < count; ++v)
        {
            std::array<Influence, kMaxJointInfluences * 2> influences{};
            usize used = 0;
            for (usize set = 0; set < sets; ++set)
            {
                std::array<cgltf_uint, 4> index{};
                std::array<cgltf_float, 4> weight{};
                if (!cgltf_accessor_read_uint(joints[set], v, index.data(), index.size()) ||
                    !cgltf_accessor_read_float(weights[set], v, weight.data(), weight.size()))
                {
                    return false;
                }
                for (usize c = 0; c < 4; ++c)
                {
                    if (!(weight.at(c) > 0.0f) || !std::isfinite(weight.at(c)))
                    {
                        continue;
                    }
                    if (index.at(c) >= remap.size())
                    {
                        return false;
                    }
                    const auto joint = static_cast<u32>(remap[index.at(c)]);
                    const auto end = influences.begin() + static_cast<std::ptrdiff_t>(used);
                    const auto same = std::find_if(influences.begin(), end, [&](const Influence& influence) {
                        return influence.joint == joint;
                    });
                    if (same != end)
                    {
                        same->weight += weight.at(c);
                    }
                    else
                    {
                        influences.at(used++) = Influence{weight.at(c), joint};
                    }
                }
            }
            // Largest first; equal weights by joint index, so the result does not depend on attribute order.
            std::sort(influences.begin(), influences.begin() + static_cast<std::ptrdiff_t>(used),
                      [](const Influence& a, const Influence& b) {
                          return a.weight != b.weight ? a.weight > b.weight : a.joint < b.joint;
                      });
            out.push_back(
                quantizeInfluences(std::span(influences).first(std::min<usize>(used, kMaxJointInfluences))));
        }
        return true;
    }

    // Weights as unorm16 summing to exactly 65535, largest first; a vertex without weights follows joint 0.
    static SkinVertex quantizeInfluences(std::span<const Influence> influences)
    {
        SkinVertex vertex;
        f32 sum = 0.0f;
        for (const Influence& influence : influences)
        {
            sum += influence.weight;
        }
        if (influences.empty() || !(sum > 0.0f))
        {
            vertex.weights[0] = 65535;
            return vertex;
        }
        std::array<i32, kMaxJointInfluences> quantized{};
        i32 total = 0;
        for (usize i = 0; i < influences.size(); ++i)
        {
            quantized.at(i) = static_cast<i32>(std::lround(influences[i].weight / sum * 65535.0f));
            total += quantized.at(i);
        }
        quantized[0] += 65535 - total;
        std::array<usize, kMaxJointInfluences> order{0, 1, 2, 3};
        std::stable_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(influences.size()),
                         [&](usize a, usize b) { return quantized.at(a) > quantized.at(b); });
        for (usize i = 0; i < influences.size(); ++i)
        {
            vertex.joints.at(i) = static_cast<u16>(influences[order.at(i)].joint);
            vertex.weights.at(i) = static_cast<u16>(quantized.at(order.at(i)));
        }
        return vertex;
    }

    bool loadImages(ModelData& model, std::string& error)
    {
        model.images.resize(m_data.images_count);
        for (cgltf_size i = 0; i < m_data.images_count; ++i)
        {
            const auto used = m_imageSrgb.find(i);
            if (used == m_imageSrgb.end())
            {
                continue; // not referenced by any material
            }
            const cgltf_image& image = m_data.images[i];
            std::vector<std::byte> encoded;
            if (image.buffer_view != nullptr)
            {
                const cgltf_buffer_view& view = *image.buffer_view;
                if (view.buffer->data == nullptr)
                {
                    error = "image buffer not loaded";
                    return false;
                }
                const auto* bytes = static_cast<const std::byte*>(view.buffer->data) + view.offset;
                encoded.assign(bytes, bytes + view.size);
            }
            else if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0)
            {
                const char* comma = std::strchr(image.uri, ',');
                if (comma == nullptr || std::strstr(image.uri, ";base64") == nullptr)
                {
                    error = "unsupported data URI for image";
                    return false;
                }
                const usize base64Length = std::strlen(comma + 1);
                const usize size = base64Length / 4 * 3;
                cgltf_options options{};
                void* decoded = nullptr;
                usize padding = 0;
                for (usize p = base64Length; p > 0 && comma[p] == '='; --p)
                {
                    ++padding;
                }
                // Malformed lengths would make `size - padding` wrap around and read past the URI.
                if (base64Length == 0 || base64Length % 4 != 0 || padding > 2 ||
                    cgltf_load_buffer_base64(&options, size - padding, comma + 1, &decoded) !=
                        cgltf_result_success)
                {
                    error = "invalid base64 image data";
                    return false;
                }
                const auto* bytes = static_cast<const std::byte*>(decoded);
                encoded.assign(bytes, bytes + (size - padding));
                std::free(decoded);
            }
            else if (image.uri != nullptr)
            {
                std::string uri = image.uri;
                uri.resize(cgltf_decode_uri(uri.data()));
                const auto path = VirtualFileSystem::normalizePath(m_directory + uri);
                if (!path || m_files.readFile(*path, encoded) != FileError::None)
                {
                    error = std::format("cannot read image '{}'", uri);
                    return false;
                }
                if (m_dependencies != nullptr)
                {
                    m_dependencies->push_back(*path);
                }
            }
            std::string decodeError;
            auto decoded = decodeImage(encoded, used->second, &decodeError);
            if (!decoded)
            {
                error = std::format("cannot decode image {}: {}", i, decodeError);
                return false;
            }
            generateMips(*decoded);
            model.images[i] = std::move(*decoded);
        }
        return true;
    }

    void convertNodes(ModelData& model)
    {
        // Emit the default scene's nodes so parents always precede children. An explicit stack keeps
        // deep hierarchies in untrusted files from overflowing the call stack (cgltf_validate rejects
        // cycles).
        const cgltf_scene* scene =
            m_data.scene != nullptr ? m_data.scene : (m_data.scenes_count > 0 ? m_data.scenes : nullptr);
        std::vector<std::pair<const cgltf_node*, i32>> pending;
        if (scene != nullptr)
        {
            for (cgltf_size i = scene->nodes_count; i > 0; --i)
            {
                pending.emplace_back(scene->nodes[i - 1], -1);
            }
        }
        else
        {
            for (cgltf_size i = m_data.nodes_count; i > 0; --i)
            {
                if (m_data.nodes[i - 1].parent == nullptr)
                {
                    pending.emplace_back(&m_data.nodes[i - 1], -1);
                }
            }
        }
        while (!pending.empty())
        {
            const auto [node, parent] = pending.back();
            pending.pop_back();
            ModelNode out;
            out.name = node->name != nullptr ? node->name : "";
            cgltf_node_transform_local(node, &out.localMatrix[0][0]);
            out.parent = parent;
            out.mesh = node->mesh != nullptr ? static_cast<i32>(node->mesh - m_data.meshes) : -1;
            model.nodes.push_back(std::move(out));
            const auto index = static_cast<i32>(model.nodes.size() - 1);
            for (cgltf_size c = node->children_count; c > 0; --c)
            {
                pending.emplace_back(node->children[c - 1], index);
            }
        }
    }

    // --- Skins and animations ------------------------------------------------------------------------------

    // The skin of the first node that draws mesh @p mesh with one, or null.
    const SkinImport* meshSkin(cgltf_size mesh) const
    {
        for (cgltf_size i = 0; i < m_data.nodes_count; ++i)
        {
            const cgltf_node& node = m_data.nodes[i];
            if (node.mesh == m_data.meshes + mesh && node.skin != nullptr)
            {
                const SkinImport& skin = m_skins.at(static_cast<usize>(node.skin - m_data.skins));
                return skin.skeleton >= 0 ? &skin : nullptr;
            }
        }
        return nullptr;
    }

    std::string nodeName(const cgltf_node* node) const
    {
        return node->name != nullptr && node->name[0] != '\0'
                   ? std::string(node->name)
                   : std::format("node{}", static_cast<usize>(node - m_data.nodes));
    }

    static const cgltf_node* commonAncestor(const cgltf_node* a, const cgltf_node* b) noexcept
    {
        for (const cgltf_node* x = a; x != nullptr; x = x->parent)
        {
            for (const cgltf_node* y = b; y != nullptr; y = y->parent)
            {
                if (x == y)
                {
                    return x;
                }
            }
        }
        return nullptr;
    }

    static void decompose(const cgltf_node& node, SkeletonJoint& joint)
    {
        if (node.has_matrix)
        {
            Mat4 matrix(1.0f);
            std::memcpy(&matrix[0][0], node.matrix, sizeof(matrix));
            joint.translation = Vec3(matrix[3]);
            joint.scale = Vec3(glm::length(Vec3(matrix[0])), glm::length(Vec3(matrix[1])),
                               glm::length(Vec3(matrix[2])));
            if (glm::determinant(glm::mat3(matrix)) < 0.0f)
            {
                joint.scale.x = -joint.scale.x;
            }
            if (joint.scale.x != 0.0f && joint.scale.y != 0.0f && joint.scale.z != 0.0f)
            {
                const glm::mat3 rotation(Vec3(matrix[0]) / joint.scale.x, Vec3(matrix[1]) / joint.scale.y,
                                         Vec3(matrix[2]) / joint.scale.z);
                joint.rotation = glm::normalize(glm::quat_cast(rotation));
            }
            return;
        }
        if (node.has_translation)
        {
            joint.translation = Vec3(node.translation[0], node.translation[1], node.translation[2]);
        }
        if (node.has_rotation)
        {
            joint.rotation =
                glm::normalize(Quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
        }
        if (node.has_scale)
        {
            joint.scale = Vec3(node.scale[0], node.scale[1], node.scale[2]);
        }
    }

    void convertSkins(ModelData& model)
    {
        m_skins.resize(m_data.skins_count);
        for (cgltf_size s = 0; s < m_data.skins_count; ++s)
        {
            const cgltf_skin& skin = m_data.skins[s];
            const std::string skinName = skin.name != nullptr ? skin.name : "";
            const usize count = skin.joints_count;
            if (count == 0 || count > kMaxJoints)
            {
                ILL_LOG_WARN("Assets", "skipping skin '{}' with {} joints (1 to {} are supported)", skinName,
                             count, kMaxJoints);
                continue;
            }
            std::unordered_map<const cgltf_node*, usize> jointOf;
            for (usize j = 0; j < count; ++j)
            {
                jointOf.emplace(skin.joints[j], j);
            }
            if (jointOf.size() != count)
            {
                ILL_LOG_WARN("Assets", "skipping skin '{}': a node is listed as a joint twice", skinName);
                continue;
            }
            // Parent of each joint: its nearest ancestor that is a joint of this skin.
            std::vector<i32> parentOf(count, -1);
            bool skipsNodes = false;
            for (usize j = 0; j < count; ++j)
            {
                for (const cgltf_node* node = skin.joints[j]->parent; node != nullptr; node = node->parent)
                {
                    if (const auto found = jointOf.find(node); found != jointOf.end())
                    {
                        parentOf.at(j) = static_cast<i32>(found->second);
                        skipsNodes = skipsNodes || node != skin.joints[j]->parent;
                        break;
                    }
                }
            }
            if (skipsNodes)
            {
                ILL_LOG_WARN("Assets", "skin '{}' has nodes between joints; their transforms are ignored",
                             skinName);
            }
            std::vector<u32> depth(count, 0);
            for (usize j = 0; j < count; ++j)
            {
                for (i32 parent = parentOf.at(j); parent >= 0;
                     parent = parentOf.at(static_cast<usize>(parent)))
                {
                    ++depth.at(j);
                }
            }
            std::vector<usize> order(count);
            std::iota(order.begin(), order.end(), usize{0});
            std::stable_sort(order.begin(), order.end(),
                             [&](usize a, usize b) { return depth.at(a) < depth.at(b); });

            SkinImport& imported = m_skins.at(s);
            imported.remap.assign(count, -1);
            for (usize k = 0; k < count; ++k)
            {
                imported.remap.at(order.at(k)) = static_cast<i32>(k);
            }
            Skeleton skeleton;
            skeleton.name = skinName;
            skeleton.joints.resize(count);
            const cgltf_accessor* inverseBinds = skin.inverse_bind_matrices;
            const cgltf_node* ancestor = nullptr;
            bool firstRoot = true;
            bool rootsDiffer = false;
            for (usize k = 0; k < count; ++k)
            {
                const usize j = order.at(k);
                const cgltf_node* node = skin.joints[j];
                SkeletonJoint& joint = skeleton.joints.at(k);
                joint.name = nodeName(node);
                joint.parent =
                    parentOf.at(j) >= 0 ? imported.remap.at(static_cast<usize>(parentOf.at(j))) : -1;
                decompose(*node, joint);
                if (inverseBinds != nullptr && inverseBinds->count >= count)
                {
                    cgltf_accessor_read_float(inverseBinds, j, &joint.inverseBind[0][0], 16);
                }
                if (joint.parent < 0)
                {
                    rootsDiffer = rootsDiffer || (!firstRoot && node->parent != ancestor);
                    ancestor = firstRoot ? node->parent : commonAncestor(ancestor, node->parent);
                    firstRoot = false;
                }
            }
            if (rootsDiffer)
            {
                ILL_LOG_WARN("Assets", "the root joints of skin '{}' have different parents", skinName);
            }
            if (ancestor != nullptr)
            {
                cgltf_node_transform_world(ancestor, &skeleton.rootTransform[0][0]);
            }
            imported.skeleton = static_cast<i32>(model.skins.size());
            model.skins.push_back(std::move(skeleton));
        }
    }

    // A name for animation @p index that `#animation/<name>` accepts and no earlier animation has.
    static std::string animationName(const cgltf_animation& animation, usize index,
                                     const std::vector<AnimationClip>& earlier)
    {
        std::string base = animation.name != nullptr ? animation.name : "";
        std::replace_if(
            base.begin(), base.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }, '_');
        if (base.empty())
        {
            base = std::format("animation{}", index);
        }
        const auto taken = [&](const std::string& name) {
            return std::any_of(earlier.begin(), earlier.end(),
                               [&](const AnimationClip& clip) { return clip.name == name; });
        };
        std::string name = base;
        for (u32 suffix = 2; taken(name); ++suffix)
        {
            name = std::format("{}.{}", base, suffix);
        }
        return name;
    }

    void convertAnimations(ModelData& model)
    {
        std::unordered_set<const cgltf_node*> joints;
        for (cgltf_size s = 0; s < m_data.skins_count; ++s)
        {
            if (m_skins.at(s).skeleton >= 0)
            {
                joints.insert(m_data.skins[s].joints, m_data.skins[s].joints + m_data.skins[s].joints_count);
            }
        }
        for (cgltf_size a = 0; a < m_data.animations_count; ++a)
        {
            const cgltf_animation& animation = m_data.animations[a];
            AnimationClip clip;
            clip.name = animationName(animation, a, model.animations);
            for (cgltf_size c = 0; c < animation.channels_count; ++c)
            {
                AnimationChannel channel;
                if (convertChannel(animation.channels[c], clip.name, joints, channel))
                {
                    clip.duration = std::max(clip.duration, channel.times.back());
                    clip.channels.push_back(std::move(channel));
                }
            }
            if (clip.channels.empty())
            {
                ILL_LOG_INFO("Assets", "skipping animation '{}': it moves no joint", clip.name);
                continue;
            }
            std::stable_sort(clip.channels.begin(), clip.channels.end(),
                             [](const AnimationChannel& x, const AnimationChannel& y) {
                                 return x.joint != y.joint ? x.joint < y.joint : x.path < y.path;
                             });
            model.animations.push_back(std::move(clip));
        }
    }

    bool convertChannel(const cgltf_animation_channel& source, std::string_view clip,
                        const std::unordered_set<const cgltf_node*>& joints, AnimationChannel& channel) const
    {
        if (source.target_node == nullptr || source.sampler == nullptr)
        {
            return false;
        }
        switch (source.target_path)
        {
        case cgltf_animation_path_type_translation:
            channel.path = AnimationPath::Translation;
            break;
        case cgltf_animation_path_type_rotation:
            channel.path = AnimationPath::Rotation;
            break;
        case cgltf_animation_path_type_scale:
            channel.path = AnimationPath::Scale;
            break;
        default:
            ILL_LOG_INFO("Assets", "animation '{}': skipping a morph target weights channel", clip);
            return false;
        }
        if (!joints.contains(source.target_node))
        {
            ILL_LOG_DEBUG("Assets", "animation '{}': skipping a channel of node '{}', which is not a joint",
                          clip, nodeName(source.target_node));
            return false;
        }
        const cgltf_animation_sampler& sampler = *source.sampler;
        switch (sampler.interpolation)
        {
        case cgltf_interpolation_type_step:
            channel.interpolation = AnimationInterpolation::Step;
            break;
        case cgltf_interpolation_type_cubic_spline:
            channel.interpolation = AnimationInterpolation::CubicSpline;
            break;
        default:
            channel.interpolation = AnimationInterpolation::Linear;
            break;
        }
        channel.joint = nodeName(source.target_node);
        const cgltf_size keys = sampler.input != nullptr ? sampler.input->count : 0;
        const cgltf_size perKey = channel.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
        const cgltf_size components = channel.path == AnimationPath::Rotation ? 4 : 3;
        if (keys == 0 || sampler.output == nullptr || sampler.output->count != keys * perKey)
        {
            ILL_LOG_WARN("Assets", "animation '{}': skipping a channel of '{}' with mismatched keyframes",
                         clip, channel.joint);
            return false;
        }
        const std::unique_ptr<f32[]> times = std::make_unique<f32[]>(keys);
        cgltf_accessor_unpack_floats(sampler.input, times.get(), keys);
        for (cgltf_size i = 0; i < keys; ++i)
        {
            if (!std::isfinite(times[i]) || (i > 0 && !(times[i] > times[i - 1])))
            {
                ILL_LOG_WARN("Assets",
                             "animation '{}': skipping a channel of '{}' whose times do not increase", clip,
                             channel.joint);
                return false;
            }
        }
        channel.times.assign(times.get(), times.get() + keys);
        const std::unique_ptr<f32[]> values = std::make_unique<f32[]>(keys * perKey * components);
        cgltf_accessor_unpack_floats(sampler.output, values.get(), keys * perKey * components);
        channel.values.reserve(keys * perKey);
        for (cgltf_size i = 0; i < keys * perKey; ++i)
        {
            const f32* value = values.get() + i * components;
            channel.values.emplace_back(value[0], value[1], value[2], components == 4 ? value[3] : 0.0f);
        }
        return true;
    }

    const VirtualFileSystem& m_files;
    const cgltf_data& m_data;
    std::string m_directory;
    std::vector<std::string>* m_dependencies;
    std::unordered_map<usize, bool> m_imageSrgb;
    std::vector<SkinImport> m_skins;
};

} // namespace

void scaleModel(ModelData& model, f32 scale)
{
    ILL_ASSERT(scale > 0.0f, "scaleModel needs a positive scale");
    if (scale == 1.0f)
    {
        return;
    }
    for (MeshData& mesh : model.meshes)
    {
        for (Vertex& vertex : mesh.vertices)
        {
            vertex.position *= scale;
        }
        mesh.computeBounds();
        for (Aabb& box : mesh.jointBounds)
        {
            if (box.isValid())
            {
                box.min *= scale;
                box.max *= scale;
            }
        }
    }
    // Skeletons and clips are scaled the same way as the nodes below.
    const auto scaleTranslation = [scale](Mat4& matrix) {
        matrix[3] = Vec4(Vec3(matrix[3]) * scale, matrix[3].w);
    };
    for (Skeleton& skeleton : model.skins)
    {
        scaleTranslation(skeleton.rootTransform);
        for (SkeletonJoint& joint : skeleton.joints)
        {
            joint.translation *= scale;
            scaleTranslation(joint.inverseBind);
        }
    }
    for (AnimationClip& clip : model.animations)
    {
        for (AnimationChannel& channel : clip.channels)
        {
            if (channel.path == AnimationPath::Translation)
            {
                for (Vec4& value : channel.values)
                {
                    value = Vec4(Vec3(value) * scale, value.w);
                }
            }
        }
    }
    // S * M * S^-1 for every local matrix: the world matrices become S * W * S^-1, which applied to the
    // scaled positions gives S * W * p.
    for (ModelNode& node : model.nodes)
    {
        node.localMatrix[3] = Vec4(Vec3(node.localMatrix[3]) * scale, node.localMatrix[3].w);
    }
}

std::vector<Mat4> ModelData::worldMatrices() const
{
    std::vector<Mat4> world(nodes.size(), Mat4(1.0f));
    for (usize i = 0; i < nodes.size(); ++i)
    {
        const ModelNode& node = nodes[i];
        world[i] =
            node.parent >= 0 ? world[static_cast<usize>(node.parent)] * node.localMatrix : node.localMatrix;
    }
    return world;
}

Aabb ModelData::bounds() const
{
    Aabb box;
    const std::vector<Mat4> world = worldMatrices();
    for (usize i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].mesh >= 0)
        {
            box.expand(meshes[static_cast<usize>(nodes[i].mesh)].bounds.transformed(world[i]));
        }
    }
    return box;
}

std::optional<ModelData> loadGltf(const VirtualFileSystem& files, std::string_view virtualPath,
                                  std::string* error, std::vector<std::string>* dependencies)
{
    ILL_PROFILE_SCOPE();
    const auto fail = [&](std::string message) -> std::optional<ModelData> {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", virtualPath, message);
        }
        return std::nullopt;
    };

    std::vector<std::byte> file;
    if (const FileError result = files.readFile(virtualPath, file); result != FileError::None)
    {
        return fail(std::string(fileErrorName(result)));
    }

    if (dependencies != nullptr)
    {
        dependencies->clear();
        if (const auto normalized = VirtualFileSystem::normalizePath(virtualPath))
        {
            dependencies->push_back(*normalized);
        }
    }
    FileContext context{&files, dependencies};
    cgltf_options options{};
    options.file.read = &readFile;
    options.file.release = &releaseFile;
    options.file.user_data = &context;

    cgltf_data* raw = nullptr;
    cgltf_result result = cgltf_parse(&options, file.data(), file.size(), &raw);
    std::unique_ptr<cgltf_data, DataDeleter> data(raw);
    if (result != cgltf_result_success)
    {
        return fail(std::string(resultName(result)));
    }
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
    {
        const std::string_view extension = data->extensions_required[i];
        if (extension != "KHR_mesh_quantization" && extension != "KHR_materials_emissive_strength" &&
            extension != "KHR_materials_diffuse_transmission")
        {
            return fail(std::format("required extension {} is not supported", extension));
        }
    }
    const std::string path(virtualPath);
    result = cgltf_load_buffers(&options, data.get(), path.c_str());
    if (result != cgltf_result_success)
    {
        return fail(std::format("cannot load buffers: {}", resultName(result)));
    }
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success)
    {
        return fail(std::format("validation failed: {}", resultName(result)));
    }

    ModelData model;
    std::string message;
    Importer importer(files, *data, virtualPath, dependencies);
    if (!importer.run(model, message))
    {
        return fail(message);
    }
    ILL_LOG_DEBUG("Assets",
                  "loaded {}: {} meshes, {} materials, {} images, {} nodes, {} skins, {} animations",
                  virtualPath, model.meshes.size(), model.materials.size(), model.images.size(),
                  model.nodes.size(), model.skins.size(), model.animations.size());
    return model;
}

} // namespace illusion
