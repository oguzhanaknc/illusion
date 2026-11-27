// The material shader compiler (ADR 0063) with the pinned slangc and the engine's shader library: compiled
// shaders, every diagnostic path with its file, line and column, the import cache and the cooker.

#include "TestSupport.hpp"

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/ShaderCompiler.hpp>
#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr std::string_view kGlowShader = R"([ShadingModel("Unlit")]
struct Parameters
{
    [Color(1, 0.5, 0.25, 1)] float4 tint;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(-3)] int steps;
    [Default(1)] bool pulse;
    [Default2(0.5, 0.25)] float2 scroll;
    [Texture("black", "mask")] Texture2DSlot mask;
    [Texture("game:/textures/detail_n.png", "normal")] Texture2DSlot detail;
    float3 direction;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    const float2 uv = input.uv + p.scroll * input.time;
    output.baseColor = p.tint.rgb * sampleTexture(input, p.mask, uv).r;
    output.normal = unpackNormal(sampleTexture(input, p.detail, uv), 1.0);
    output.emission = p.tint.rgb * p.intensity * (p.pulse ? 0.5 + 0.5 * sin(input.time) : 1.0);
    output.opacity = p.tint.a * valueNoise(input.worldPosition * float(p.steps));
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    return float3(0.0, 0.1 * sin(input.time + input.worldPosition.x), 0.0);
}
)";

constexpr std::string_view kPlainShader = R"(struct Parameters
{
};

SurfaceOutput surface(SurfaceInput input)
{
    return defaultSurface();
}
)";

// A decal shader writing @p channels that uses the decal API.
std::string decalSource(std::string_view channels)
{
    return "[Decal(\"" + std::string(channels) + R"(")]
struct Parameters
{
    [Color(1, 0.5, 0.25, 1)] float4 tint;
    [Default(4)] [Range(1, 8)] int columns;
    [Texture("white", "color")] Texture2DSlot albedo;
    [Texture("normal", "normal")] Texture2DSlot bumps;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float2 uv = decalAtlasUv(input.uv, decalVariant(input), uint(p.columns), 2u);
    const float4 texel = sampleTexture(input, p.albedo, uv);
    SurfaceOutput output = defaultSurface();
    output.baseColor = texel.rgb * p.tint.rgb * input.color.rgb;
    output.normal = unpackNormal(sampleTexture(input, p.bumps, uv * 2.0), 1.0);
    output.roughness = 0.2 + 0.6 * valueNoise(input.worldPosition * 4.0);
    output.emission = p.tint.rgb * (1.0 - decalLife(input)) * step(decalAge(input), 2.0);
    output.opacity = texel.a * p.tint.a;
    return output;
}
)";
}

// The 1-based line and column of the first @p text in @p source.
std::pair<u32, u32> locate(std::string_view source, std::string_view text)
{
    const usize at = source.find(text);
    REQUIRE(at != std::string_view::npos);
    const usize lineStart = source.rfind('\n', at);
    const auto line = static_cast<u32>(
        std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(at), '\n') + 1);
    return {line, static_cast<u32>(lineStart == std::string_view::npos ? at + 1 : at - lineStart)};
}

ShaderCompiler makeCompiler(const std::filesystem::path& library = utf8Path(ILLUSION_TEST_SHADER_LIBRARY))
{
    Expected<ShaderCompiler, std::string> compiler =
        ShaderCompiler::create(ShaderCompilerDesc{.slangc = utf8Path(ILLUSION_TEST_SLANGC),
                                                  .library = library,
                                                  .workDirectory = {},
                                                  .timeout = std::chrono::minutes(2),
                                                  .maxProcesses = 0});
    REQUIRE_MESSAGE(compiler.hasValue(), compiler.error());
    return std::move(*compiler);
}

const ShaderCompiler& sharedCompiler()
{
    static const ShaderCompiler compiler = makeCompiler();
    return compiler;
}

// The errors of @p result (warnings and notes left out).
std::vector<ShaderDiagnostic> errorsOf(const ShaderCompileResult& result)
{
    std::vector<ShaderDiagnostic> errors;
    std::copy_if(result.diagnostics.begin(), result.diagnostics.end(), std::back_inserter(errors),
                 [](const ShaderDiagnostic& diagnostic) {
                     return diagnostic.severity == ShaderDiagnosticSeverity::Error;
                 });
    return errors;
}

std::string describe(const ShaderCompileResult& result)
{
    std::string text;
    for (const ShaderDiagnostic& diagnostic : result.diagnostics)
    {
        text += formatShaderDiagnostic(diagnostic) + "\n";
    }
    return text;
}

