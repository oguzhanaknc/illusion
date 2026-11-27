#include <illusion/assets/ShaderCompiler.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

namespace illusion
{
namespace
{

constexpr std::string_view kSurfaceFile = "illusion/Surface.slang";
constexpr std::string_view kInterfaceFile = "illusion/MaterialInterface.slang";
constexpr std::string_view kPassFile = "illusion/MaterialPass.slang";
constexpr std::string_view kDecalFile = "illusion/Decal.slang";
constexpr std::string_view kDecalPassFile = "illusion/DecalPass.slang";
constexpr std::string_view kInterfaceEntry = "illusionInterface";
constexpr std::string_view kParametersName = "illusionParameters";
constexpr u32 kMaxDefaultProcesses = 8;
constexpr std::chrono::milliseconds kVersionTimeout{30000};

// The flags of illusion_add_shaders() (cmake/IllusionShaders.cmake) without -warnings-as-errors: game shaders
// get the same bindings and matrix layout as the engine's, and their warnings stay warnings.
constexpr std::array<std::string_view, 19> kSlangFlags = {"-target",
                                                          "spirv",
                                                          "-fvk-t-shift",
                                                          "0",
                                                          "all",
                                                          "-fvk-s-shift",
                                                          "128",
                                                          "all",
                                                          "-fvk-b-shift",
                                                          "256",
                                                          "all",
                                                          "-fvk-u-shift",
                                                          "384",
                                                          "all",
                                                          "-matrix-layout-column-major",
                                                          "-enable-machine-readable-diagnostics",
                                                          "-diagnostic-color",
                                                          "never",
                                                          "-I"};

std::string pathUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool readFileBytes(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    out = std::move(text).str();
    return true;
}

bool writeFileBytes(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::optional<u32> parseU32(std::string_view text) noexcept
{
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string_view> splitTabs(std::string_view line)
{
    std::vector<std::string_view> fields;
    usize start = 0;
    while (true)
    {
        const usize tab = line.find('\t', start);
        fields.push_back(
            line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos)
        {
            return fields;
        }
        start = tab + 1;
    }
}

std::optional<ShaderDiagnosticSeverity> severityOf(std::string_view word) noexcept
{
    if (word == "error" || word == "fatal" || word == "internal")
    {
        return ShaderDiagnosticSeverity::Error;
    }
    if (word == "warning")
    {
        return ShaderDiagnosticSeverity::Warning;
    }
    if (word == "note" || word == "info")
    {
        return ShaderDiagnosticSeverity::Note;
    }
    return std::nullopt;
}

ShaderDiagnostic error(std::string file, u32 line, u32 column, std::string message)
{
    return ShaderDiagnostic{.file = std::move(file),
                            .line = line,
                            .column = column,
                            .severity = ShaderDiagnosticSeverity::Error,
                            .code = {},
                            .message = std::move(message)};
}

bool hasError(const std::vector<ShaderDiagnostic>& diagnostics) noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const ShaderDiagnostic& diagnostic) {
        return diagnostic.severity == ShaderDiagnosticSeverity::Error;
    });
}

// Appends @p from to @p to without the diagnostics @p to already has (each variant repeats the warnings of
// the shader's code).
void mergeDiagnostics(std::vector<ShaderDiagnostic>& to, std::vector<ShaderDiagnostic> from)
{
    for (ShaderDiagnostic& diagnostic : from)
    {
        if (std::find(to.begin(), to.end(), diagnostic) == to.end())
        {
            to.push_back(std::move(diagnostic));
        }
    }
}

// The 1-based line and column of byte @p offset of @p text.
std::pair<u32, u32> lineColumn(std::string_view text, usize offset) noexcept
{
    u32 line = 1;
    usize lineStart = 0;
    for (usize i = 0; i < offset && i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            ++line;
            lineStart = i + 1;
        }
    }
    return {line, static_cast<u32>(offset - lineStart + 1)};
}

