#include "CommandSupport.hpp"

#include <illusion/assets/MaterialAsset.hpp>

namespace illusion::editor::detail
{
namespace
{

const StructReflection& materialReflection()
{
    const StructReflection* type =
        ComponentRegistry::instance().structs().reflectionOf(typeHash<MaterialAsset>());
    ILL_ASSERT(type != nullptr, "EditorContext reflects the material types");
    return *type;
}

Expected<void, CommandError> checkMaterialPath(CommandContext& context, std::string_view path)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return open;
    }
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || lowerExtension(path) != kMaterialAssetExtension ||
        path.size() <= kAssetsRoot.size() + kMaterialAssetExtension.size() ||
        context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path",
            "must be a .material file in the assets: mount, e.g. \"assets:/materials/red.material\""));
    }
    return {};
}

Expected<MaterialAsset, CommandError> readMaterial(CommandContext& context, std::string_view path)
{
    if (Expected<void, CommandError> valid = checkMaterialPath(context, path); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    std::string text;
    if (const FileError error = context.editor().files().readText(path, text); error != FileError::None)
    {
        return unexpected(
            error == FileError::NotFound
                ? CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist")
                : CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                             std::string(fileErrorName(error)) + ")"));
    }
    Expected<MaterialAsset, std::string> material = parseMaterialAsset(text);
    if (!material)
    {
        return unexpected(CommandError::invalid("/path", "is not a material file: " + material.error()));
    }
    return std::move(material).value();
}

// Merges the JSON patch @p value (params member "/value") into @p material.
Expected<void, CommandError> mergeMaterial(MaterialAsset& material, const JsonText& value)
{
    Expected<JsonDocument, CommandError> patch = parseParam(value, "/value");
    if (!patch)
    {
        return unexpected(std::move(patch).error());
    }
    if (!patch->root().isObject())
    {
        return unexpected(
            CommandError::invalid("/value", "must be an object: {\"material\": {...}, \"textures\": {...}}"));
    }
    const StructReflection& type = materialReflection();
    const ValueReadResult read = readValue(type, &material, patch->root(), {.mode = JsonReadMode::Merge});
    if (!read.ok())
    {
        CommandError error =
            CommandError::invalid("/value" + read.errors.front().pointer, read.errors.front().message);
        for (usize i = 1; i < read.errors.size(); ++i)
        {
            error.errors.push_back(ValueError{"/value" + read.errors[i].pointer, read.errors[i].message});
        }
        return unexpected(std::move(error));
    }
    if (!read.unknownKeys.empty())
    {
        return unexpected(CommandError::invalid("/value" + read.unknownKeys.front(), "is not a field"));
    }
    return {};
}

Expected<std::string, CommandError> materialText(const MaterialAsset& material)
{
    Expected<std::string, std::string> text = writeMaterialAsset(material);
    if (!text)
    {
        return unexpected(CommandError::invalid("/value", "cannot be saved: " + text.error()));
    }
    return std::move(text).value();
}

Expected<FileWriteResult, CommandError> materialCreate(CommandContext& context,
                                                       const MaterialCreateParams& params)
{
    if (Expected<void, CommandError> valid = checkMaterialPath(context, params.path); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (!params.overwrite.value_or(false) && context.editor().files().exists(params.path))
    {
        return unexpected(
            CommandError::make(ErrorKind::AlreadyExists, "'" + params.path + "' exists already"));
    }
    MaterialAsset material;
    if (params.value)
    {
        if (Expected<void, CommandError> merged = mergeMaterial(material, *params.value); !merged)
        {
            return unexpected(std::move(merged).error());
        }
    }
    Expected<std::string, CommandError> text = materialText(material);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    Expected<bool, CommandError> created = context.edit().writeFile(params.path, *text);
    if (!created)
    {
        return unexpected(std::move(created).error());
    }
    return FileWriteResult{params.path, *created};
}

Expected<MaterialGetResult, CommandError> materialGet(CommandContext& context, const FilePathParams& params)
{
    Expected<MaterialAsset, CommandError> material = readMaterial(context, params.path);
    if (!material)
    {
        return unexpected(std::move(material).error());
    }
    Expected<std::string, CommandError> value = writeReflected(materialReflection(), &*material);
    if (!value)
    {
        return unexpected(std::move(value).error());
    }
    MaterialGetResult result;
    result.path = params.path;
    result.value.text = std::move(value).value();
    return result;
}

Expected<MaterialSetResult, CommandError> materialSet(CommandContext& context,
                                                      const MaterialSetParams& params)
{
    Expected<MaterialAsset, CommandError> material = readMaterial(context, params.path);
    if (!material)
    {
        return unexpected(std::move(material).error());
    }
    Expected<std::string, CommandError> before = materialText(*material);
    if (!before)
    {
        return unexpected(std::move(before).error());
    }
    if (Expected<void, CommandError> merged = mergeMaterial(*material, params.value); !merged)
    {
        return unexpected(std::move(merged).error());
    }
    Expected<std::string, CommandError> after = materialText(*material);
    if (!after)
    {
        return unexpected(std::move(after).error());
    }
    if (*after == *before)
    {
        return MaterialSetResult{params.path, false};
    }
    if (Expected<bool, CommandError> written = context.edit().writeFile(params.path, *after); !written)
    {
        return unexpected(std::move(written).error());
    }
    return MaterialSetResult{params.path, true};
}

} // namespace

void registerMaterialCommands(CommandRegistry& registry)
{
    registry.add<&materialCreate>({
        .name = "material.create",
        .title = strings::kCommandMaterialCreateTitle,
        .description = strings::kCommandMaterialCreateDescription,
        .agentSummary =
            "Creates a .material file with default values and the given fields (MaterialData under "
            "\"material\", texture paths under \"textures\"). Example: {\"path\": "
            "\"assets:/materials/red.material\", \"value\": {\"material\": {\"baseColorFactor\": [0.8, 0.1, "
            "0.1, "
            "1]}}}.",
        .category = "Material",
        .icon = "palette",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
        .palette = true,
    });
    registry.add<&materialGet>({
        .name = "material.get",
        .title = strings::kCommandMaterialGetTitle,
        .description = strings::kCommandMaterialGetDescription,
        .agentSummary = "Returns every field of a .material file. Example: {\"path\": "
                        "\"assets:/materials/red.material\"}.",
        .category = "Material",
        .icon = "data_object",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&materialSet>({
        .name = "material.set",
        .title = strings::kCommandMaterialSetTitle,
        .description = strings::kCommandMaterialSetDescription,
        .agentSummary =
            "Merges fields into a .material file (colours as linear arrays or \"#rrggbb\"). "
            "Example: {\"path\": \"assets:/materials/red.material\", \"value\": {\"material\": "
            "{\"roughnessFactor\": 0.2}, \"textures\": {\"baseColor\": \"assets:/textures/red.png\"}}}.",
        .category = "Material",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
}

} // namespace illusion::editor::detail