// Compiles @p source as game:/test.shader and checks that it fails with an error at @p line and @p column of
// that file whose message contains @p text.
void checkError(std::string_view source, u32 line, u32 column, std::string_view text)
{
    const ShaderCompileResult result = sharedCompiler().compile("game:/test.shader", source);
    INFO(describe(result));
    CHECK_FALSE(result.succeeded());
    const std::vector<ShaderDiagnostic> errors = errorsOf(result);
    REQUIRE_FALSE(errors.empty());
    const auto found = std::find_if(errors.begin(), errors.end(), [&](const ShaderDiagnostic& diagnostic) {
        return diagnostic.file == "game:/test.shader" && diagnostic.line == line &&
               diagnostic.column == column && diagnostic.message.find(text) != std::string::npos;
    });
    CHECK(found != errors.end());
}

std::string withParameters(std::string_view fields)
{
    return "struct Parameters\n{\n" + std::string(fields) +
           "};\n\nSurfaceOutput surface(SurfaceInput input)\n{\n    return defaultSurface();\n}\n";
}

void writeText(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

TEST_CASE("the shader compiler knows the version of slangc and the hash of the shader library")
{
    const ShaderCompiler& compiler = sharedCompiler();
    CHECK_FALSE(compiler.compilerVersion().empty());
    CHECK(compiler.compilerVersion().find('\n') == std::string::npos);
    CHECK(compiler.libraryHash() != 0);
    CHECK(makeCompiler().libraryHash() == compiler.libraryHash());
    CHECK(compiler.desc().maxProcesses >= 1);
}

TEST_CASE("a shader compiler cannot be created without slangc or the shader library")
{
    test::TempDirectory directory;
    const Expected<ShaderCompiler, std::string> noCompiler =
        ShaderCompiler::create(ShaderCompilerDesc{.slangc = directory.path / "slangc",
                                                  .library = utf8Path(ILLUSION_TEST_SHADER_LIBRARY),
                                                  .workDirectory = {},
                                                  .timeout = std::chrono::minutes(1),
                                                  .maxProcesses = 1});
    REQUIRE_FALSE(noCompiler.hasValue());
    CHECK(noCompiler.error().find("does not exist") != std::string::npos);
    const Expected<ShaderCompiler, std::string> noLibrary =
        ShaderCompiler::create(ShaderCompilerDesc{.slangc = utf8Path(ILLUSION_TEST_SLANGC),
                                                  .library = directory.path,
                                                  .workDirectory = {},
                                                  .timeout = std::chrono::minutes(1),
                                                  .maxProcesses = 1});
    REQUIRE_FALSE(noLibrary.hasValue());
    CHECK(noLibrary.error().find("illusion/Surface.slang") != std::string::npos);
}

TEST_CASE("a material shader compiles into every pass and alpha mode with its parameter layout")
{
    const ShaderCompileResult result = sharedCompiler().compile("game:/materials/glow.shader", kGlowShader);
    INFO(describe(result));
    REQUIRE(result.succeeded());
    CHECK(errorsOf(result).empty());
    const ShaderAsset& shader = *result.shader;
    CHECK(shader.source == "game:/materials/glow.shader");
    CHECK(shader.shadingModel == ShadingModel::Unlit);
    CHECK(shader.worldOffset);
    CHECK(shader.compilerVersion == sharedCompiler().compilerVersion());
    CHECK(shader.libraryHash == sharedCompiler().libraryHash());

    REQUIRE(shader.variants.size() == 15);
    for (const ShaderVariant& variant : shader.variants)
    {
        CAPTURE(shaderPassName(variant.pass));
        CAPTURE(alphaModeName(variant.alphaMode));
        REQUIRE(variant.spirv.size() >= 20);
        CHECK(variant.spirv.size() % 4 == 0);
        u32 magic = 0;
        std::memcpy(&magic, variant.spirv.data(), sizeof(magic));
        CHECK(magic == 0x07230203u);
        const std::string_view text(reinterpret_cast<const char*>(variant.spirv.data()),
                                    variant.spirv.size());
        CHECK(text.find(kShaderVertexEntry) != std::string_view::npos);
        CHECK(text.find(kShaderFragmentEntry) != std::string_view::npos);
    }
    CHECK(shader.variant(ShaderPass::ShadowCaster, AlphaMode::Mask) != nullptr);
    CHECK(shader.variant(ShaderPass::ShadowCaster, AlphaMode::Blend) == nullptr);

    REQUIRE(shader.parameters.size() == 6);
    const ShaderParameter& tint = shader.parameters[0];
    CHECK(tint.name == "tint");
    CHECK(tint.type == ShaderParameterType::Float4);
    CHECK(tint.offset == 0);
    CHECK(tint.color);
    CHECK(tint.defaultValue == std::array<f32, 4>{1.0f, 0.5f, 0.25f, 1.0f});
    const ShaderParameter& intensity = shader.parameters[1];
    CHECK(intensity.type == ShaderParameterType::Float);
    CHECK(intensity.offset == 16);
    CHECK(intensity.defaultValue[0] == 2.5f);
    CHECK(intensity.hasRange);
    CHECK(intensity.minimum == 0.0f);
    CHECK(intensity.maximum == 10.0f);
    CHECK(shader.parameters[2].type == ShaderParameterType::Int);
    CHECK(shader.parameters[2].defaultValue[0] == -3.0f);
    CHECK(shader.parameters[3].type == ShaderParameterType::Bool);
    CHECK(shader.parameters[3].offset == 24);
    CHECK(shader.parameters[4].type == ShaderParameterType::Float2);
    CHECK(shader.parameters[4].offset == 32);
    CHECK(shader.parameters[4].defaultValue == std::array<f32, 4>{0.5f, 0.25f, 0.0f, 0.0f});
    CHECK(shader.parameters[5].name == "direction");
    CHECK(shader.parameters[5].type == ShaderParameterType::Float3);
    CHECK(shader.parameters[5].defaultValue == std::array<f32, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(shader.parameters[5].offset == 48);
    CHECK(shader.parameterBytes == 64);

    REQUIRE(shader.textures.size() == 2);
    CHECK(shader.textures[0].name == "mask");
    CHECK(shader.textures[0].slot == 0);
    CHECK(shader.textures[0].offset == 40);
    CHECK(shader.textures[0].usage == ShaderTextureUsage::Mask);
    CHECK(shader.textures[0].fallback == ShaderTextureFallback::Black);
    CHECK(shader.textures[0].defaultTexture.empty());
    CHECK(shader.textures[1].slot == 1);
    CHECK(shader.textures[1].offset == 44);
    CHECK(shader.textures[1].usage == ShaderTextureUsage::Normal);
    CHECK(shader.textures[1].defaultTexture == "game:/textures/detail_n.png");
}

TEST_CASE("compiling the same shader twice gives the same bytes")
{
    const ShaderCompileResult first = sharedCompiler().compile("game:/materials/glow.shader", kGlowShader);
    const ShaderCompileResult second = sharedCompiler().compile("game:/materials/glow.shader", kGlowShader);
    REQUIRE(first.succeeded());
    REQUIRE(second.succeeded());
    CHECK(serializeShader(*first.shader) == serializeShader(*second.shader));
}

TEST_CASE("a shader without parameters or worldOffset compiles with the default shading model")
{
    const ShaderCompileResult result = sharedCompiler().compile("game:/plain.shader", kPlainShader);
    INFO(describe(result));
    REQUIRE(result.succeeded());
    CHECK(result.diagnostics.empty());
    CHECK(result.shader->shadingModel == ShadingModel::Default);
    CHECK_FALSE(result.shader->worldOffset);
    CHECK(result.shader->parameters.empty());
    CHECK(result.shader->textures.empty());
    CHECK(result.shader->parameterBytes == 0);
    CHECK(result.shader->variants.size() == 15);
}

TEST_CASE("syntax and type errors name the shader file with line and column")
{
    SUBCASE("a syntax error")
    {
        checkError("struct Parameters\n{\n};\n\nSurfaceOutput surface(SurfaceInput input)\n{\n    return "
                   "defaultSurface()\n}\n",
                   8, 1, "");
    }
    SUBCASE("an undefined identifier")
    {
        checkError("struct Parameters\n{\n};\n\nSurfaceOutput surface(SurfaceInput input)\n{\n"
                   "    SurfaceOutput output = defaultSurface();\n    output.roughness = undefinedThing;\n"
                   "    return output;\n}\n",
                   8, 24, "undefinedThing");
    }
}

TEST_CASE("a shader without surface() or Parameters does not compile")
{
    SUBCASE("no surface function")
    {
        const ShaderCompileResult result =
            sharedCompiler().compile("game:/test.shader", "struct Parameters\n{\n};\n");
        INFO(describe(result));
        CHECK_FALSE(result.succeeded());
        const std::vector<ShaderDiagnostic> errors = errorsOf(result);
        CHECK(std::any_of(errors.begin(), errors.end(), [](const ShaderDiagnostic& diagnostic) {
            return diagnostic.message.find("'surface'") != std::string::npos && diagnostic.line > 0;
        }));
    }
    SUBCASE("no Parameters")
    {
        const ShaderCompileResult result = sharedCompiler().compile(
            "game:/test.shader",
            "SurfaceOutput surface(SurfaceInput input)\n{\n    return defaultSurface();\n}\n");
        INFO(describe(result));
        CHECK_FALSE(result.succeeded());
        const std::vector<ShaderDiagnostic> errors = errorsOf(result);
        CHECK(std::any_of(errors.begin(), errors.end(), [](const ShaderDiagnostic& diagnostic) {
            return diagnostic.message.find("Parameters") != std::string::npos;
        }));
    }
}

TEST_CASE("a worldOffset() with the wrong signature is an error")
{
    const ShaderCompileResult result = sharedCompiler().compile(
        "game:/test.shader",
        std::string(kPlainShader) + "\nfloat3 worldOffset(float3 position)\n{\n    return position;\n}\n");
    INFO(describe(result));
    CHECK_FALSE(result.succeeded());
    const std::vector<ShaderDiagnostic> errors = errorsOf(result);
    CHECK(std::any_of(errors.begin(), errors.end(), [](const ShaderDiagnostic& diagnostic) {
        return diagnostic.file == "game:/test.shader" &&
               diagnostic.message.find("float3 worldOffset(WorldOffsetInput input)") != std::string::npos;
    }));
}

TEST_CASE("parameter checks name the field's line and column")
{
    SUBCASE("an unsupported type")
    {
        checkError(withParameters("    float4 tint;\n    float4x4 transform;\n"), 4, 14, "float, float2");
    }
    SUBCASE("a default that does not fit the type")
    {
        checkError(withParameters("    [Default(1)] float3 offset;\n"), 3, 25,
                   "[Default] does not fit the type float3");
    }
    SUBCASE("a colour that is not a vector of three or four")
    {
        checkError(withParameters("    [Color(1, 0, 0, 1)] float tint;\n"), 3, 31,
                   "colours are float3 or float4");
    }
    SUBCASE("two defaults")
    {
        checkError(withParameters("    [Default(1)] [Default(2)] float value;\n"), 3, 37, "two defaults");
    }
    SUBCASE("an int default with a fraction")
    {
        checkError(withParameters("    [Default(1.5)] int count;\n"), 3, 24, "whole number");
    }
    SUBCASE("a bool default other than 0 or 1")
    {
        checkError(withParameters("    [Default(2)] bool flag;\n"), 3, 23, "0 or 1");
    }
    SUBCASE("a range on a vector")
    {
        checkError(withParameters("    [Range(0, 1)] float2 uv;\n"), 3, 26,
                   "[Range] applies to float and int");
    }
    SUBCASE("a range with its ends swapped")
    {
        checkError(withParameters("    [Range(5, 1)] float amount;\n"), 3, 25, "minimum above its maximum");
    }
    SUBCASE("a texture attribute on a float")
    {
        checkError(withParameters("    [Texture(\"white\", \"color\")] float mask;\n"), 3, 39,
                   "Texture2DSlot");
    }
    SUBCASE("a texture with an unknown default")
    {
        checkError(withParameters("    [Texture(\"grey\", \"color\")] Texture2DSlot mask;\n"), 3, 46,
                   "'grey'");
    }
    SUBCASE("a texture with an unknown usage")
    {
        checkError(withParameters("    [Texture(\"white\", \"albedo\")] Texture2DSlot mask;\n"), 3, 48,
                   "'albedo'");
    }
    SUBCASE("a uint without a texture attribute")
    {
        checkError(withParameters("    uint count;\n"), 3, 10, "uint32");
    }
}

TEST_CASE("Parameters larger than 256 bytes, with more than eight textures or an unknown shading model fail")
{
    SUBCASE("too many bytes")
    {
        std::string fields;
        for (u32 i = 0; i < 17; ++i)
        {
            fields += "    float4 value" + std::to_string(i) + ";\n";
        }
        checkError(withParameters(fields), 1, 8, "at most 256");
    }
    SUBCASE("too many textures")
    {
        std::string fields;
        for (u32 i = 0; i < 9; ++i)
        {
            fields += "    [Texture(\"white\", \"color\")] Texture2DSlot texture" + std::to_string(i) + ";\n";
        }
        checkError(withParameters(fields), 1, 8, "at most 8");
    }
    SUBCASE("an unknown shading model")
    {
        checkError("[ShadingModel(\"Toon\")]\n" + withParameters(""), 2, 8, "unknown shading model 'Toon'");
    }
}

TEST_CASE("warnings of a shader that compiles are reported once")
{
    const std::string source =
        "struct Parameters\n{\n};\n\nSurfaceOutput surface(SurfaceInput input)\n{\n"
        "    SurfaceOutput output = defaultSurface();\n    int unused;\n    return output;\n}\n";
    const ShaderCompileResult result = sharedCompiler().compile("game:/test.shader", source);
    INFO(describe(result));
    REQUIRE(result.succeeded());
    CHECK(errorsOf(result).empty());
    for (usize i = 0; i < result.diagnostics.size(); ++i)
    {
        for (usize j = i + 1; j < result.diagnostics.size(); ++j)
        {
            CHECK_FALSE(result.diagnostics[i] == result.diagnostics[j]);
        }
    }
}

TEST_CASE("malformed shader text never crashes the compiler")
{
    std::string binary;
    for (u32 i = 0; i < 512; ++i)
    {
        binary.push_back(static_cast<char>((i * 37u + 11u) & 0xFFu));
    }
    for (const std::string_view source :
         {std::string_view(), std::string_view("}}}}"), std::string_view("#include \"missing.slang\"\n"),
          std::string_view("#line 1 \"x\"\n\"unterminated"), std::string_view(binary)})
    {
        CAPTURE(source.size());
        const ShaderCompileResult result = sharedCompiler().compile("game:/test.shader", source);
        CHECK_FALSE(result.succeeded());
        CHECK_FALSE(errorsOf(result).empty());
    }
}

TEST_CASE("a compile that takes longer than its timeout is ended")
{
    Expected<ShaderCompiler, std::string> compiler =
        ShaderCompiler::create(ShaderCompilerDesc{.slangc = utf8Path(ILLUSION_TEST_SLANGC),
                                                  .library = utf8Path(ILLUSION_TEST_SHADER_LIBRARY),
                                                  .workDirectory = {},
                                                  .timeout = std::chrono::milliseconds(0),
                                                  .maxProcesses = 1});
    REQUIRE(compiler.hasValue());
    const ShaderCompileResult result = compiler->compile("game:/plain.shader", kPlainShader);
    CHECK_FALSE(result.succeeded());
    const std::vector<ShaderDiagnostic> errors = errorsOf(result);
    CHECK(std::any_of(errors.begin(), errors.end(), [](const ShaderDiagnostic& diagnostic) {
        return diagnostic.message.find("did not finish") != std::string::npos;
    }));
}

TEST_CASE("slangc diagnostics are read with the detail of their spans")
{
    const std::vector<ShaderDiagnostic> diagnostics =
        parseSlangDiagnostics("E30015\terror\tgame:/a.shader\t6\t23\t6\t37\tundefined identifier\r\n"
                              "E30015\tspan\tgame:/a.shader\t6\t23\t6\t37\tundefined identifier 'x'.\n"
                              "E30015\tnote\tC:/lib/illusion/Surface.slang\t3\t1\t3\t4\tdid you mean 'y'?\n"
                              "W41012\twarning\tgame:/a.shader\t2\t5\t2\t9\tunused variable\n"
                              "W41012\tspan\tgame:/a.shader\t2\t5\t2\t9\tdeclared here\n"
                              "\n"
                              "slangc: internal failure\n");
    REQUIRE(diagnostics.size() == 4);
    CHECK(diagnostics[0] == ShaderDiagnostic{.file = "game:/a.shader",
                                             .line = 6,
                                             .column = 23,
                                             .severity = ShaderDiagnosticSeverity::Error,
                                             .code = "E30015",
                                             .message = "undefined identifier 'x'."});
    CHECK(diagnostics[1].severity == ShaderDiagnosticSeverity::Note);
    CHECK(diagnostics[1].file == "C:/lib/illusion/Surface.slang");
    CHECK(diagnostics[2].severity == ShaderDiagnosticSeverity::Warning);
    CHECK(diagnostics[2].message == "unused variable: declared here");
    CHECK(diagnostics[3].severity == ShaderDiagnosticSeverity::Error);
    CHECK(diagnostics[3].file.empty());
    CHECK(diagnostics[3].message == "slangc: internal failure");
    CHECK(parseSlangDiagnostics("").empty());
}

TEST_CASE("shader diagnostics format like GCC messages")
{
    CHECK(formatShaderDiagnostic(ShaderDiagnostic{.file = "game:/a.shader",
                                                  .line = 6,
                                                  .column = 23,
                                                  .severity = ShaderDiagnosticSeverity::Error,
                                                  .code = "E30015",
                                                  .message = "undefined identifier 'x'."}) ==
          "game:/a.shader:6:23: error: undefined identifier 'x'. [E30015]");
    CHECK(formatShaderDiagnostic(ShaderDiagnostic{.file = "game:/a.shader",
                                                  .line = 2,
                                                  .column = 0,
                                                  .severity = ShaderDiagnosticSeverity::Warning,
                                                  .code = {},
                                                  .message = "careful"}) ==
          "game:/a.shader:2: warning: careful");
    CHECK(formatShaderDiagnostic(ShaderDiagnostic{.file = {},
                                                  .line = 0,
                                                  .column = 0,
                                                  .severity = ShaderDiagnosticSeverity::Note,
                                                  .code = {},
                                                  .message = "more"}) == "note: more");
    CHECK(shaderDiagnosticSeverityName(ShaderDiagnosticSeverity::Warning) == "warning");
}

TEST_CASE("the import cache keeps a compiled shader until its source, library or compiler changes")
{
    test::TempDirectory directory;
    writeText(directory.path / "game" / "glow.shader", kGlowShader);
    std::filesystem::create_directories(directory.path / "cache");
    VirtualFileSystem files;
    REQUIRE(files.mount("game", directory.path / "game", MountAccess::ReadWrite) == FileError::None);
    REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);
    const ShaderCompiler& compiler = sharedCompiler();

    AssetDatabase database(files, "cache");
    std::vector<ShaderDiagnostic> diagnostics;
    std::string error;
    const std::optional<ShaderAsset> first =
        database.loadShader("game:/glow.shader", &compiler, &diagnostics, &error);
    INFO(error);
    REQUIRE(first.has_value());
    CHECK(database.statistics().imports == 1);
    CHECK(database.statistics().cacheWrites == 1);

    const std::optional<ShaderAsset> cached = database.loadShader("game:/glow.shader", &compiler);
    REQUIRE(cached.has_value());
    CHECK(*cached == *first);
    CHECK(database.statistics().cacheHits == 1);
    CHECK(database.statistics().imports == 1);

    // Without a compiler the cooked shader still loads (the player has none).
    CHECK(database.loadShader("game:/glow.shader", nullptr).has_value());
    CHECK(database.statistics().cacheHits == 2);

    // Another shader library (another engine version) makes the entry stale.
    test::TempDirectory libraryCopy;
    std::filesystem::copy(utf8Path(ILLUSION_TEST_SHADER_LIBRARY), libraryCopy.path,
                          std::filesystem::copy_options::recursive);
    writeText(libraryCopy.path / "illusion" / "Extra.slang", "// another engine version\n");
    const ShaderCompiler changedLibrary = makeCompiler(libraryCopy.path);
    CHECK(changedLibrary.libraryHash() != compiler.libraryHash());
    const std::optional<ShaderAsset> recompiled = database.loadShader("game:/glow.shader", &changedLibrary);
    REQUIRE(recompiled.has_value());
    CHECK(recompiled->libraryHash == changedLibrary.libraryHash());
    CHECK(database.statistics().imports == 2);
    CHECK(database.loadShader("game:/glow.shader", &changedLibrary).has_value());
    CHECK(database.statistics().imports == 2);

    // An edited source is compiled again.
    writeText(directory.path / "game" / "glow.shader", std::string(kGlowShader) + "\n// edited\n");
    CHECK(database.loadShader("game:/glow.shader", &changedLibrary).has_value());
    CHECK(database.statistics().imports == 3);
}

TEST_CASE("a shader that does not compile is not cached and reports its diagnostics")
{
    test::TempDirectory directory;
    writeText(
        directory.path / "game" / "broken.shader",
        "struct Parameters {};\nSurfaceOutput surface(SurfaceInput input)\n{\n    return nothing;\n}\n");
    std::filesystem::create_directories(directory.path / "cache");
    VirtualFileSystem files;
    REQUIRE(files.mount("game", directory.path / "game") == FileError::None);
    REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);
    AssetDatabase database(files, "cache");
    std::vector<ShaderDiagnostic> diagnostics;
    std::string error;
    CHECK_FALSE(
        database.loadShader("game:/broken.shader", &sharedCompiler(), &diagnostics, &error).has_value());
    CHECK(error.find("game:/broken.shader: the shader does not compile") == 0);
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].file == "game:/broken.shader");
    CHECK(diagnostics[0].line == 4);
    CHECK(database.statistics().cacheWrites == 0);

    CHECK_FALSE(database.loadShader("game:/broken.shader", nullptr, nullptr, &error).has_value());
    CHECK(error == "game:/broken.shader: not cooked, and there is no shader compiler");
    CHECK_FALSE(database.loadShader("game:/missing.shader", &sharedCompiler(), nullptr, &error).has_value());
    CHECK(error.find("game:/missing.shader") == 0);
}