bool identifierCharacter(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Offset of the identifier @p word in @p text at or after @p from (whole words only), or npos.
usize findWord(std::string_view text, std::string_view word, usize from) noexcept
{
    for (usize at = text.find(word, from); at != std::string_view::npos; at = text.find(word, at + 1))
    {
        const bool startsWord = at == 0 || !identifierCharacter(text[at - 1]);
        const bool endsWord = at + word.size() >= text.size() || !identifierCharacter(text[at + word.size()]);
        if (startsWord && endsWord)
        {
            return at;
        }
    }
    return std::string_view::npos;
}

// True if the identifier at @p offset of @p text opens an attribute: `[` before it, spaces aside.
bool attributeAt(std::string_view text, usize offset) noexcept
{
    while (offset > 0 && (text[offset - 1] == ' ' || text[offset - 1] == '\t'))
    {
        --offset;
    }
    return offset > 0 && text[offset - 1] == '[';
}

// True if @p source writes a `[Decal(...)]` attribute, so that it is compiled with the decal helpers
// (illusion/Decal.slang). The reflection decides whether it is a decal shader.
bool declaresDecal(std::string_view source) noexcept
{
    for (usize at = findWord(source, "Decal", 0); at != std::string_view::npos;
         at = findWord(source, "Decal", at + 1))
    {
        if (attributeAt(source, at))
        {
            return true;
        }
    }
    return false;
}

// Where the source declares Parameters and its fields, for the diagnostics of checks on the reflection.
class SourceLocator
{
public:
    SourceLocator(std::string_view path, std::string_view source)
        : m_path(path)
        , m_source(source)
    {
        for (usize at = findWord(source, "struct", 0); at != std::string_view::npos;
             at = findWord(source, "struct", at + 1))
        {
            const usize name = source.find_first_not_of(" \t\r\n", at + 6);
            if (name != std::string_view::npos && findWord(source, "Parameters", name) == name)
            {
                m_struct = name;
                break;
            }
        }
    }

    // An error at the declaration of Parameters.
    [[nodiscard]] ShaderDiagnostic structError(std::string message) const
    {
        return at(m_struct, std::move(message));
    }

    // An error at the attribute @p name of Parameters, `[name(...)]` before its declaration (at Parameters
    // when it cannot be found).
    [[nodiscard]] ShaderDiagnostic attributeError(std::string_view name, std::string message) const
    {
        usize found = std::string_view::npos;
        for (usize offset = findWord(m_source, name, 0);
             offset != std::string_view::npos && offset < m_struct;
             offset = findWord(m_source, name, offset + 1))
        {
            if (attributeAt(m_source, offset))
            {
                found = offset;
            }
        }
        return at(found == std::string_view::npos ? m_struct : found, std::move(message));
    }

    // An error at the first use of the identifier @p word (at Parameters when there is none).
    [[nodiscard]] ShaderDiagnostic wordError(std::string_view word, std::string message) const
    {
        const usize found = findWord(m_source, word, 0);
        return at(found == std::string_view::npos ? m_struct : found, std::move(message));
    }

    // An error at the field @p field of Parameters (at Parameters when it cannot be found).
    [[nodiscard]] ShaderDiagnostic fieldError(std::string_view field, std::string message) const
    {
        const usize found = m_struct == std::string_view::npos ? std::string_view::npos
                                                               : findWord(m_source, field, m_struct + 10);
        return at(found == std::string_view::npos ? m_struct : found, std::move(message));
    }

private:
    [[nodiscard]] ShaderDiagnostic at(usize offset, std::string message) const
    {
        if (offset == std::string_view::npos)
        {
            return error(std::string(m_path), 0, 0, std::move(message));
        }
        const auto [line, column] = lineColumn(m_source, offset);
        return error(std::string(m_path), line, column, std::move(message));
    }

    std::string_view m_path;
    std::string_view m_source;
    usize m_struct = std::string_view::npos;
};

// A user attribute of the reflection: its name and arguments.
struct Attribute
{
    std::string_view name;
    JsonValue arguments;
};

std::vector<Attribute> attributesOf(JsonValue node)
{
    std::vector<Attribute> attributes;
    for (const JsonValue attribute : node.find("userAttribs").elements())
    {
        attributes.push_back(Attribute{.name = attribute.find("name").asString().value_or(""),
                                       .arguments = attribute.find("arguments")});
    }
    return attributes;
}

std::optional<f32> numberArgument(JsonValue arguments, usize index) noexcept
{
    const std::optional<f64> value = arguments[index].asF64();
    if (!value || !std::isfinite(*value))
    {
        return std::nullopt;
    }
    return static_cast<f32>(*value);
}

// The parameter type of a reflected field type, or nullopt; @p unsignedScalar is set for `uint` fields
// (texture slots).
std::optional<ShaderParameterType> parameterType(JsonValue type, bool& unsignedScalar)
{
    unsignedScalar = false;
    const std::string_view kind = type.find("kind").asString().value_or("");
    if (kind == "scalar")
    {
        const std::string_view scalar = type.find("scalarType").asString().value_or("");
        if (scalar == "float32")
        {
            return ShaderParameterType::Float;
        }
        if (scalar == "int32")
        {
            return ShaderParameterType::Int;
        }
        if (scalar == "bool")
        {
            return ShaderParameterType::Bool;
        }
        unsignedScalar = scalar == "uint32";
        return std::nullopt;
    }
    if (kind == "vector" && type.find("elementType").find("scalarType").asString() == "float32")
    {
        switch (type.find("elementCount").asU64().value_or(0))
        {
        case 2:
            return ShaderParameterType::Float2;
        case 3:
            return ShaderParameterType::Float3;
        case 4:
            return ShaderParameterType::Float4;
        default:
            break;
        }
    }
    return std::nullopt;
}

std::string typeText(JsonValue type)
{
    const std::string_view kind = type.find("kind").asString().value_or("unknown");
    if (kind == "scalar")
    {
        return std::string(type.find("scalarType").asString().value_or("scalar"));
    }
    if (kind == "vector")
    {
        return std::format("{} vector of {}",
                           type.find("elementType").find("scalarType").asString().value_or("?"),
                           type.find("elementCount").asU64().value_or(0));
    }
    if (const std::optional<std::string_view> name = type.find("name").asString())
    {
        return std::format("{} {}", kind, *name);
    }
    return std::string(kind);
}

// Reads the texture attribute `[Texture(fallback, usage)]` of @p field into @p texture.
void readTextureAttribute(const SourceLocator& locator, std::string_view field, JsonValue arguments,
                          ShaderTexture& texture, std::vector<ShaderDiagnostic>& diagnostics)
{
    const std::string_view fallback = arguments[0].asString().value_or("");
    const std::string_view usage = arguments[1].asString().value_or("");
    if (fallback == "white")
    {
        texture.fallback = ShaderTextureFallback::White;
    }
    else if (fallback == "black")
    {
        texture.fallback = ShaderTextureFallback::Black;
    }
    else if (fallback == "normal")
    {
        texture.fallback = ShaderTextureFallback::FlatNormal;
    }
    else if (fallback.find(":/") != std::string_view::npos)
    {
        texture.defaultTexture = std::string(fallback);
    }
    else
    {
        diagnostics.push_back(locator.fieldError(
            field,
            std::format("texture '{}': the default '{}' is not \"white\", \"black\", \"normal\" or an asset "
                        "path",
                        field, fallback)));
    }
    constexpr std::array<std::pair<std::string_view, ShaderTextureUsage>, 4> kUsages{{
        {"color", ShaderTextureUsage::Color},
        {"linear", ShaderTextureUsage::Linear},
        {"normal", ShaderTextureUsage::Normal},
        {"mask", ShaderTextureUsage::Mask},
    }};
    const auto found =
        std::find_if(kUsages.begin(), kUsages.end(), [&](const auto& entry) { return entry.first == usage; });
    if (found == kUsages.end())
    {
        diagnostics.push_back(locator.fieldError(
            field,
            std::format("texture '{}': the usage '{}' is not \"color\", \"linear\", \"normal\" or \"mask\"",
                        field, usage)));
        return;
    }
    texture.usage = found->second;
}

// Reads one reflected field of Parameters into @p shader.
void readField(const SourceLocator& locator, JsonValue field, ShaderAsset& shader,
               std::vector<ShaderDiagnostic>& diagnostics)
{
    const std::string name(field.find("name").asString().value_or(""));
    const JsonValue type = field.find("type");
    const u32 offset = static_cast<u32>(field.find("binding").find("offset").asU64().value_or(0));
    const std::vector<Attribute> attributes = attributesOf(field);
    const bool isTexture = std::any_of(attributes.begin(), attributes.end(), [](const Attribute& attribute) {
        return attribute.name == "Texture";
    });
    bool unsignedScalar = false;
    const std::optional<ShaderParameterType> parameter = parameterType(type, unsignedScalar);
    if (isTexture)
    {
        if (!unsignedScalar)
        {
            diagnostics.push_back(locator.fieldError(
                name, std::format("parameter '{}': [Texture] needs the type Texture2DSlot, not {}", name,
                                  typeText(type))));
            return;
        }
        ShaderTexture texture{.name = name,
                              .offset = offset,
                              .slot = static_cast<u32>(shader.textures.size()),
                              .usage = ShaderTextureUsage::Color,
                              .fallback = ShaderTextureFallback::White,
                              .defaultTexture = {}};
        for (const Attribute& attribute : attributes)
        {
            if (attribute.name == "Texture")
            {
                readTextureAttribute(locator, name, attribute.arguments, texture, diagnostics);
            }
            else
            {
                diagnostics.push_back(locator.fieldError(
                    name, std::format("texture '{}': [{}] applies to parameters, not textures", name,
                                      attribute.name)));
            }
        }
        shader.textures.push_back(std::move(texture));
        return;
    }
    if (!parameter)
    {
        diagnostics.push_back(locator.fieldError(
            name,
            std::format("parameter '{}' has the type {}; parameters are float, float2, float3, float4, int, "
                        "bool or Texture2DSlot with [Texture]",
                        name, typeText(type))));
        return;
    }
    ShaderParameter result{.name = name, .type = *parameter, .offset = offset};
    const u32 components = shaderParameterComponents(*parameter);
    bool hasDefault = false;
    for (const Attribute& attribute : attributes)
    {
        const std::string_view attributeName = attribute.name;
        u32 count = 0;
        if (attributeName == "Default")
        {
            count = 1;
        }
        else if (attributeName == "Default2" || attributeName == "Default3" || attributeName == "Default4")
        {
            count = static_cast<u32>(attributeName.back() - '0');
        }
        else if (attributeName == "Color")
        {
            count = 4;
        }
        if (count > 0)
        {
            if (hasDefault)
            {
                diagnostics.push_back(
                    locator.fieldError(name, std::format("parameter '{}' has two defaults", name)));
                continue;
            }
            hasDefault = true;
            const bool colorType =
                *parameter == ShaderParameterType::Float3 || *parameter == ShaderParameterType::Float4;
            if (attributeName == "Color" ? !colorType : count != components)
            {
                diagnostics.push_back(locator.fieldError(
                    name, std::format("parameter '{}': [{}] does not fit the type {}{}", name, attributeName,
                                      shaderParameterTypeName(*parameter),
                                      attributeName == "Color" ? " (colours are float3 or float4)" : "")));
                continue;
            }
            for (u32 i = 0; i < std::min(count, components); ++i)
            {
                result.defaultValue[i] = numberArgument(attribute.arguments, i).value_or(0.0f);
            }
            result.color = attributeName == "Color";
            const f32 value = result.defaultValue[0];
            if (*parameter == ShaderParameterType::Int && value != std::floor(value))
            {
                diagnostics.push_back(locator.fieldError(
                    name, std::format("parameter '{}': the default of an int is a whole number", name)));
            }
            if (*parameter == ShaderParameterType::Bool && value != 0.0f && value != 1.0f)
            {
                diagnostics.push_back(locator.fieldError(
                    name, std::format("parameter '{}': the default of a bool is 0 or 1", name)));
            }
        }
        else if (attributeName == "Range")
        {
            if (*parameter != ShaderParameterType::Float && *parameter != ShaderParameterType::Int)
            {
                diagnostics.push_back(locator.fieldError(
                    name, std::format("parameter '{}': [Range] applies to float and int parameters", name)));
                continue;
            }
            result.hasRange = true;
            result.minimum = numberArgument(attribute.arguments, 0).value_or(0.0f);
            result.maximum = numberArgument(attribute.arguments, 1).value_or(0.0f);
            if (result.minimum > result.maximum)
            {
                diagnostics.push_back(locator.fieldError(
                    name, std::format("parameter '{}': [Range] has its minimum above its maximum", name)));
            }
        }
        else
        {
            diagnostics.push_back(locator.fieldError(
                name, std::format("parameter '{}': unknown attribute [{}]", name, attributeName)));
        }
    }
    shader.parameters.push_back(std::move(result));
}

// Reads the decal attribute `[Decal("BaseColor Normal ...")]` of Parameters into @p shader.
void readDecalAttribute(const SourceLocator& locator, JsonValue arguments, ShaderAsset& shader,
                        std::vector<ShaderDiagnostic>& diagnostics)
{
    const std::string_view names = arguments[0].asString().value_or("");
    DecalChannels channels = DecalChannels::None;
    usize at = 0;
    while (at < names.size())
    {
        if (names[at] == ' ' || names[at] == '\t')
        {
            ++at;
            continue;
        }
        usize end = at;
        while (end < names.size() && names[end] != ' ' && names[end] != '\t')
        {
            ++end;
        }
        const std::string_view name = names.substr(at, end - at);
        at = end;
        const std::optional<DecalChannels> channel = parseDecalChannels(name);
        if (!channel)
        {
            diagnostics.push_back(locator.attributeError(
                "Decal",
                std::format("[Decal]: unknown channel '{}'; the channels are BaseColor, Normal, ORM and "
                            "Emission",
                            name)));
            return;
        }
        if (hasDecalChannel(channels, *channel))
        {
            diagnostics.push_back(locator.attributeError(
                "Decal", std::format("[Decal]: the channel '{}' is listed twice", name)));
            return;
        }
        channels = channels | *channel;
    }
    if (channels == DecalChannels::None)
    {
        diagnostics.push_back(locator.attributeError(
            "Decal", "[Decal] lists no channel; write one or more of BaseColor, Normal, ORM and Emission, "
                     "separated by spaces"));
        return;
    }
    shader.decalChannels = channels;
}

// Fills the layout and shading model of @p shader from the interface compile's reflection.
void readReflection(std::string_view json, const SourceLocator& locator, ShaderAsset& shader,
                    std::vector<ShaderDiagnostic>& diagnostics)
{
    const Expected<JsonDocument, JsonError> document = JsonDocument::parse(json);
    if (!document)
    {
        diagnostics.push_back(
            error({}, 0, 0, "the compiler's reflection is not valid JSON: " + document.error().message));
        return;
    }
    JsonValue block;
    for (const JsonValue parameter : document->root().find("parameters").elements())
    {
        if (parameter.find("name").asString() == kParametersName)
        {
            block = parameter.find("type");
        }
    }
    const JsonValue structure = block.find("elementType");
    if (structure.find("kind").asString() != "struct")
    {
        diagnostics.push_back(locator.structError("the compiler's reflection has no struct Parameters"));
        return;
    }
    bool shadingModel = false;
    bool decal = false;
    for (const Attribute& attribute : attributesOf(structure))
    {
        if (attribute.name == "Decal")
        {
            if (decal)
            {
                diagnostics.push_back(
                    locator.attributeError("Decal", "Parameters has two [Decal] attributes"));
                continue;
            }
            decal = true;
            readDecalAttribute(locator, attribute.arguments, shader, diagnostics);
            continue;
        }
        if (attribute.name != "ShadingModel")
        {
            diagnostics.push_back(
                locator.structError(std::format("Parameters: unknown attribute [{}]", attribute.name)));
            continue;
        }
        shadingModel = true;
        const std::string_view name = attribute.arguments[0].asString().value_or("");
        if (const std::optional<ShadingModel> model = parseShadingModel(name))
        {
            shader.shadingModel = *model;
        }
        else
        {
            diagnostics.push_back(locator.structError(std::format(
                "unknown shading model '{}'; use Default, Unlit, Subsurface, TwoSidedFoliage or ClearCoat",
                name)));
        }
    }
    if (decal && shadingModel)
    {
        diagnostics.push_back(locator.attributeError(
            "Decal",
            "[Decal] and [ShadingModel] cannot be used together: a decal changes the material of the "
            "surfaces it lands on, which keep their own shading model"));
    }
    u32 size = 0;
    for (const JsonValue field : structure.find("fields").elements())
    {
        readField(locator, field, shader, diagnostics);
        const JsonValue binding = field.find("binding");
        size = std::max(size, static_cast<u32>(binding.find("offset").asU64().value_or(0) +
                                               binding.find("size").asU64().value_or(0)));
    }
    shader.parameterBytes = std::max(
        size,
        static_cast<u32>(block.find("elementVarLayout").find("binding").find("size").asU64().value_or(0)));
    if (shader.parameterBytes > kMaxShaderParameterBytes)
    {
        diagnostics.push_back(
            locator.structError(std::format("Parameters take {} bytes; at most {} are allowed",
                                            shader.parameterBytes, kMaxShaderParameterBytes)));
    }
    if (shader.textures.size() > kMaxShaderTextures)
    {
        diagnostics.push_back(
            locator.structError(std::format("Parameters declare {} textures; at most {} are allowed",
                                            shader.textures.size(), kMaxShaderTextures)));
    }
}

// One run of slangc: started, then finished with its output.
class SlangRun
{
public:
    SlangRun(const ShaderCompilerDesc& desc, std::vector<std::string> arguments)
    {
        ProcessOptions options;
        options.executable = desc.slangc;
        options.arguments = std::move(arguments);
        Expected<Process, std::string> process = Process::spawn(options);
        if (!process)
        {
            m_failure = std::format("cannot run {}: {}", pathUtf8(desc.slangc), process.error());
            return;
        }
        m_process = std::move(*process);
        m_deadline = std::chrono::steady_clock::now() + desc.timeout;
        m_timeout = desc.timeout;
    }

    // Waits for the run; true if it exited with 0. @p diagnostics receives what it reported.
    bool finish(std::vector<ShaderDiagnostic>& diagnostics)
    {
        if (!m_failure.empty())
        {
            diagnostics.push_back(error({}, 0, 0, m_failure));
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto left = m_deadline > now
                              ? std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - now)
                              : std::chrono::milliseconds(0);
        const std::optional<i32> exitCode = m_process.wait(left);
        if (!exitCode)
        {
            static_cast<void>(m_process.terminate());
            static_cast<void>(m_process.wait());
            diagnostics.push_back(
                error({}, 0, 0, std::format("slangc did not finish within {} ms", m_timeout.count())));
            return false;
        }
        std::string output;
        while (!m_process.drained(ProcessStream::Error) || !m_process.drained(ProcessStream::Output))
        {
            std::string line;
            if (!m_process.readLine(ProcessStream::Error, line, std::chrono::milliseconds(100)) &&
                !m_process.readLine(ProcessStream::Output, line, std::chrono::milliseconds(0)))
            {
                continue;
            }
            output += line;
            output += '\n';
        }
        std::vector<ShaderDiagnostic> reported = parseSlangDiagnostics(output);
        const bool failed = *exitCode != 0;
        if (failed && !hasError(reported))
        {
            reported.push_back(error({}, 0, 0, std::format("slangc failed with exit code {}", *exitCode)));
        }
        mergeDiagnostics(diagnostics, std::move(reported));
        return !failed;
    }

private:
    Process m_process;
    std::string m_failure;
    std::chrono::steady_clock::time_point m_deadline{};
    std::chrono::milliseconds m_timeout{0};
};

std::vector<std::string> slangArguments(const ShaderCompilerDesc& desc, const std::filesystem::path& input)
{
    std::vector<std::string> arguments{pathUtf8(input)};
    for (const std::string_view flag : kSlangFlags)
    {
        arguments.emplace_back(flag);
    }
    arguments.push_back(pathUtf8(desc.library));
    return arguments;
}

std::string escapeLineName(std::string_view text)
{
    std::string escaped;
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            escaped += '\\';
        }
        if (c != '\n' && c != '\r')
        {
            escaped += c;
        }
    }
    return escaped;
}

