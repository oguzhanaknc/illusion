// The material.* commands: material files (create, get, set) and their shaders (ADR 0063): compile a
// `.shader` file on the editor's background thread and validate a material or shader with locations for
// agents.

#include "CommandSupport.hpp"

#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/assets/ShaderLoader.hpp>
#include <illusion/core/Hash.hpp>

#include <format>
#include <memory>

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

bool isBuiltin(std::string_view path) noexcept
{
    return path.starts_with("builtin:/");
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

Expected<void, CommandError> checkShaderPath(CommandContext& context, std::string_view path)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return open;
    }
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || lowerExtension(path) != kShaderAssetExtension ||
        path.size() <= kAssetsRoot.size() + kShaderAssetExtension.size() ||
        context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path", "must be a .shader file in the assets: mount, e.g. \"assets:/shaders/glow.shader\""));
    }
    return {};
}

CommandError readFailure(FileError error, std::string_view path)
{
    return error == FileError::NotFound
               ? CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist")
               : CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                            std::string(fileErrorName(error)) + ")");
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
        return unexpected(readFailure(error, path));
    }
    Expected<MaterialAsset, std::string> material = parseMaterialAsset(text);
    if (!material)
    {
        return unexpected(CommandError::invalid("/path", "is not a material file: " + material.error()));
    }
    return std::move(material).value();
}

// --- shaders ----------------------------------------------------------------------------------------------

JsonText jsonText(JsonMutDocument& doc, JsonMutValue value)
{
    doc.setRoot(value);
    JsonText text;
    (void)doc.write(text.text, JsonWriteOptions{.pretty = false});
    return text;
}

MaterialParameterInfo describe(const ShaderParameter& parameter)
{
    MaterialParameterInfo info;
    info.name = parameter.name;
    info.type = std::string(shaderParameterTypeName(parameter.type));
    info.color = parameter.color;
    if (parameter.hasRange)
    {
        info.minimum = static_cast<f64>(parameter.minimum);
        info.maximum = static_cast<f64>(parameter.maximum);
    }
    JsonMutDocument doc;
    JsonMutValue value;
    switch (parameter.type)
    {
    case ShaderParameterType::Float:
        value = doc.makeF32(parameter.defaultValue[0]);
        break;
    case ShaderParameterType::Int:
        value = doc.makeI64(static_cast<i64>(parameter.defaultValue[0]));
        break;
    case ShaderParameterType::Bool:
        value = doc.makeBool(parameter.defaultValue[0] != 0.0f);
        break;
    case ShaderParameterType::Float2:
    case ShaderParameterType::Float3:
    case ShaderParameterType::Float4:
        value = doc.makeArray();
        for (u32 i = 0; i < shaderParameterComponents(parameter.type); ++i)
        {
            value.push(doc.makeF32(parameter.defaultValue[i]));
        }
        break;
    }
    info.defaultValue = jsonText(doc, value);
    return info;
}

MaterialParameterInfo describe(const ShaderTexture& texture)
{
    MaterialParameterInfo info;
    info.name = texture.name;
    info.type = "texture";
    info.usage = std::string(shaderTextureUsageName(texture.usage));
    JsonMutDocument doc;
    info.defaultValue = jsonText(doc, doc.makeString(texture.defaultTexture));
    return info;
}

// What the editor knows about a shader: its info, and the build when it is of the file as it is now.
struct ShaderState
{
    MaterialShaderInfo info;
    std::shared_ptr<const ShaderBuild> build; // current and built; null otherwise
};

void fill(MaterialShaderInfo& info, const ShaderBuild& build)
{
    info.status = build.succeeded() ? "ok" : "failed";
    info.error = build.error;
    for (const ShaderDiagnostic& diagnostic : build.diagnostics)
    {
        info.diagnostics.push_back(
            ShaderDiagnosticInfo{diagnostic.file, diagnostic.line, diagnostic.column,
                                 std::string(shaderDiagnosticSeverityName(diagnostic.severity)),
                                 diagnostic.code, diagnostic.message});
    }
    if (!build.shader)
    {
        return;
    }
    info.shadingModel = std::string(shadingModelName(build.shader->shadingModel));
    info.worldOffset = build.shader->worldOffset;
    for (const ShaderParameter& parameter : build.shader->parameters)
    {
        info.parameters.push_back(describe(parameter));
    }
    for (const ShaderTexture& texture : build.shader->textures)
    {
        info.parameters.push_back(describe(texture));
    }
}

