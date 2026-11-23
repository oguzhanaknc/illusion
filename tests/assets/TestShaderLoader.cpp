// The background shader loader (ADR 0063): builds on its own thread, keeps the latest build of every path,
// coalesces requests, reports errors with locations and, without a compiler, loads cooked shaders.

#include "TestSupport.hpp"

#include <illusion/assets/ShaderLoader.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr std::string_view kShader = R"(struct Parameters
{
    [Default(2)] float intensity;
};

SurfaceOutput surface(SurfaceInput input)
{
    SurfaceOutput output = defaultSurface();
    output.emission = float3(parameters(input).intensity);
    return output;
}
)";

void writeText(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct LoaderProject
{
    test::TempDirectory directory;
    VirtualFileSystem files;

    LoaderProject()
    {
        std::filesystem::create_directories(directory.path / "cache");
        std::filesystem::create_directories(directory.path / "game");
        REQUIRE(files.mount("game", directory.path / "game", MountAccess::ReadWrite) == FileError::None);
        REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);
    }

    ShaderLoaderDesc compiling() const
    {
        ShaderLoaderDesc desc;
        desc.compiler = ShaderCompilerDesc{.slangc = utf8Path(ILLUSION_TEST_SLANGC),
                                           .library = utf8Path(ILLUSION_TEST_SHADER_LIBRARY),
                                           .workDirectory = {},
                                           .timeout = std::chrono::minutes(2),
                                           .maxProcesses = 0};
        return desc;
    }
};

} // namespace

TEST_CASE("the shader loader builds on its thread and keeps the latest build of each path")
{
    LoaderProject project;
    writeText(project.directory.path / "game" / "glow.shader", kShader);
    ShaderLoader loader(project.files, project.compiling());
    CHECK(loader.canCompile());

    const ShaderLoader::State unknown = loader.state("game:/glow.shader");
    CHECK_FALSE(unknown.building);
    CHECK(unknown.completed == 0);
    CHECK(unknown.build == nullptr);

    loader.request("game:/glow.shader");
    loader.waitIdle();
    const ShaderLoader::State first = loader.state("game:/glow.shader");
    REQUIRE(first.build != nullptr);
    CHECK_FALSE(first.building);
    CHECK(first.completed == 1);
    REQUIRE_MESSAGE(first.build->succeeded(), first.build->error);
    CHECK(first.build->path == "game:/glow.shader");
    CHECK(first.build->sourceHash != 0);
    CHECK(first.build->shader->parameters.size() == 1);
    CHECK(loader.pending() == 0);

    // The same source again comes from the import cache; an edit is compiled again.
    loader.request("game:/glow.shader");
    loader.waitIdle();
    const ShaderLoader::State second = loader.state("game:/glow.shader");
    CHECK(second.completed == 2);
    CHECK(second.build->sourceHash == first.build->sourceHash);
    CHECK(*second.build->shader == *first.build->shader);

    writeText(project.directory.path / "game" / "glow.shader", std::string(kShader) + "\n// edited\n");
    loader.request("game:/glow.shader");
    loader.waitIdle();
    const ShaderLoader::State third = loader.state("game:/glow.shader");
    CHECK(third.completed == 3);
    CHECK(third.build->sourceHash != first.build->sourceHash);
    CHECK(first.build->succeeded()); // builds already handed out never change
}

TEST_CASE("a shader that does not compile reports its errors with file, line and column")
{
    LoaderProject project;
    writeText(
        project.directory.path / "game" / "broken.shader",
        "struct Parameters\n{\n};\n\nSurfaceOutput surface(SurfaceInput input)\n{\n    return nothing;\n}\n");
    ShaderLoader loader(project.files, project.compiling());
    loader.request("game:/broken.shader");
    loader.waitIdle();
    const ShaderLoader::State state = loader.state("game:/broken.shader");
    REQUIRE(state.build != nullptr);
    CHECK_FALSE(state.build->succeeded());
    CHECK(state.build->error.find("does not compile") != std::string::npos);
    bool located = false;
    for (const ShaderDiagnostic& diagnostic : state.build->diagnostics)
    {
        located = located ||
                  (diagnostic.severity == ShaderDiagnosticSeverity::Error &&
                   diagnostic.file == "game:/broken.shader" && diagnostic.line == 7 && diagnostic.column > 0);
    }
    CHECK(located);

    // A missing file is a build that failed, not a crash.
    loader.request("game:/missing.shader");
    loader.waitIdle();
    const ShaderLoader::State missing = loader.state("game:/missing.shader");
    REQUIRE(missing.build != nullptr);
    CHECK_FALSE(missing.build->succeeded());
    CHECK(missing.build->sourceHash == 0);
    CHECK(missing.build->error.starts_with("game:/missing.shader: "));
}

TEST_CASE("many requests for the same paths all end and leave every path built")
{
    LoaderProject project;
    writeText(project.directory.path / "game" / "a.shader", kShader);
    writeText(project.directory.path / "game" / "b.shader", kShader);
    ShaderLoader loader(project.files, project.compiling());
    for (int i = 0; i < 50; ++i)
    {
        loader.request("game:/a.shader");
        loader.request("game:/b.shader");
    }
    loader.waitIdle();
    CHECK(loader.pending() == 0);
    for (const char* path : {"game:/a.shader", "game:/b.shader"})
    {
        const ShaderLoader::State state = loader.state(path);
        CHECK_FALSE(state.building);
        CHECK(state.completed >= 1);
        REQUIRE(state.build != nullptr);
        CHECK(state.build->succeeded());
    }
}

TEST_CASE("a loader without a compiler loads cooked shaders and fails on the others")
{
    LoaderProject project;
    writeText(project.directory.path / "game" / "glow.shader", kShader);
    writeText(project.directory.path / "game" / "raw.shader", kShader);
    {
        ShaderLoader compiling(project.files, project.compiling());
        compiling.request("game:/glow.shader");
        compiling.waitIdle();
        REQUIRE(compiling.state("game:/glow.shader").build->succeeded());
    }
    ShaderLoader cooked(project.files, ShaderLoaderDesc{});
    CHECK_FALSE(cooked.canCompile());
    cooked.request("game:/glow.shader");
    cooked.request("game:/raw.shader");
    cooked.waitIdle();
    CHECK(cooked.state("game:/glow.shader").build->succeeded());
    const auto raw = cooked.state("game:/raw.shader").build;
    REQUIRE(raw != nullptr);
    CHECK_FALSE(raw->succeeded());
    CHECK(raw->error.find("no shader compiler") != std::string::npos);

    // A compiler that cannot be made fails every build with the reason.
    ShaderLoaderDesc broken = project.compiling();
    broken.compiler->slangc = project.directory.path / "no-slangc";
    ShaderLoader unavailable(project.files, broken);
    unavailable.request("game:/glow.shader");
    unavailable.waitIdle();
    const auto build = unavailable.state("game:/glow.shader").build;
    REQUIRE(build != nullptr);
    CHECK_FALSE(build->succeeded());
    CHECK(build->error.find("the shader compiler is not available") != std::string::npos);
}