// The source compiled for @p virtualPath: the surface API (with the decal helpers for decal shaders), the
// shader under its own name, then @p tail.
std::string wrapSource(std::string_view virtualPath, std::string_view source, std::string_view tail)
{
    const std::string decal = declaresDecal(source) ? std::format("#include \"{}\"\n", kDecalFile) : "";
    return std::format("#include \"{}\"\n{}#line 1 \"{}\"\n{}\n#include \"{}\"\n", kSurfaceFile, decal,
                       escapeLineName(virtualPath), source, tail);
}

// A directory of temporary files, removed with its contents.
class WorkDirectory
{
public:
    explicit WorkDirectory(const ShaderCompilerDesc& desc)
    {
        static std::atomic<u64> counter{0};
        std::error_code code;
        const std::filesystem::path base =
            desc.workDirectory.empty() ? std::filesystem::temp_directory_path(code) : desc.workDirectory;
        m_path = base / std::format("illusion-shader-{}-{}", currentProcessId(), counter.fetch_add(1));
        std::filesystem::remove_all(m_path, code);
        m_valid = std::filesystem::create_directories(m_path, code);
    }

    ~WorkDirectory()
    {
        std::error_code code;
        std::filesystem::remove_all(m_path, code);
    }

    WorkDirectory(const WorkDirectory&) = delete;
    WorkDirectory& operator=(const WorkDirectory&) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
    bool m_valid = false;
};

