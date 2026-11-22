#include "AssetImport.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/ShaderCompiler.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace illusion
{

AssetDatabase::AssetDatabase(VirtualFileSystem& files, std::string cacheMount, bool verifySources)
    : m_files(&files)
    , m_cacheMount(std::move(cacheMount))
    , m_verifySources(verifySources)
{
}

std::string AssetDatabase::cachePath(std::string_view virtualPath, std::string_view importKey) const
{
    return detail::assetCachePath(m_cacheMount, virtualPath, importKey);
}

std::optional<AssetFile> AssetDatabase::readCache(const std::string& path, AssetType type, u32 version) const
{
    if (m_cacheMount.empty())
    {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    if (m_files->readFile(path, bytes) != FileError::None)
    {
        return std::nullopt;
    }
    std::optional<AssetFile> file = detail::parseCache(bytes, type, version);
    if (!file || !m_verifySources)
    {
        return file;
    }
    // Valid only while every source file still has the content it was imported from.
    std::vector<std::vector<std::byte>> contents(file->dependencies.size());
    for (usize i = 0; i < contents.size(); ++i)
    {
        if (!detail::readDependency(*m_files, file->dependencies[i].path, contents[i]))
        {
            return std::nullopt;
        }
    }
    if (!detail::dependenciesUnchanged(*file, contents))
    {
        return std::nullopt;
    }
    return file;
}

void AssetDatabase::writeCache(const std::string& path, const AssetFile& file)
{
    if (m_cacheMount.empty() || file.dependencies.empty())
    {
        return;
    }
    if (const FileError result = m_files->writeFile(path, writeAssetFile(file)); result != FileError::None)
    {
        ILL_LOG_WARN("Assets", "cannot write import cache {}: {}", path, fileErrorName(result));
        return;
    }
    ++m_statistics.cacheWrites;
}

std::optional<Image> AssetDatabase::loadImage(std::string_view virtualPath,
                                              const ImageImportSettings& settings, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, detail::imageImportKey(settings));
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Image, kImageFormatVersion))
    {
        if (std::optional<Image> image = deserializeImage(file->payload))
        {
            ++m_statistics.cacheHits;
            return image;
        }
    }

    ++m_statistics.imports;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: invalid path", virtualPath);
        }
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    if (const FileError result = m_files->readFile(*path, bytes); result != FileError::None)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", *path, fileErrorName(result));
        }
        return std::nullopt;
    }
    std::vector<std::byte> sidecar;
    if (!detail::readImportSettingsFile(*m_files, *path, sidecar, error))
    {
        return std::nullopt;
    }
    AssetFile cooked;
    std::optional<Image> image = detail::importImage(bytes, sidecar, *path, settings, cooked, error);
    if (image)
    {
        writeCache(cached, cooked);
    }
    return image;
}

std::optional<ModelData> AssetDatabase::loadModel(std::string_view virtualPath, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, detail::kModelImporter);
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Model, kModelFormatVersion))
    {
        if (std::optional<ModelData> model = deserializeModel(file->payload))
        {
            ++m_statistics.cacheHits;
            return model;
        }
    }

    ++m_statistics.imports;
    AssetFile cooked;
    std::optional<ModelData> model = detail::importModel(*m_files, virtualPath, cooked, error);
    if (model)
    {
        writeCache(cached, cooked);
    }
    return model;
}

std::optional<Font> AssetDatabase::loadFont(std::string_view virtualPath, std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, detail::kFontImporter);
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Font, kFontFormatVersion))
    {
        if (std::optional<Font> font = deserializeFont(file->payload))
        {
            ++m_statistics.cacheHits;
            return font;
        }
    }

    ++m_statistics.imports;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: invalid path", virtualPath);
        }
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    if (const FileError result = m_files->readFile(*path, bytes); result != FileError::None)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", *path, fileErrorName(result));
        }
        return std::nullopt;
    }
    std::vector<std::byte> sidecar;
    if (!detail::readImportSettingsFile(*m_files, *path, sidecar, error))
    {
        return std::nullopt;
    }
    AssetFile cooked;
    std::optional<Font> font = detail::importFont(bytes, sidecar, *path, cooked, error);
    if (font)
    {
        writeCache(cached, cooked);
    }
    return font;
}

std::optional<ShaderAsset> AssetDatabase::loadShader(std::string_view virtualPath,
                                                     const ShaderCompiler* compiler,
                                                     std::vector<ShaderDiagnostic>* diagnostics,
                                                     std::string* error)
{
    ILL_PROFILE_SCOPE();
    const std::string cached = cachePath(virtualPath, detail::kShaderImporter);
    if (std::optional<AssetFile> file = readCache(cached, AssetType::Shader, kShaderFormatVersion))
    {
        std::optional<ShaderAsset> shader = deserializeShader(file->payload);
        // A different engine shader library or compiler makes the cached SPIR-V stale.
        if (shader && (compiler == nullptr || (shader->libraryHash == compiler->libraryHash() &&
                                               shader->compilerVersion == compiler->compilerVersion())))
        {
            ++m_statistics.cacheHits;
            return shader;
        }
    }

    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: invalid path", virtualPath);
        }
        return std::nullopt;
    }
    if (compiler == nullptr)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: not cooked, and there is no shader compiler", *path);
        }
        return std::nullopt;
    }
    ++m_statistics.imports;
    std::vector<std::byte> bytes;
    if (const FileError result = m_files->readFile(*path, bytes); result != FileError::None)
    {
        if (error != nullptr)
        {
            *error = std::format("{}: {}", *path, fileErrorName(result));
        }
        return std::nullopt;
    }
    ShaderCompileResult compiled =
        compiler->compile(*path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (diagnostics != nullptr)
    {
        diagnostics->insert(diagnostics->end(), compiled.diagnostics.begin(), compiled.diagnostics.end());
    }
    if (!compiled.shader)
    {
        if (error != nullptr)
        {
            const auto errors =
                std::count_if(compiled.diagnostics.begin(), compiled.diagnostics.end(),
                              [](const ShaderDiagnostic& diagnostic) {
                                  return diagnostic.severity == ShaderDiagnosticSeverity::Error;
                              });
            *error = std::format("{}: the shader does not compile ({} error{})", *path, errors,
                                 errors == 1 ? "" : "s");
        }
        return std::nullopt;
    }
    AssetFile cooked{.type = AssetType::Shader,
                     .version = kShaderFormatVersion,
                     .dependencies = {AssetDependency{.path = *path, .contentHash = xxhash64(bytes)}},
                     .payload = serializeShader(*compiled.shader)};
    writeCache(cached, cooked);
    return std::move(compiled.shader);
}

} // namespace illusion
