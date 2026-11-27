// The example shaders of the docs: pulse.shader in the manual page "Material shaders" and dissolve.shader in
// the tutorial "A dissolving surface" (docs/content/{en,tr}/manual/material-shaders.md and
// tutorials/dissolve-shader.md). Both pages show the files of tests/data/docs unchanged, and the files
// compile with the pinned slangc.

#include <illusion/assets/ShaderCompiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace illusion;

namespace
{

std::string readText(const std::string& path)
{
    std::ifstream file(utf8Path(path), std::ios::binary);
    REQUIRE_MESSAGE(file.is_open(), path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string normalized;
    for (const char c : buffer.str())
    {
        if (c != '\r')
        {
            normalized += c;
        }
    }
    return normalized;
}

std::string exampleShader(const char* name)
{
    return readText(std::string(ILLUSION_TEST_DATA_DIR) + "/docs/" + name);
}

ShaderCompileResult compileExample(const char* name)
{
    Expected<ShaderCompiler, std::string> compiler =
        ShaderCompiler::create(ShaderCompilerDesc{.slangc = utf8Path(ILLUSION_TEST_SLANGC),
                                                  .library = utf8Path(ILLUSION_TEST_SHADER_LIBRARY),
                                                  .workDirectory = {},
                                                  .timeout = std::chrono::minutes(2),
                                                  .maxProcesses = 0});
    REQUIRE_MESSAGE(compiler.hasValue(), compiler.error());
    return compiler->compile(std::string("assets:/shaders/") + name, exampleShader(name));
}

// True if @p page has @p shader as the body of a fenced `slang` block.
bool pageShows(const std::string& page, const std::string& shader)
{
    return page.find("```slang\n" + shader + "```") != std::string::npos;
}

} // namespace

TEST_CASE("the example shader of the manual compiles and both languages show it unchanged")
{
    const ShaderCompileResult result = compileExample("pulse.shader");
    std::string diagnostics;
    for (const ShaderDiagnostic& diagnostic : result.diagnostics)
    {
        diagnostics += formatShaderDiagnostic(diagnostic) + "\n";
    }
    INFO(diagnostics);
    REQUIRE(result.succeeded());
    CHECK(result.shader->worldOffset);
    CHECK(result.shader->shadingModel == ShadingModel::Default);
    REQUIRE(result.shader->parameters.size() == 5);
    CHECK(result.shader->textures.size() == 1);
    const std::string shader = exampleShader("pulse.shader");
    for (const char* language : {"en", "tr"})
    {
        CHECK_MESSAGE(pageShows(readText(std::string(ILLUSION_DOCS_DIR) + "/" + language +
                                         "/manual/material-shaders.md"),
                                shader),
                      language);
    }
}

TEST_CASE("the shader of the tutorial compiles and both languages show it unchanged")
{
    const ShaderCompileResult result = compileExample("dissolve.shader");
    std::string diagnostics;
    for (const ShaderDiagnostic& diagnostic : result.diagnostics)
    {
        diagnostics += formatShaderDiagnostic(diagnostic) + "\n";
    }
    INFO(diagnostics);
    REQUIRE(result.succeeded());
    CHECK_FALSE(result.shader->worldOffset);
    CHECK(result.shader->parameters.size() == 4);
    const std::string shader = exampleShader("dissolve.shader");
    for (const char* language : {"en", "tr"})
    {
        CHECK_MESSAGE(pageShows(readText(std::string(ILLUSION_DOCS_DIR) + "/" + language +
                                         "/tutorials/dissolve-shader.md"),
                                shader),
                      language);
    }
}