struct VariantKey
{
    ShaderPass pass;
    AlphaMode alphaMode;
};

std::string_view passDefine(ShaderPass pass) noexcept
{
    constexpr std::array<std::string_view, kShaderPassCount> kDefines = {
        "-DILLUSION_PASS_FORWARD=1",        "-DILLUSION_PASS_FORWARD_BINDLESS=1",
        "-DILLUSION_PASS_DEPTH_PREPASS=1",  "-DILLUSION_PASS_SHADOW_CASTER=1",
        "-DILLUSION_PASS_MOTION_VECTORS=1", "-DILLUSION_PASS_ENTITY_IDS=1",
        "-DILLUSION_PASS_DECAL=1",          "-DILLUSION_PASS_DECAL_EMISSION=1"};
    return kDefines[static_cast<usize>(pass)];
}

// Bytes of Parameters in a structured buffer (std430, the bindless material table): the fields' offsets are
// those of the constant buffer, the size is rounded to the largest alignment of a field instead of 16.
u32 structuredParameterBytes(const ShaderAsset& shader) noexcept
{
    u32 end = 0;
    u32 alignment = 4;
    for (const ShaderParameter& parameter : shader.parameters)
    {
        const u32 components = shaderParameterComponents(parameter.type);
        end = std::max(end, parameter.offset + 4 * components);
        alignment = std::max(alignment, components == 1 ? 4u : (components == 2 ? 8u : 16u));
    }
    for (const ShaderTexture& texture : shader.textures)
    {
        end = std::max(end, texture.offset + 4);
    }
    return (end + alignment - 1) / alignment * alignment;
}