TEST_CASE("cooking compiles shaders into an archive that loads without sources or compiler")
{
    test::TempDirectory directory;
    writeText(directory.path / "game" / "materials" / "glow.shader", kGlowShader);
    writeText(directory.path / "game" / "materials" / "plain.shader", kPlainShader);
    writeText(directory.path / "game" / "materials" / "zbroken.shader", "struct Parameters {};\n}\n");
    std::filesystem::create_directories(directory.path / "cache");
    VirtualFileSystem files;
    REQUIRE(files.mount("game", directory.path / "game") == FileError::None);
    REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);

    const CookResult withoutCompiler = cookAssets(files, "game:/", "cache");
    CHECK(withoutCompiler.shaders == 0);
    REQUIRE(withoutCompiler.errors.size() == 3);
    CHECK(withoutCompiler.errors[0] ==
          "game:/materials/glow.shader: cooking material shaders needs the shader compiler (slangc)");

    const CookResult result = cookAssets(files, "game:/", "cache", &sharedCompiler());
    CHECK(result.shaders == 2);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].find("game:/materials/zbroken.shader") == 0);
    REQUIRE_FALSE(result.shaderDiagnostics.empty());
    CHECK(result.shaderDiagnostics[0].file == "game:/materials/zbroken.shader");
    CHECK(result.shaderDiagnostics[0].line == 2);

    const std::filesystem::path archive = directory.path / "cooked.illpak";
    REQUIRE(packDirectory(directory.path / "cache", archive) == FileError::None);
    VirtualFileSystem shipped;
    REQUIRE(shipped.mountArchive("cache", archive) == FileError::None);
    AssetDatabase database(shipped, "cache", false);
    std::string error;
    const std::optional<ShaderAsset> glow =
        database.loadShader("game:/materials/glow.shader", nullptr, nullptr, &error);
    INFO(error);
    REQUIRE(glow.has_value());
    CHECK(glow->variants.size() == 15);
    CHECK(database.loadShader("game:/materials/plain.shader", nullptr).has_value());
    CHECK_FALSE(database.loadShader("game:/materials/zbroken.shader", nullptr).has_value());
}

