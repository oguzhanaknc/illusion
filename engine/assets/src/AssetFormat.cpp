#include <illusion/assets/AssetFormat.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Serialization.hpp>

#include <cmath>
#include <format>
#include <utility>

namespace illusion
{
namespace
{

constexpr u32 kMagic = 0x414C4C49; // "ILLA"
constexpr u32 kContainerVersion = 1;

static_assert(sizeof(usize) == 8, "cooked formats assume 64-bit sizes");

void setError(std::string* error, std::string message)
{
    if (error != nullptr)
    {
        *error = std::move(message);
    }
}

// Fields are written one by one so that padding never reaches the file (deterministic output).
void writeTexture(BinaryWriter& writer, const TextureRef& texture)
{
    writer.write(texture.image);
    writer.write(static_cast<u8>(texture.wrapU));
    writer.write(static_cast<u8>(texture.wrapV));
}

bool readTexture(BinaryReader& reader, TextureRef& texture, usize imageCount)
{
    u8 wrapU = 0;
    u8 wrapV = 0;
    if (!reader.read(texture.image) || !reader.read(wrapU) || !reader.read(wrapV) || wrapU > 2 || wrapV > 2 ||
        texture.image < -1 || (texture.image >= 0 && static_cast<usize>(texture.image) >= imageCount))
    {
        reader.fail();
        return false;
    }
    texture.wrapU = static_cast<TextureWrap>(wrapU);
    texture.wrapV = static_cast<TextureWrap>(wrapV);
    return true;
}

void writeImage(BinaryWriter& writer, const Image& image)
{
    writer.write(static_cast<u8>(image.format));
    writer.writeVector(std::span<const ImageMip>(image.mips));
    writer.writeVector(std::span<const std::byte>(image.data));
}

bool readImage(BinaryReader& reader, Image& image)
{
    u8 format = 0;
    if (!reader.read(format) || format > static_cast<u8>(ImageFormat::BC6H) ||
        !reader.readVector(image.mips) || !reader.readVector(image.data))
    {
        reader.fail();
        return false;
    }
    image.format = static_cast<ImageFormat>(format);
    for (const ImageMip& mip : image.mips)
    {
        if (mip.width == 0 || mip.height == 0 ||
            mip.size != imageLevelSize(image.format, mip.width, mip.height) ||
            mip.offset > image.data.size() || mip.size > image.data.size() - mip.offset)
        {
            reader.fail();
            return false;
        }
    }
    return true;
}

void writeSkeleton(BinaryWriter& writer, const Skeleton& skeleton)
{
    writer.writeString(skeleton.name);
    writer.write(skeleton.rootTransform);
    writer.write(static_cast<u32>(skeleton.joints.size()));
    for (const SkeletonJoint& joint : skeleton.joints)
    {
        writer.writeString(joint.name);
        writer.write(joint.parent);
        writer.write(joint.translation);
        writer.write(joint.rotation);
        writer.write(joint.scale);
        writer.write(joint.inverseBind);
    }
}

bool readSkeleton(BinaryReader& reader, Skeleton& skeleton)
{
    u32 count = 0;
    constexpr usize kMinJointSize = 4 + 4 + 12 + 16 + 12 + 64;
    if (!reader.readString(skeleton.name) || !reader.read(skeleton.rootTransform) || !reader.read(count) ||
        count > kMaxJoints || usize{count} * kMinJointSize > reader.remaining())
    {
        return false;
    }
    skeleton.joints.resize(count);
    for (usize i = 0; i < skeleton.joints.size(); ++i)
    {
        SkeletonJoint& joint = skeleton.joints[i];
        reader.readString(joint.name);
        reader.read(joint.parent);
        reader.read(joint.translation);
        reader.read(joint.rotation);
        reader.read(joint.scale);
        reader.read(joint.inverseBind);
        // Parents precede children (localToModel() relies on it).
        if (!reader.ok() || joint.parent < -1 || joint.parent >= static_cast<i32>(i))
        {
            return false;
        }
    }
    return true;
}

// A rigid mesh has no skin data; a skinned one has a SkinVertex per vertex whose joints exist in its
// skeleton and whose weights sum to 1, and a box per joint.
bool isValidSkin(const MeshData& mesh, std::span<const Skeleton> skins)
{
    if (mesh.skeleton < 0)
    {
        return mesh.skeleton == -1 && mesh.skin.empty() && mesh.jointBounds.empty();
    }
    if (static_cast<usize>(mesh.skeleton) >= skins.size())
    {
        return false;
    }
    const usize joints = skins[static_cast<usize>(mesh.skeleton)].joints.size();
    if (mesh.skin.size() != mesh.vertices.size() || mesh.jointBounds.size() != joints)
    {
        return false;
    }
    for (const SkinVertex& vertex : mesh.skin)
    {
        u32 sum = 0;
        for (u32 i = 0; i < kMaxJointInfluences; ++i)
        {
            sum += vertex.weights[i];
            if (vertex.joints[i] >= joints)
            {
                return false;
            }
        }
        if (sum != 65535)
        {
            return false;
        }
    }
    return true;
}

void writeClip(BinaryWriter& writer, const AnimationClip& clip)
{
    writer.writeString(clip.name);
    writer.write(clip.duration);
    writer.write(static_cast<u32>(clip.channels.size()));
    for (const AnimationChannel& channel : clip.channels)
    {
        writer.writeString(channel.joint);
        writer.write(static_cast<u8>(channel.path));
        writer.write(static_cast<u8>(channel.interpolation));
        writer.writeVector(std::span<const f32>(channel.times));
        writer.writeVector(std::span<const Vec4>(channel.values));
    }
}

bool readClip(BinaryReader& reader, AnimationClip& clip)
{
    u32 count = 0;
    constexpr usize kMinChannelSize = 4 + 1 + 1 + 8 + 8;
    if (!reader.readString(clip.name) || !reader.read(clip.duration) || !reader.read(count) ||
        usize{count} * kMinChannelSize > reader.remaining() || !std::isfinite(clip.duration) ||
        clip.duration < 0.0f)
    {
        return false;
    }
    clip.channels.resize(count);
    for (AnimationChannel& channel : clip.channels)
    {
        u8 path = 0;
        u8 interpolation = 0;
        if (!reader.readString(channel.joint) || !reader.read(path) || !reader.read(interpolation) ||
            !reader.readVector(channel.times) || !reader.readVector(channel.values) ||
            path > static_cast<u8>(AnimationPath::Scale) ||
            interpolation > static_cast<u8>(AnimationInterpolation::CubicSpline) || channel.times.empty())
        {
            return false;
        }
        channel.path = static_cast<AnimationPath>(path);
        channel.interpolation = static_cast<AnimationInterpolation>(interpolation);
        const usize perKey = channel.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
        if (channel.values.size() != channel.times.size() * perKey || !std::isfinite(channel.times.front()) ||
            !(channel.times.back() <= clip.duration))
        {
            return false;
        }
        for (usize i = 1; i < channel.times.size(); ++i)
        {
            if (!(channel.times[i] > channel.times[i - 1]) || !std::isfinite(channel.times[i]))
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::vector<std::byte> writeAssetFile(const AssetFile& file)
{
    std::vector<std::byte> bytes;
    bytes.reserve(file.payload.size() + 256);
    BinaryWriter writer(bytes);
    writer.write(kMagic);
    writer.write(kContainerVersion);
    writer.write(static_cast<u32>(file.type));
    writer.write(file.version);
    writer.write(static_cast<u32>(file.dependencies.size()));
    for (const AssetDependency& dependency : file.dependencies)
    {
        writer.writeString(dependency.path);
        writer.write(dependency.contentHash);
    }
    writer.write(static_cast<u64>(file.payload.size()));
    writer.write(xxhash64(file.payload));
    writer.writeBytes(file.payload);
    return bytes;
}

std::optional<AssetFile> readAssetFile(std::span<const std::byte> bytes, std::string* error)
{
    BinaryReader reader(bytes);
    u32 magic = 0;
    u32 containerVersion = 0;
    u32 type = 0;
    AssetFile file;
    if (!reader.read(magic) || magic != kMagic)
    {
        setError(error, "not an asset file");
        return std::nullopt;
    }
    if (!reader.read(containerVersion) || containerVersion != kContainerVersion)
    {
        setError(error, std::format("unsupported asset container version {}", containerVersion));
        return std::nullopt;
    }
    u32 dependencyCount = 0;
    reader.read(type);
    reader.read(file.version);
    reader.read(dependencyCount);
    if (!reader.ok() || dependencyCount > reader.remaining())
    {
        setError(error, "truncated asset file");
        return std::nullopt;
    }
    file.type = static_cast<AssetType>(type);
    file.dependencies.resize(dependencyCount);
    for (AssetDependency& dependency : file.dependencies)
    {
        reader.readString(dependency.path);
        reader.read(dependency.contentHash);
    }
    u64 payloadSize = 0;
    u64 checksum = 0;
    reader.read(payloadSize);
    reader.read(checksum);
    if (!reader.ok() || payloadSize != reader.remaining())
    {
        setError(error, "truncated asset file");
        return std::nullopt;
    }
    const std::span<const std::byte> payload = reader.view(static_cast<usize>(payloadSize));
    if (xxhash64(payload) != checksum)
    {
        setError(error, "asset file checksum mismatch");
        return std::nullopt;
    }
    file.payload.assign(payload.begin(), payload.end());
    return file;
}

std::vector<std::byte> serializeImage(const Image& image)
{
    std::vector<std::byte> bytes;
    BinaryWriter writer(bytes);
    writeImage(writer, image);
    return bytes;
}

std::optional<Image> deserializeImage(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    Image image;
    if (!readImage(reader, image) || reader.remaining() != 0)
    {
        return std::nullopt;
    }
    return image;
}

std::vector<std::byte> serializeModel(const ModelData& model)
{
    std::vector<std::byte> bytes;
    BinaryWriter writer(bytes);
    writer.write(static_cast<u32>(model.meshes.size()));
    for (const MeshData& mesh : model.meshes)
    {
        writer.writeVector(std::span<const Vertex>(mesh.vertices));
        writer.writeVector(std::span<const u32>(mesh.indices));
        writer.writeVector(std::span<const Submesh>(mesh.submeshes));
        writer.write(mesh.bounds);
        writer.writeVector(std::span<const SkinVertex>(mesh.skin));
        writer.writeVector(std::span<const Aabb>(mesh.jointBounds));
        writer.write(mesh.skeleton);
    }
    writer.write(static_cast<u32>(model.images.size()));
    for (const Image& image : model.images)
    {
        writeImage(writer, image);
    }
    writer.write(static_cast<u32>(model.materials.size()));
    for (const MaterialData& material : model.materials)
    {
        writer.writeString(material.name);
        writer.write(material.baseColorFactor);
        writer.write(material.metallicFactor);
        writer.write(material.roughnessFactor);
        writer.write(material.emissiveFactor);
        writer.write(material.normalScale);
        writer.write(material.occlusionStrength);
        writer.write(material.alphaCutoff);
        writer.write(static_cast<u8>(material.alphaMode));
        writer.write(static_cast<u8>(material.doubleSided ? 1 : 0));
        writer.write(material.windSway);
        writer.write(material.windFlutter);
        writer.write(material.diffuseTransmission);
        writer.write(material.diffuseTransmissionColor);
        writeTexture(writer, material.baseColorTexture);
        writeTexture(writer, material.metallicRoughnessTexture);
        writeTexture(writer, material.normalTexture);
        writeTexture(writer, material.occlusionTexture);
        writeTexture(writer, material.emissiveTexture);
    }
    writer.write(static_cast<u32>(model.nodes.size()));
    for (const ModelNode& node : model.nodes)
    {
        writer.writeString(node.name);
        writer.write(node.localMatrix);
        writer.write(node.parent);
        writer.write(node.mesh);
    }
    writer.write(static_cast<u32>(model.skins.size()));
    for (const Skeleton& skeleton : model.skins)
    {
        writeSkeleton(writer, skeleton);
    }
    writer.write(static_cast<u32>(model.animations.size()));
    for (const AnimationClip& clip : model.animations)
    {
        writeClip(writer, clip);
    }
    return bytes;
}

std::optional<ModelData> deserializeModel(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    ModelData model;
    u32 count = 0;
    // Counts are checked against the remaining bytes before allocating.
    const auto readCount = [&](usize minimumElementSize) {
        return reader.read(count) && static_cast<usize>(count) * minimumElementSize <= reader.remaining();
    };

    if (!readCount(8 * 3))
    {
        return std::nullopt;
    }
    model.meshes.resize(count);
    for (MeshData& mesh : model.meshes)
    {
        if (!reader.readVector(mesh.vertices) || !reader.readVector(mesh.indices) ||
            !reader.readVector(mesh.submeshes) || !reader.read(mesh.bounds) ||
            !reader.readVector(mesh.skin) || !reader.readVector(mesh.jointBounds) ||
            !reader.read(mesh.skeleton))
        {
            return std::nullopt;
        }
        for (const u32 index : mesh.indices)
        {
            if (index >= mesh.vertices.size())
            {
                return std::nullopt;
            }
        }
        for (const Submesh& submesh : mesh.submeshes)
        {
            if (static_cast<u64>(submesh.indexOffset) + submesh.indexCount > mesh.indices.size() ||
                submesh.lodCount > kMaxSubmeshLods)
            {
                return std::nullopt;
            }
            for (u32 level = 0; level < submesh.lodCount; ++level)
            {
                const SubmeshLod& lod = submesh.lods[level];
                if (lod.indexCount == 0 || lod.indexCount % 3 != 0 ||
                    static_cast<u64>(lod.indexOffset) + lod.indexCount > mesh.indices.size() ||
                    !(lod.error >= 0.0f))
                {
                    return std::nullopt;
                }
            }
        }
    }
    if (!readCount(1 + 8 + 8))
    {
        return std::nullopt;
    }
    model.images.resize(count);
    for (Image& image : model.images)
    {
        if (!readImage(reader, image))
        {
            return std::nullopt;
        }
    }
    if (!readCount(4))
    {
        return std::nullopt;
    }
    model.materials.resize(count);
    for (MaterialData& material : model.materials)
    {
        u8 alphaMode = 0;
        u8 doubleSided = 0;
        reader.readString(material.name);
        reader.read(material.baseColorFactor);
        reader.read(material.metallicFactor);
        reader.read(material.roughnessFactor);
        reader.read(material.emissiveFactor);
        reader.read(material.normalScale);
        reader.read(material.occlusionStrength);
        reader.read(material.alphaCutoff);
        reader.read(alphaMode);
        reader.read(doubleSided);
        reader.read(material.windSway);
        reader.read(material.windFlutter);
        reader.read(material.diffuseTransmission);
        reader.read(material.diffuseTransmissionColor);
        const usize images = model.images.size();
        readTexture(reader, material.baseColorTexture, images);
        readTexture(reader, material.metallicRoughnessTexture, images);
        readTexture(reader, material.normalTexture, images);
        readTexture(reader, material.occlusionTexture, images);
        readTexture(reader, material.emissiveTexture, images);
        if (!reader.ok() || alphaMode > static_cast<u8>(AlphaMode::Blend))
        {
            return std::nullopt;
        }
        material.alphaMode = static_cast<AlphaMode>(alphaMode);
        material.doubleSided = doubleSided != 0;
    }
    for (const MeshData& mesh : model.meshes)
    {
        for (const Submesh& submesh : mesh.submeshes)
        {
            if (submesh.material < -1 ||
                (submesh.material >= 0 && static_cast<usize>(submesh.material) >= count))
            {
                return std::nullopt;
            }
        }
    }
    if (!readCount(4 + 64 + 8))
    {
        return std::nullopt;
    }
    model.nodes.resize(count);
    for (usize i = 0; i < model.nodes.size(); ++i)
    {
        ModelNode& node = model.nodes[i];
        reader.readString(node.name);
        reader.read(node.localMatrix);
        reader.read(node.parent);
        reader.read(node.mesh);
        // Parents precede children (ModelData::worldMatrices() relies on it).
        if (!reader.ok() || node.parent < -1 || node.parent >= static_cast<i32>(i) || node.mesh < -1 ||
            (node.mesh >= 0 && static_cast<usize>(node.mesh) >= model.meshes.size()))
        {
            return std::nullopt;
        }
    }
    if (!readCount(4 + 4 + 64))
    {
        return std::nullopt;
    }
    model.skins.resize(count);
    for (Skeleton& skeleton : model.skins)
    {
        if (!readSkeleton(reader, skeleton))
        {
            return std::nullopt;
        }
    }
    for (const MeshData& mesh : model.meshes)
    {
        if (!isValidSkin(mesh, model.skins))
        {
            return std::nullopt;
        }
    }
    if (!readCount(4 + 4 + 4))
    {
        return std::nullopt;
    }
    model.animations.resize(count);
    for (AnimationClip& clip : model.animations)
    {
        if (!readClip(reader, clip))
        {
            return std::nullopt;
        }
    }
    if (reader.remaining() != 0)
    {
        return std::nullopt;
    }
    return model;
}

std::vector<std::byte> serializeEnvironment(const BakedEnvironment& environment)
{
    std::vector<std::byte> bytes;
    bytes.reserve((environment.background.size() + environment.specular.size()) * sizeof(u64) +
                  environment.backgroundBlocks.size() + environment.specularBlocks.size() + 256);
    BinaryWriter writer(bytes);
    writer.write(environment.backgroundSize);
    writer.write(environment.backgroundMips);
    writer.write(environment.specularSize);
    writer.write(environment.specularMips);
    writer.write(static_cast<u8>(environment.compressed ? 1 : 0));
    if (environment.compressed)
    {
        writer.writeVector(std::span<const std::byte>(environment.backgroundBlocks));
        writer.writeVector(std::span<const std::byte>(environment.specularBlocks));
    }
    else
    {
        writer.writeVector(std::span<const u64>(environment.background));
        writer.writeVector(std::span<const u64>(environment.specular));
    }
    for (const Vec3& coefficient : environment.irradiance.coefficients)
    {
        writer.write(coefficient.x);
        writer.write(coefficient.y);
        writer.write(coefficient.z);
    }
    return bytes;
}

std::optional<BakedEnvironment> deserializeEnvironment(std::span<const std::byte> bytes)
{
    BinaryReader reader(bytes);
    BakedEnvironment environment;
    u8 compressed = 0;
    if (!reader.read(environment.backgroundSize) || !reader.read(environment.backgroundMips) ||
        !reader.read(environment.specularSize) || !reader.read(environment.specularMips) ||
        !reader.read(compressed) || compressed > 1)
    {
        return std::nullopt;
    }
    environment.compressed = compressed != 0;
    const bool cubes =
        environment.compressed
            ? reader.readVector(environment.backgroundBlocks) && reader.readVector(environment.specularBlocks)
            : reader.readVector(environment.background) && reader.readVector(environment.specular);
    if (!cubes)
    {
        return std::nullopt;
    }
    for (Vec3& coefficient : environment.irradiance.coefficients)
    {
        if (!reader.read(coefficient.x) || !reader.read(coefficient.y) || !reader.read(coefficient.z))
        {
            return std::nullopt;
        }
    }
    if (reader.remaining() != 0 || !environment.isValid())
    {
        return std::nullopt;
    }
    return environment;
}

} // namespace illusion