// True if the probe of worldOffset() failed only because the shader has none.
bool missingWorldOffset(const std::vector<ShaderDiagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const ShaderDiagnostic& diagnostic) {
        return diagnostic.severity == ShaderDiagnosticSeverity::Error && diagnostic.code == "E30015" &&
               diagnostic.message.find("'worldOffset'") != std::string::npos;
    });
}

} // namespace

std::string_view shaderDiagnosticSeverityName(ShaderDiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
    case ShaderDiagnosticSeverity::Error:
        return "error";
    case ShaderDiagnosticSeverity::Warning:
        return "warning";
    case ShaderDiagnosticSeverity::Note:
        return "note";
    }
    return "error";
}

std::string formatShaderDiagnostic(const ShaderDiagnostic& diagnostic)
{
    std::string text = diagnostic.file;
    if (diagnostic.line > 0)
    {
        text += std::format(":{}", diagnostic.line);
        if (diagnostic.column > 0)
        {
            text += std::format(":{}", diagnostic.column);
        }
    }
    text += text.empty() ? "" : ": ";
    text += shaderDiagnosticSeverityName(diagnostic.severity);
    text += ": ";
    text += diagnostic.message;
    if (!diagnostic.code.empty())
    {
        text += std::format(" [{}]", diagnostic.code);
    }
    return text;
}