TEST_CASE("a decal shader compiles into the decal variants of its channels and nothing else")
{
    for (u32 bits = 1; bits <= 15; ++bits)
    {
        const auto channels = static_cast<DecalChannels>(bits);
        const std::string names = decalChannelsName(channels);
        CAPTURE(names);
        const ShaderCompileResult result =
            sharedCompiler().compile("game:/decals/scorch.shader", decalSource(names));
        INFO(describe(result));
        REQUIRE(result.succeeded());
        CHECK(errorsOf(result).empty());
        const ShaderAsset& shader = *result.shader;
        CHECK(shader.decalChannels == channels);
        CHECK(shader.shadingModel == ShadingModel::Default);
        CHECK_FALSE(shader.worldOffset);
        REQUIRE(shader.parameters.size() == 2);
        CHECK(shader.parameters[1].name == "columns");
        REQUIRE(shader.textures.size() == 2);
        CHECK(shader.textures[1].usage == ShaderTextureUsage::Normal);

        const bool buffer =
            hasDecalChannel(channels, DecalChannels::BaseColor | DecalChannels::Normal | DecalChannels::ORM);
        const bool emission = hasDecalChannel(channels, DecalChannels::Emission);
        CHECK(shader.variants.size() == (buffer ? 1u : 0u) + (emission ? 1u : 0u));
        CHECK((shader.variant(ShaderPass::Decal, AlphaMode::Blend) != nullptr) == buffer);
        CHECK((shader.variant(ShaderPass::DecalEmission, AlphaMode::Blend) != nullptr) == emission);
        CHECK(shader.variant(ShaderPass::Forward, AlphaMode::Opaque) == nullptr);
        for (const ShaderVariant& variant : shader.variants)
        {
            CAPTURE(shaderPassName(variant.pass));
            REQUIRE(variant.spirv.size() >= 20);
            u32 magic = 0;
            std::memcpy(&magic, variant.spirv.data(), sizeof(magic));
            CHECK(magic == 0x07230203u);
            const std::string_view text(reinterpret_cast<const char*>(variant.spirv.data()),
                                        variant.spirv.size());
            CHECK(text.find(kShaderVertexEntry) != std::string_view::npos);
            CHECK(text.find(kShaderFragmentEntry) != std::string_view::npos);
        }
        const std::optional<ShaderAsset> read = deserializeShader(serializeShader(shader));
        REQUIRE(read.has_value());
        CHECK(*read == shader);
    }
}

