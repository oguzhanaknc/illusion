#include <illusion/assets/Model.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <cgltf.h>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <unordered_map>
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
        model.meshes.reserve(m_data.meshes_count);
        for (cgltf_size i = 0; i < m_data.meshes_count; ++i)
        {
            MeshData mesh;
            if (!convertMesh(m_data.meshes[i], mesh, error))
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

    bool convertMesh(const cgltf_mesh& source, MeshData& mesh, std::string& error)
    {
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
        return true;
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

    const VirtualFileSystem& m_files;
    const cgltf_data& m_data;
    std::string m_directory;
    std::vector<std::string>* m_dependencies;
    std::unordered_map<usize, bool> m_imageSrgb;
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
        if (extension != "KHR_mesh_quantization" && extension != "KHR_materials_emissive_strength")
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
    ILL_LOG_DEBUG("Assets", "loaded {}: {} meshes, {} materials, {} images, {} nodes", virtualPath,
                  model.meshes.size(), model.materials.size(), model.images.size(), model.nodes.size());
    return model;
}

} // namespace illusion