std::vector<ShaderDiagnostic> parseSlangDiagnostics(std::string_view output)
{
    std::vector<ShaderDiagnostic> diagnostics;
    while (!output.empty())
    {
        const usize end = output.find('\n');
        std::string_view line = output.substr(0, end);
        output = end == std::string_view::npos ? std::string_view() : output.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        {
            line.remove_suffix(1);
        }
        if (line.empty())
        {
            continue;
        }
        const std::vector<std::string_view> fields = splitTabs(line);
        if (fields.size() >= 8)
        {
            std::string message(fields[7]);
            for (usize i = 8; i < fields.size(); ++i)
            {
                message += '\t';
                message += fields[i];
            }
            const std::optional<u32> number = parseU32(fields[3]);
            const std::optional<u32> column = parseU32(fields[4]);
            if (fields[1] == "span" && !diagnostics.empty())
            {
                // The span repeats the location with the specific text: "undefined identifier 'x'."
                ShaderDiagnostic& last = diagnostics.back();
                if (message.starts_with(last.message))
                {
                    last.message = std::move(message);
                }
                else if (last.message.find(message) == std::string::npos)
                {
                    last.message += ": " + message;
                }
                continue;
            }
            if (const std::optional<ShaderDiagnosticSeverity> severity = severityOf(fields[1]))
            {
                diagnostics.push_back(ShaderDiagnostic{.file = std::string(fields[2]),
                                                       .line = number.value_or(0),
                                                       .column = column.value_or(0),
                                                       .severity = *severity,
                                                       .code = std::string(fields[0]),
                                                       .message = std::move(message)});
                continue;
            }
        }
        diagnostics.push_back(error({}, 0, 0, std::string(line)));
    }
    return diagnostics;
}