TEST_CASE("the standard decal shader writes every channel from an atlas")
{
    const std::filesystem::path path = utf8Path(ILLUSION_TEST_SHADER_LIBRARY) / "illusion" / "Decal.shader";
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream text;
    text << file.rdbuf();
    const ShaderCompileResult result = sharedCompiler().compile("builtin:/shader/decal", text.str());
    INFO(describe(result));
    REQUIRE(result.succeeded());
    CHECK(result.diagnostics.empty());
    const ShaderAsset& shader = *result.shader;
    CHECK(shader.decalChannels == kAllDecalChannels);
    CHECK(shader.variants.size() == 2);
    for (const std::string_view name :
         {"baseColorFactor", "emissiveFactor", "normalScale", "occlusionStrength", "roughnessFactor",
          "metallicFactor", "atlasColumns", "atlasRows"})
    {
        CAPTURE(name);
        CHECK(shader.findParameter(name) != nullptr);
    }
    REQUIRE(shader.textures.size() == 4);
    CHECK(shader.textures[0].name == "baseColorTexture");
    CHECK(shader.textures[1].fallback == ShaderTextureFallback::FlatNormal);
    CHECK(shader.textures[2].usage == ShaderTextureUsage::Linear);
}

TEST_CASE("compiling the same decal shader twice gives the same bytes")
{
    const std::string source = decalSource("BaseColor Normal ORM Emission");
    const ShaderCompileResult first = sharedCompiler().compile("game:/decals/scorch.shader", source);
    const ShaderCompileResult second = sharedCompiler().compile("game:/decals/scorch.shader", source);
    REQUIRE(first.succeeded());
    REQUIRE(second.succeeded());
    CHECK(serializeShader(*first.shader) == serializeShader(*second.shader));
}