// The state of the shader at @p path. A shader that was not built from the file as it is now is queued for a
// build on the editor's background thread, and reports "building".
ShaderState shaderState(CommandContext& context, const std::string& path)
{
    ShaderState state;
    state.info.path = path;
    if (isBuiltin(path))
    {
        state.info.status = "builtin";
        return state;
    }
    std::vector<std::byte> source;
    if (const FileError error = context.editor().files().readFile(path, source); error != FileError::None)
    {
        state.info.status = "missing";
        state.info.error =
            "'" + path + "' " +
            (error == FileError::NotFound ? "does not exist" : std::string(fileErrorName(error)));
        return state;
    }
    ShaderLoader& loader = context.editor().shaders();
    const ShaderLoader::State progress = loader.state(path);
    if (progress.build && progress.build->sourceHash == xxhash64(source))
    {
        fill(state.info, *progress.build);
        if (progress.build->succeeded())
        {
            state.build = progress.build;
        }
        return state;
    }
    if (!progress.building)
    {
        loader.request(path);
    }
    state.info.status = "building";
    return state;
}

// The first error of @p info as one line, or its error text.
std::string firstProblem(const MaterialShaderInfo& info)
{
    for (const ShaderDiagnosticInfo& diagnostic : info.diagnostics)
    {
        if (diagnostic.severity == "error")
        {
            std::string text = diagnostic.file;
            if (diagnostic.line != 0)
            {
                text += ":" + std::to_string(diagnostic.line);
                text += diagnostic.column != 0 ? ":" + std::to_string(diagnostic.column) : "";
            }
            return (text.empty() ? "" : text + ": ") + diagnostic.message;
        }
    }
    return info.error;
}

// Splits "/parameters/x: message" into its pointer and message.
std::pair<std::string, std::string> splitPointer(const std::string& text)
{
    const usize colon = text.find(": ");
    if (text.starts_with('/') && colon != std::string::npos)
    {
        return {text.substr(0, colon), text.substr(colon + 2)};
    }
    return {{}, text};
}

// Checks the parameters of @p material against its shader when the shader is built; otherwise says why they
// were not checked.
Expected<void, CommandError> checkParameters(CommandContext& context, const MaterialAsset& material,
                                             std::string_view pointerPrefix)
{
    Expected<JsonDocument, JsonError> parameters = JsonDocument::parse(material.parameters.text);
    const bool any = parameters && parameters->root().size() != 0;
    if (material.shader.empty())
    {
        if (any)
        {
            return unexpected(CommandError::invalid(std::string(pointerPrefix) + "/parameters",
                                                    "needs a shader: set \"shader\" to a .shader file too"));
        }
        return {};
    }
    const std::string path(material.shader.path());
    if (isBuiltin(path) || !any)
    {
        return {};
    }
    const ShaderState state = shaderState(context, path);
    if (!state.build)
    {
        context.warn("the parameters were not checked: the shader '" + path + "' is " + state.info.status +
                     (state.info.status == "failed" ? " (material.validate lists its errors)"
                                                    : " (material.validate checks them once it built)"));
        return {};
    }
    const Expected<MaterialParameters, std::string> resolved =
        resolveMaterialParameters(material, *state.build->shader);
    if (!resolved)
    {
        const auto [pointer, message] = splitPointer(resolved.error());
        return unexpected(CommandError::invalid(std::string(pointerPrefix) + pointer, message));
    }
    return {};
}

// --- editing ----------------------------------------------------------------------------------------------