Expected<ShaderCompiler, std::string> ShaderCompiler::create(ShaderCompilerDesc desc)
{
    ILL_PROFILE_SCOPE();
    std::error_code code;
    if (!std::filesystem::is_regular_file(desc.slangc, code))
    {
        return unexpected(std::format("the shader compiler {} does not exist", pathUtf8(desc.slangc)));
    }
    if (!std::filesystem::is_regular_file(desc.library / utf8Path(kSurfaceFile), code))
    {
        return unexpected(std::format("{} is not the engine's shader library (it has no {})",
                                      pathUtf8(desc.library), kSurfaceFile));
    }
    if (desc.maxProcesses == 0)
    {
        desc.maxProcesses = std::clamp(std::thread::hardware_concurrency(), 1u, kMaxDefaultProcesses);
    }
    ShaderCompiler compiler;

    // The library's Slang files sorted by path, so the hash is the same on every machine and in the SDK,
    // which installs only them.
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(desc.library, code), end; !code && it != end;
         it.increment(code))
    {
        if (it->is_regular_file(code) && it->path().extension() == ".slang")
        {
            files.push_back(it->path().lexically_relative(desc.library));
        }
    }
    if (code)
    {
        return unexpected(std::format("cannot list {}: {}", pathUtf8(desc.library), code.message()));
    }
    std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return pathUtf8(a) < pathUtf8(b);
    });
    u64 hash = fnv1a64("illusion.shaderLibrary");
    for (const std::filesystem::path& file : files)
    {
        std::string bytes;
        if (!readFileBytes(desc.library / file, bytes))
        {
            return unexpected(std::format("cannot read {}", pathUtf8(desc.library / file)));
        }
        hash = hashCombine(hash, fnv1a64(pathUtf8(file)));
        hash = hashCombine(hash, xxhash64(std::as_bytes(std::span(bytes.data(), bytes.size()))));
    }
    compiler.m_libraryHash = hash;

    ProcessOptions options;
    options.executable = desc.slangc;
    options.arguments = {"-v"};
    Expected<Process, std::string> process = Process::spawn(options);
    if (!process)
    {
        return unexpected(std::format("cannot run {}: {}", pathUtf8(desc.slangc), process.error()));
    }
    const std::optional<i32> exitCode = process->wait(kVersionTimeout);
    if (!exitCode)
    {
        static_cast<void>(process->terminate());
        return unexpected(
            std::format("{} -v did not finish within {} ms", pathUtf8(desc.slangc), kVersionTimeout.count()));
    }
    std::string version;
    std::string line;
    while (process->readLine(ProcessStream::Output, line, std::chrono::milliseconds(1000)) ||
           process->readLine(ProcessStream::Error, line, std::chrono::milliseconds(0)))
    {
        if (version.empty() && !line.empty())
        {
            version = line;
        }
    }
    while (!version.empty() && (version.back() == ' ' || version.back() == '\r'))
    {
        version.pop_back();
    }
    if (*exitCode != 0 || version.empty())
    {
        return unexpected(
            std::format("{} -v reported no version (exit code {})", pathUtf8(desc.slangc), *exitCode));
    }
    compiler.m_version = std::move(version);
    compiler.m_desc = std::move(desc);
    return compiler;
}