TEST_CASE("decal attribute errors name the attribute's line and column")
{
    SUBCASE("[Decal] with [ShadingModel]")
    {
        checkError("[Decal(\"BaseColor\")]\n[ShadingModel(\"Unlit\")]\n" + withParameters(""), 1, 2,
                   "[Decal] and [ShadingModel]");
    }
    SUBCASE("[ShadingModel] before [Decal]")
    {
        checkError("[ShadingModel(\"Default\")] [Decal(\"ORM\")]\n" + withParameters(""), 1, 28,
                   "[Decal] and [ShadingModel]");
    }
    SUBCASE("an unknown channel")
    {
        checkError("// a scorch mark\n[Decal(\"BaseColor Albedo\")]\n" + withParameters(""), 2, 2,
                   "unknown channel 'Albedo'");
    }
    SUBCASE("a channel in the wrong case")
    {
        checkError("[ Decal(\"orm\")]\n" + withParameters(""), 1, 3, "unknown channel 'orm'");
    }
    SUBCASE("an empty channel list")
    {
        checkError("[Decal(\"\")]\n" + withParameters(""), 1, 2, "lists no channel");
    }
    SUBCASE("a list of spaces")
    {
        checkError("[Decal(\"   \")]\n" + withParameters(""), 1, 2, "lists no channel");
    }
    SUBCASE("a channel listed twice")
    {
        checkError("[Decal(\"Normal ORM Normal\")]\n" + withParameters(""), 1, 2,
                   "the channel 'Normal' is listed twice");
    }
}