// Merges the object @p patch into the JSON object text @p parameters: members replace members, null removes.
Expected<void, CommandError> mergeParameters(JsonText& parameters, JsonValue patch, std::string_view pointer)
{
    if (!patch.isObject())
    {
        return unexpected(
            CommandError::invalid(std::string(pointer), "must be an object of parameter values by name"));
    }
    const Expected<JsonDocument, JsonError> current = JsonDocument::parse(parameters.text);
    JsonMutDocument doc;
    JsonMutValue merged = doc.makeObject();
    if (current && current->root().isObject())
    {
        for (const JsonMember member : current->root().members())
        {
            if (patch.find(member.key).isAbsent())
            {
                merged.add(member.key, doc.copy(member.value));
            }
        }
    }
    for (const JsonMember member : patch.members())
    {
        if (!member.value.isNull())
        {
            merged.add(member.key, doc.copy(member.value));
        }
    }
    parameters = jsonText(doc, merged);
    return {};
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
        return unexpected(CommandError::invalid(
            "/value", "must be an object: {\"shader\": \"...\", \"material\": {...}, \"textures\": {...}, "
                      "\"parameters\": {...}}"));
    }
    // "parameters" merges by name; the rest of the patch merges like any reflected object.
    JsonMutDocument rest;
    JsonMutValue restRoot = rest.makeObject();
    JsonValue parameters;
    for (const JsonMember member : patch->root().members())
    {
        if (member.key == "parameters")
        {
            parameters = member.value;
        }
        else
        {
            restRoot.add(member.key, rest.copy(member.value));
        }
    }
    rest.setRoot(restRoot);
    std::string restText;
    (void)rest.write(restText, JsonWriteOptions{.pretty = false});
    const Expected<JsonDocument, JsonError> restDocument = JsonDocument::parse(restText);
    const StructReflection& type = materialReflection();
    const ValueReadResult read =
        readValue(type, &material, restDocument->root(), {.mode = JsonReadMode::Merge});
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
    if (!parameters.isAbsent())
    {
        return mergeParameters(material.parameters, parameters, "/value/parameters");
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
        if (Expected<void, CommandError> checked = checkParameters(context, material, "/value"); !checked)
        {
            return unexpected(std::move(checked).error());
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
    if (!material->shader.empty())
    {
        result.shader = shaderState(context, std::string(material->shader.path())).info;
    }
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
    if (Expected<void, CommandError> checked = checkParameters(context, *material, "/value"); !checked)
    {
        return unexpected(std::move(checked).error());
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

// --- shaders: compile and validate ------------------------------------------------------------------------

// Asks the loader for a build and waits for it over the frames, without blocking one.
class CompileTask final : public CommandTask<MaterialShaderInfo>
{
public:
    explicit CompileTask(std::string path)
        : m_path(std::move(path))
    {
    }

    Expected<std::optional<MaterialShaderInfo>, CommandError> update(CommandContext& context,
                                                                     CommandProgress& progress) override
    {
        ShaderLoader& loader = context.editor().shaders();
        if (!m_requested)
        {
            m_before = loader.state(m_path).completed;
            loader.request(m_path);
            m_requested = true;
        }
        const ShaderLoader::State state = loader.state(m_path);
        if (state.building || state.completed <= m_before)
        {
            progress.message = "compiling " + m_path;
            return std::optional<MaterialShaderInfo>{};
        }
        MaterialShaderInfo info;
        info.path = m_path;
        info.status = "failed";
        if (state.build)
        {
            fill(info, *state.build);
        }
        return std::optional<MaterialShaderInfo>(std::move(info));
    }

private:
    std::string m_path;
    bool m_requested = false;
    u64 m_before = 0;
};

Expected<std::unique_ptr<CommandTask<MaterialShaderInfo>>, CommandError>
materialCompile(CommandContext& context, const MaterialCompileParams& params)
{
    if (Expected<void, CommandError> valid = checkShaderPath(context, params.path); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (!context.editor().files().exists(params.path))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.path + "' does not exist"));
    }
    return std::unique_ptr<CommandTask<MaterialShaderInfo>>(std::make_unique<CompileTask>(params.path));
}

void invalidate(MaterialValidateResult& result, std::string error, std::string pointer)
{
    result.valid = false;
    result.error = std::move(error);
    result.errorPath = std::move(pointer);
}

Expected<MaterialValidateResult, CommandError> materialValidate(CommandContext& context,
                                                                const MaterialValidateParams& params)
{
    const bool isShader = lowerExtension(params.path) == kShaderAssetExtension;
    if (Expected<void, CommandError> valid =
            isShader ? checkShaderPath(context, params.path) : checkMaterialPath(context, params.path);
        !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (!context.editor().files().exists(params.path))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.path + "' does not exist"));
    }
    MaterialValidateResult result;
    result.path = params.path;
    if (isShader)
    {
        result.kind = "Shader";
        ShaderState state = shaderState(context, params.path);
        result.valid = state.info.status == "ok";
        if (state.info.status == "building")
        {
            result.error = "the shader is still building; run material.compile to wait for it";
        }
        else if (!result.valid)
        {
            result.error = firstProblem(state.info);
        }
        result.shader = std::move(state.info);
        return result;
    }
    result.kind = "Material";
    std::string text;
    if (const FileError error = context.editor().files().readText(params.path, text);
        error != FileError::None)
    {
        return unexpected(readFailure(error, params.path));
    }
    const Expected<MaterialAsset, std::string> material = parseMaterialAsset(text);
    if (!material)
    {
        const auto [pointer, message] = splitPointer(material.error());
        invalidate(result, message, pointer);
        return result;
    }
    if (material->shader.empty())
    {
        const Expected<JsonDocument, JsonError> parameters = JsonDocument::parse(material->parameters.text);
        if (parameters && parameters->root().size() != 0)
        {
            invalidate(result, "the material names no shader, so it has no parameters", "/parameters");
            return result;
        }
        result.valid = true;
        return result;
    }
    const std::string shader(material->shader.path());
    ShaderState state = shaderState(context, shader);
    result.shader = state.info;
    if (state.info.status == "builtin")
    {
        result.valid = true;
    }
    else if (state.info.status == "missing")
    {
        invalidate(result, "the shader '" + shader + "' does not exist", "/shader");
    }
    else if (state.info.status == "building")
    {
        invalidate(result, "the shader '" + shader + "' is still building; run material.compile on it",
                   "/shader");
    }
    else if (!state.build)
    {
        invalidate(result, "the shader '" + shader + "' does not compile: " + firstProblem(state.info),
                   "/shader");
    }
    else if (const Expected<MaterialParameters, std::string> resolved =
                 resolveMaterialParameters(*material, *state.build->shader);
             !resolved)
    {
        const auto [pointer, message] = splitPointer(resolved.error());
        invalidate(result, message, pointer);
    }
    else
    {
        result.valid = true;
    }
    return result;
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
            "\"material\", texture paths under \"textures\", a custom shader as \"shader\" with the values "
            "of "
            "its parameters by name under \"parameters\"). Example: {\"path\": "
            "\"assets:/materials/red.material\", \"value\": {\"material\": {\"baseColorFactor\": [0.8, 0.1, "
            "0.1, 1]}}}.",
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
        .agentSummary =
            "Returns every field of a .material file, and for a material with a custom shader the shader's "
            "status and the parameters and textures it declares (name, type, default, range, usage). "
            "Example: {\"path\": \"assets:/materials/red.material\"}.",
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
            "Merges fields into a .material file (colours as linear arrays or \"#rrggbb\"). \"shader\" names "
            "a .shader file; \"parameters\" merges by name (null removes one) and is checked against the "
            "shader once it has built: a name the shader does not declare is an error. Example: {\"path\": "
            "\"assets:/materials/red.material\", \"value\": {\"material\": {\"roughnessFactor\": 0.2}, "
            "\"textures\": {\"baseColor\": \"assets:/textures/red.png\"}, \"parameters\": {\"pulse\": "
            "2.5}}}.",
        .category = "Material",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.addAsync<&materialCompile>({
        .name = "material.compile",
        .title = strings::kCommandMaterialCompileTitle,
        .description = strings::kCommandMaterialCompileDescription,
        .agentSummary =
            "Asynchronous: compiles a .shader file with the pinned slangc on a background thread (the editor "
            "also does so by itself whenever the file changes) and answers when it is done, never blocking a "
            "frame. The answer has status \"ok\" or \"failed\", the diagnostics with file, line and column, "
            "and when it built the shading model and the parameters and textures the shader declares. A "
            "shader that does not compile is a result, not an error. Example: {\"path\": "
            "\"assets:/shaders/glow.shader\"}.",
        .category = "Material",
        .icon = "build",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&materialValidate>({
        .name = "material.validate",
        .title = strings::kCommandMaterialValidateTitle,
        .description = strings::kCommandMaterialValidateDescription,
        .agentSummary =
            "Checks a .material or .shader file without changing it. A shader is valid when its current "
            "source built; a material when it parses and its \"parameters\" fit the shader it names (unknown "
            "names, wrong types and sizes are errors with a JSON Pointer in errorPath). The shader member "
            "has the build status (a shader that changed is queued for a build: use material.compile to "
            "wait for it) and the compiler's diagnostics with file, line and column. Example: {\"path\": "
            "\"assets:/materials/red.material\"}.",
        .category = "Material",
        .icon = "fact_check",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
}

} // namespace illusion::editor::detail