ShaderCompileResult ShaderCompiler::compile(std::string_view virtualPath, std::string_view source) const
{
    ILL_PROFILE_SCOPE();
    ShaderCompileResult result;
    const WorkDirectory work(m_desc);
    const std::filesystem::path interfaceFile = work.path() / "interface.slang";
    const std::filesystem::path passFile = work.path() / "pass.slang";
    if (!work.valid() || !writeFileBytes(interfaceFile, wrapSource(virtualPath, source, kInterfaceFile)))
    {
        result.diagnostics.push_back(
            error(std::string(virtualPath), 0, 0,
                  "cannot write the compiler's temporary files in " + pathUtf8(work.path())));
        return result;
    }

    // The interface: does the shader compile on its own, what are its parameters, does it have worldOffset()?
    const std::filesystem::path reflectionFile = work.path() / "interface.json";
    std::vector<std::string> interfaceArguments = slangArguments(m_desc, interfaceFile);
    for (const std::string_view argument :
         {std::string_view("-entry"), kInterfaceEntry, std::string_view("-stage"),
          std::string_view("fragment"), std::string_view("-o")})
    {
        interfaceArguments.emplace_back(argument);
    }
    interfaceArguments.push_back(pathUtf8(work.path() / "interface.spv"));
    interfaceArguments.emplace_back("-reflection-json");
    interfaceArguments.push_back(pathUtf8(reflectionFile));
    std::vector<std::string> probeArguments = interfaceArguments;
    probeArguments.emplace_back("-DILLUSION_PROBE_WORLD_OFFSET=1");

    ShaderAsset shader;
    shader.source = std::string(virtualPath);
    shader.libraryHash = m_libraryHash;
    shader.compilerVersion = m_version;
    std::vector<ShaderDiagnostic> probe;
    shader.worldOffset = SlangRun(m_desc, probeArguments).finish(probe);
    if (!shader.worldOffset)
    {
        if (!SlangRun(m_desc, interfaceArguments).finish(result.diagnostics))
        {
            return result;
        }
        if (!missingWorldOffset(probe))
        {
            mergeDiagnostics(result.diagnostics, std::move(probe));
            result.diagnostics.push_back(
                error(std::string(virtualPath), 0, 0,
                      "worldOffset() must be declared as float3 worldOffset(WorldOffsetInput "
                      "input)"));
            return result;
        }
    }
    else
    {
        mergeDiagnostics(result.diagnostics, std::move(probe));
    }
    std::string reflection;
    if (!readFileBytes(reflectionFile, reflection))
    {
        result.diagnostics.push_back(error(std::string(virtualPath), 0, 0, "slangc wrote no reflection"));
        return result;
    }
    const SourceLocator locator(virtualPath, source);
    readReflection(reflection, locator, shader, result.diagnostics);
    const bool decal = shader.decalChannels != DecalChannels::None;
    if (decal && shader.worldOffset)
    {
        result.diagnostics.push_back(locator.wordError(
            "worldOffset",
            "a decal shader cannot have worldOffset(): decals do not move the surfaces they land on"));
    }
    if (hasError(result.diagnostics))
    {
        return result;
    }
    if (!writeFileBytes(passFile, wrapSource(virtualPath, source, decal ? kDecalPassFile : kPassFile)))
    {
        result.diagnostics.push_back(
            error(std::string(virtualPath), 0, 0,
                  "cannot write the compiler's temporary files in " + pathUtf8(work.path())));
        return result;
    }

    std::vector<VariantKey> keys;
    for (u32 pass = 0; pass < kShaderPassCount; ++pass)
    {
        for (const AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
        {
            if (shaderVariantExists(static_cast<ShaderPass>(pass), mode, shader.decalChannels))
            {
                keys.push_back(VariantKey{.pass = static_cast<ShaderPass>(pass), .alphaMode = mode});
            }
        }
    }
    const std::string shadingModel =
        std::format("-DILLUSION_SHADING_MODEL={}", static_cast<u32>(shader.shadingModel));
    const std::string padding = std::format(
        "-DILLUSION_PARAMETER_PADDING={}", (kMaxShaderParameterBytes - structuredParameterBytes(shader)) / 4);
    const std::string decalChannels =
        std::format("-DILLUSION_DECAL_CHANNELS={}", static_cast<u32>(shader.decalChannels));
    std::vector<std::filesystem::path> outputs(keys.size());
    bool compiled = true;
    for (usize first = 0; first < keys.size(); first += m_desc.maxProcesses)
    {
        const usize last = std::min(keys.size(), first + m_desc.maxProcesses);
        std::vector<SlangRun> runs;
        runs.reserve(last - first);
        for (usize i = first; i < last; ++i)
        {
            outputs[i] = work.path() / std::format("variant{}.spv", i);
            std::vector<std::string> arguments = slangArguments(m_desc, passFile);
            arguments.emplace_back(passDefine(keys[i].pass));
            if (keys[i].alphaMode == AlphaMode::Mask)
            {
                arguments.emplace_back("-DILLUSION_ALPHA_MASK=1");
            }
            else if (keys[i].alphaMode == AlphaMode::Blend)
            {
                arguments.emplace_back("-DILLUSION_ALPHA_BLEND=1");
            }
            if (shader.worldOffset)
            {
                arguments.emplace_back("-DILLUSION_WORLD_OFFSET=1");
            }
            if (!shader.parameters.empty() || !shader.textures.empty())
            {
                arguments.emplace_back("-DILLUSION_MATERIAL_PARAMETERS=1");
            }
            arguments.push_back(shadingModel);
            arguments.push_back(padding);
            if (decal)
            {
                arguments.push_back(decalChannels);
            }
            for (const std::string_view argument :
                 {std::string_view("-entry"), kShaderVertexEntry, std::string_view("-stage"),
                  std::string_view("vertex"), std::string_view("-entry"), kShaderFragmentEntry,
                  std::string_view("-stage"), std::string_view("fragment"), std::string_view("-o")})
            {
                arguments.emplace_back(argument);
            }
            arguments.push_back(pathUtf8(outputs[i]));
            runs.emplace_back(m_desc, std::move(arguments));
        }
        for (SlangRun& run : runs)
        {
            compiled = run.finish(result.diagnostics) && compiled;
        }
    }
    if (!compiled)
    {
        return result;
    }
    for (usize i = 0; i < keys.size(); ++i)
    {
        std::string spirv;
        if (!readFileBytes(outputs[i], spirv) || spirv.empty() || spirv.size() % 4 != 0)
        {
            result.diagnostics.push_back(
                error(std::string(virtualPath), 0, 0,
                      std::format("slangc wrote no SPIR-V for the {} {} variant",
                                  shaderPassName(keys[i].pass), alphaModeName(keys[i].alphaMode))));
            return result;
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(spirv.data());
        shader.variants.push_back(
            ShaderVariant{.pass = keys[i].pass,
                          .alphaMode = keys[i].alphaMode,
                          .spirv = std::vector<std::byte>(bytes, bytes + spirv.size())});
    }
    result.shader = std::move(shader);
    return result;
}

} // namespace illusion