TEST_CASE("a decal shader with worldOffset() is an error at the function")
{
    const std::string source =
        decalSource("BaseColor") +
        "\nfloat3 worldOffset(WorldOffsetInput input)\n{\n    return float3(0.0, 0.0, 0.0);\n}\n";
    const auto [line, column] = locate(source, "worldOffset(");
    checkError(source, line, column, "a decal shader cannot have worldOffset()");
}

TEST_CASE("surface shaders compile to the same bytes with or without the decal library")
{
    test::TempDirectory libraryCopy;
    std::filesystem::copy(utf8Path(ILLUSION_TEST_SHADER_LIBRARY), libraryCopy.path,
                          std::filesystem::copy_options::recursive);
    REQUIRE(std::filesystem::remove(libraryCopy.path / "illusion" / "Decal.slang"));
    REQUIRE(std::filesystem::remove(libraryCopy.path / "illusion" / "DecalPass.slang"));
    const ShaderCompiler withoutDecals = makeCompiler(libraryCopy.path);
    CHECK(withoutDecals.libraryHash() != sharedCompiler().libraryHash());

    for (const std::string_view source : {kGlowShader, kPlainShader})
    {
        const ShaderCompileResult with = sharedCompiler().compile("game:/surface.shader", source);
        const ShaderCompileResult without = withoutDecals.compile("game:/surface.shader", source);
        INFO(describe(with));
        REQUIRE(with.succeeded());
        REQUIRE(without.succeeded());
        CHECK(with.shader->decalChannels == DecalChannels::None);
        CHECK(with.shader->variants.size() == 15);
        CHECK(with.shader->variants == without.shader->variants);
        CHECK(with.shader->parameters == without.shader->parameters);
        CHECK(with.shader->textures == without.shader->textures);
        CHECK(with.shader->variant(ShaderPass::Decal, AlphaMode::Blend) == nullptr);
    }
    // A decal shader needs the decal library.
    CHECK_FALSE(withoutDecals.compile("game:/decal.shader", decalSource("BaseColor")).succeeded());
}

TEST_CASE("cooked decal shaders load with their channels, and malformed ones never crash the cooker")
{
    test::TempDirectory directory;
    writeText(directory.path / "game" / "decals" / "scorch.shader", decalSource("BaseColor ORM Emission"));
    const std::vector<std::string> broken = {
        "[Decal(\"BaseColor\")]\n",
        "[Decal(\"BaseColor\")]\nstruct Parameters\n{\n};\n",
        "[Decal(\"Glow\")]\n" + withParameters(""),
        "[Decal(42)]\n" + withParameters(""),
        "[Decal]\n" + withParameters(""),
        "[Decal(\"BaseColor\", \"Normal\")]\n" + withParameters(""),
        "[Decal(\"BaseColor\")] [Decal(\"ORM\")]\n" + withParameters(""),
        "[Decal(\"Emission\")]\n[ShadingModel(\"Toon\")]\n" + withParameters(""),
        "[Decal(\"BaseColor\")]\n" + std::string(kGlowShader),
        "[Decal(\"" + std::string(300, 'x') + "\")]\n" + withParameters(""),
    };
    for (usize i = 0; i < broken.size(); ++i)
    {
        writeText(directory.path / "game" / "decals" / ("broken" + std::to_string(i) + ".shader"), broken[i]);
    }
    std::filesystem::create_directories(directory.path / "cache");
    VirtualFileSystem files;
    REQUIRE(files.mount("game", directory.path / "game") == FileError::None);
    REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);

    const CookResult result = cookAssets(files, "game:/", "cache", &sharedCompiler());
    CHECK(result.shaders == 1);
    CHECK(result.errors.size() == broken.size());
    for (const std::string& error : result.errors)
    {
        CHECK(error.find("game:/decals/broken") == 0);
    }

    const std::filesystem::path archive = directory.path / "cooked.illpak";
    REQUIRE(packDirectory(directory.path / "cache", archive) == FileError::None);
    VirtualFileSystem shipped;
    REQUIRE(shipped.mountArchive("cache", archive) == FileError::None);
    AssetDatabase database(shipped, "cache", false);
    std::string error;
    const std::optional<ShaderAsset> scorch =
        database.loadShader("game:/decals/scorch.shader", nullptr, nullptr, &error);
    INFO(error);
    REQUIRE(scorch.has_value());
    CHECK(scorch->decalChannels == (DecalChannels::BaseColor | DecalChannels::ORM | DecalChannels::Emission));
    CHECK(scorch->variants.size() == 2);
}
