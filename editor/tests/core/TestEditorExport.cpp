// Exporting a project as a game (ADR 0055): validation, the generated build, the archives, the exclude
// patterns, the report and cleanup, with the stand-in for CMake (FakeCMake.cpp) and this build's
// illusion_pack, and one real build against this build tree's SDK.

#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/AppIcon.hpp>
#include <illusion/editor/core/Export.hpp>
#include <illusion/editor/core/ExportTools.hpp>
#include <illusion/editor/core/ModuleBuild.hpp>
#include <illusion/platform/Archive.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

#if defined(_WIN32)
constexpr std::string_view kPlatformName = "windows";
#elif defined(__APPLE__)
constexpr std::string_view kPlatformName = "macos";
#else
constexpr std::string_view kPlatformName = "linux";
#endif

// The files an export of @p product installs with FakeCMake.cpp, relative to the output directory.
std::vector<std::string> gameFiles(const std::string& product, const std::string& executable)
{
#if defined(__APPLE__)
    static_cast<void>(executable);
    const std::string app = product + ".app/Contents/";
    return {app + "MacOS/" + product, app + "Resources/data/assets.illpak",
            app + "Resources/data/cache.illpak", app + "Resources/data/settings.illpak"};
#else
#if defined(_WIN32)
    const std::string file = executable + ".exe";
#else
    const std::string file = executable;
#endif
    return {product + "/data/assets.illpak", product + "/data/cache.illpak",
            product + "/data/settings.illpak", product + "/" + file};
#endif
}

// The executable of an exported game, relative to the output directory.
std::string executableFile(const std::string& product, const std::string& executable)
{
#if defined(__APPLE__)
    static_cast<void>(executable);
    return product + ".app/Contents/MacOS/" + product;
#elif defined(_WIN32)
    return product + "/" + executable + ".exe";
#else
    return product + "/" + executable;
#endif
}

// The game's directory in the output directory (the bundle on macOS).
std::string gameDirectoryName(const std::string& product)
{
#if defined(__APPLE__)
    return product + ".app";
#else
    return product;
#endif
}

// The data directory of an exported game, relative to its directory.
std::filesystem::path dataOf(const std::filesystem::path& game)
{
#if defined(__APPLE__)
    return game / "Contents" / "Resources" / "data";
#else
    return game / "data";
#endif
}

// Sets an environment variable of this process for the lifetime of the object.
class ScopedVariable
{
public:
    ScopedVariable(const char* name, const std::string& value)
        : m_name(name)
    {
        REQUIRE(set(value));
    }
    ~ScopedVariable() { static_cast<void>(set("")); }
    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
    [[nodiscard]] bool set(const std::string& value) const
    {
#if defined(_WIN32)
        return _putenv_s(m_name, value.c_str()) == 0;
#else
        return (value.empty() ? unsetenv(m_name) : setenv(m_name, value.c_str(), 1)) == 0;
#endif
    }

    const char* m_name;
};

std::filesystem::path testData()
{
    return utf8Path(ILLUSION_ENGINE_TEST_DATA);
}

void writeFile(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    if (in)
    {
        text << in.rdbuf();
    }
    return text.str();
}

ModuleToolchain fakeToolchain()
{
    return ModuleToolchain{.cmake = utf8Path(ILLUSION_FAKE_CMAKE),
                           .generator = "Ninja",
                           .config = "Debug",
                           .package = "sdk",
                           .player = true,
                           .pack = utf8Path(ILLUSION_PACK_EXECUTABLE)};
}

// A project with a startup scene, a module, a glTF model with an external buffer and an image, files the
// exclude patterns of its export settings leave out, and project settings.
struct ExportProject
{
    TempDirectory directory;
    std::filesystem::path root = directory.path / "Exported Project";
    std::filesystem::path output = directory.path / "out";

    explicit ExportProject(std::string_view exportSettings =
                               R"({"productName":"Space Blocks 2","exclude":["raw/**","**/*.psd","*.tmp"]})",
                           std::string_view module = "blocks")
    {
        writeFile(root / "project.illusion",
                  R"({"format":"illusion.project","name":"Blocks","version":"1.2.3","engineVersion":"",)"
                  R"("startupScene":"assets:/scenes/main.scene","modules":[]})");
        writeFile(root / "assets" / "scenes" / "main.scene",
                  R"({"format":"illusion.scene","version":2,"entities":[]})");
        std::filesystem::create_directories(root / "assets" / "models");
        std::filesystem::create_directories(root / "assets" / "textures");
        for (const char* file : {"two_nodes.gltf", "two_nodes.bin"})
        {
            std::filesystem::copy_file(testData() / "gltf" / "scene" / file,
                                       root / "assets" / "models" / file);
        }
        std::filesystem::copy_file(testData() / "gltf" / "textures" / "checker.png",
                                   root / "assets" / "textures" / "checker.png");
        writeFile(root / "assets" / "raw" / "notes.txt", "notes");
        writeFile(root / "assets" / "raw" / "deep" / "x.psd", "layers");
        writeFile(root / "assets" / "textures" / "big.psd", "layers");
        writeFile(root / "assets" / "a.tmp", "temporary");
        writeFile(root / "assets" / "textures" / "b.tmp", "kept: * does not cross directories");
        writeFile(root / "settings" / "game.json", "{}");
        if (!exportSettings.empty())
        {
            writeFile(
                root / "settings" / "export.json",
                R"({"format":"illusion.settings","type":"illusion.ExportSettings","version":1,"value":)" +
                    std::string(exportSettings) + "}");
        }
        if (!module.empty())
        {
            const std::string name(module);
            writeFile(root / "modules" / name / "CMakeLists.txt",
                      "illusion_add_module(" + name + " SHARED SOURCES " + name + ".cpp USES runtime)\n");
            writeFile(root / "modules" / name / (name + ".cpp"), "// a module\n");
        }
    }

    [[nodiscard]] ExportOptions options(ModuleToolchain toolchain = fakeToolchain()) const
    {
        return ExportOptions{
            .toolchain = std::move(toolchain), .projectRoot = root, .outputDirectory = output};
    }
};

std::unique_ptr<ProjectExport> start(const ExportOptions& options)
{
    Expected<std::unique_ptr<ProjectExport>, std::string> started = ProjectExport::start(options);
    REQUIRE_MESSAGE(started.hasValue(), (started ? std::string() : started.error()));
    return std::move(*started);
}

void finish(ProjectExport& exported)
{
    while (!exported.update())
    {
    }
}

std::set<std::string> archiveEntries(const std::filesystem::path& path)
{
    std::string error;
    const std::unique_ptr<ArchiveReader> archive = ArchiveReader::open(path, &error);
    REQUIRE_MESSAGE(archive != nullptr, error);
    std::set<std::string> entries;
    for (const ArchiveEntry& entry : archive->entries())
    {
        entries.insert(entry.path);
    }
    return entries;
}

std::string startError(const ExportOptions& options)
{
    Expected<std::unique_ptr<ProjectExport>, std::string> started = ProjectExport::start(options);
    REQUIRE_FALSE(started.hasValue());
    return started.error();
}

} // namespace

TEST_CASE("an export installs the game with its three archives and reports every file")
{
    const ExportProject project;
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    CHECK(exported->stage() == ProjectExport::Stage::Generate);
    CHECK(exported->progress() == 0.0);
    finish(*exported);
    INFO(exported->outputTail());
    REQUIRE(exported->succeeded());
    CHECK(exported->error().empty());
    CHECK(exported->stage() == ProjectExport::Stage::Done);
    CHECK(exported->progress() == 1.0);

    const ExportReport& report = exported->report();
    const std::filesystem::path game = project.output / utf8Path(gameDirectoryName("Space Blocks 2"));
    CHECK(report.directory == game);
    CHECK(report.configuration == "Debug");
    CHECK(report.platform == kPlatformName);
    CHECK(report.package.empty());
    CHECK_FALSE(report.signedBinaries);
    CHECK_FALSE(report.notarized);
    const std::vector<std::string> expected = gameFiles("Space Blocks 2", "space_blocks_2");
    REQUIRE(report.files.size() == expected.size());
    for (usize i = 0; i < expected.size(); ++i)
    {
        CHECK(report.files[i].path == expected[i]);
    }
    u64 total = 0;
    for (const ExportedFile& file : report.files)
    {
        CHECK(file.bytes == std::filesystem::file_size(project.output / utf8Path(file.path)));
        total += file.bytes;
    }
    CHECK(report.totalBytes == total);
    CHECK_FALSE(std::filesystem::exists(project.output / "Space Blocks 2.partial"));

    const std::set<std::string> assets = archiveEntries(dataOf(game) / "assets.illpak");
    CHECK(assets.contains("scenes/main.scene"));
    CHECK(assets.contains("textures/checker.png"));
    CHECK(assets.contains("textures/b.tmp"));
    // The cooked model replaces the glTF file and the buffer it names.
    CHECK_FALSE(assets.contains("models/two_nodes.gltf"));
    CHECK_FALSE(assets.contains("models/two_nodes.bin"));
    CHECK_FALSE(archiveEntries(dataOf(game) / "cache.illpak").empty());
}

TEST_CASE("exclude patterns leave files out of the assets archive and the report lists them")
{
    const ExportProject project;
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    REQUIRE(exported->succeeded());
    CHECK(exported->report().skipped == std::vector<std::string>{"assets:/a.tmp", "assets:/raw/deep/x.psd",
                                                                 "assets:/raw/notes.txt",
                                                                 "assets:/textures/big.psd"});
    const std::set<std::string> assets = archiveEntries(
        dataOf(project.output / utf8Path(gameDirectoryName("Space Blocks 2"))) / "assets.illpak");
    for (const char* left : {"a.tmp", "raw/notes.txt", "raw/deep/x.psd", "textures/big.psd"})
    {
        CAPTURE(left);
        CHECK_FALSE(assets.contains(left));
    }
}

TEST_CASE("excluded images and models are not cooked into the cache archive")
{
    const ExportProject project(R"({"productName":"Blocks","exclude":["models/**","textures/checker.png"]})");
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    INFO(exported->outputTail());
    REQUIRE(exported->succeeded());
    // The project's only cookable assets are excluded, so nothing of them is in the cache.
    const std::filesystem::path data = dataOf(project.output / utf8Path(gameDirectoryName("Blocks")));
    CHECK(archiveEntries(data / "cache.illpak").empty());
    CHECK(exported->report().skipped == std::vector<std::string>{"assets:/models/two_nodes.bin",
                                                                 "assets:/models/two_nodes.gltf",
                                                                 "assets:/textures/checker.png"});
    const std::set<std::string> assets = archiveEntries(data / "assets.illpak");
    CHECK_FALSE(assets.contains("textures/checker.png"));
    CHECK(assets.contains("textures/b.tmp"));
}

TEST_CASE("a model whose texture is excluded fails the cook with a message that names the texture")
{
    const ExportProject project(R"({"productName":"Blocks","exclude":["textures/checker.png"]})");
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    CHECK_FALSE(exported->succeeded());
    CHECK(exported->error() == "cooking the assets failed (exit code 1)");
    CHECK(exported->outputTail().find("checker.png") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(project.output));
}

TEST_CASE("the settings archive holds the project settings but not the export settings")
{
    const ExportProject project;
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    REQUIRE(exported->succeeded());
    const std::set<std::string> settings = archiveEntries(
        dataOf(project.output / utf8Path(gameDirectoryName("Space Blocks 2"))) / "settings.illpak");
    CHECK(settings == std::set<std::string>{"game.json"});
}

TEST_CASE("the generated build links the project's modules statically into the player")
{
    const ExportProject project;
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    REQUIRE(exported->succeeded());
    const std::filesystem::path generated = project.root / ".illusion" / "export";
    REQUIRE(std::filesystem::is_directory(generated));
    const std::filesystem::path platform = std::filesystem::directory_iterator(generated)->path();
    const std::string text = readText(platform / "CMakeLists.txt");
    const std::string modules = (project.root / "modules" / "blocks").generic_string();
    for (const std::string& line : {
             std::string("find_package(illusion REQUIRED CONFIG COMPONENTS player)"),
             std::string("set(ILLUSION_STATIC_MODULES ON)"),
             "add_subdirectory([=[" + modules + "]=] modules/blocks)",
             std::string("illusion_add_game(space_blocks_2_game"),
             std::string("    MODULES blocks"),
             std::string("    NAME [=[Space Blocks 2]=]"),
             std::string("    ORGANIZATION [=[Space Blocks 2]=]"),
             std::string("    VERSION [=[1.2.3]=]"),
             std::string("    STARTUP_SCENE [=[assets:/scenes/main.scene]=])"),
             std::string("set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON)"),
             std::string("set(ILLUSION_GAME_REQUIRE_VULKAN ON)"),
#if defined(__APPLE__)
             std::string(
                 "set_target_properties(space_blocks_2_game PROPERTIES OUTPUT_NAME [=[Space Blocks 2]=]"),
#else
             std::string(
                 "set_target_properties(space_blocks_2_game PROPERTIES OUTPUT_NAME [=[space_blocks_2]=]"),
#endif
             std::string("    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/game\")"),
             std::string(
                 "illusion_package_game(space_blocks_2_game DATA data/assets.illpak data/settings.illpak "
                 "data/cache.illpak)"),
         })
    {
        CAPTURE(line);
        CHECK(text.find(line + "\n") != std::string::npos);
    }
    // The configure step got the SDK's package and no module output directory (static modules have none).
    const std::string configure = readText(platform / "build" / "configure-args.txt");
    CHECK(configure.find("-Dillusion_DIR=sdk\n") != std::string::npos);
    CHECK(configure.find("ILLUSION_MODULE_OUTPUT_DIRECTORY") == std::string::npos);
}

TEST_CASE("validation collects every problem of the project in one message")
{
    ExportProject project(R"({"productName":"9 Lives","organization":"A/B","identifier":"nodots",)"
                          R"("exclude":["","C:\\x"]})");
    std::filesystem::remove(project.root / "assets" / "scenes" / "main.scene");
    writeFile(project.root / "modules" / "Bad-Name" / "CMakeLists.txt", "\n");
    writeFile(project.root / "project.illusion",
              R"({"format":"illusion.project","name":"Blocks","version":"1.0.0","engineVersion":"",)"
              R"("startupScene":"assets:/scenes/main.scene","modules":["ghost"]})");
    ModuleToolchain toolchain = fakeToolchain();
    toolchain.player = false;
    toolchain.pack = project.directory.path / "no-pack";
    ExportOptions options = project.options(std::move(toolchain));
    options.signing.signCommand = "no-such-signing-tool {file}";
    options.linuxRuntime = "sniper";
    const std::string error = startError(options);
    CHECK(error.starts_with("cannot export: "));
    for (const char* part : {
             "has no player component",
             "has no illusion_pack",
#if defined(_WIN32)
             "the sign command's program 'no-such-signing-tool' was not found",
#else
             "signCommand signs Windows exports",
#endif
             "(linuxRuntime) is not supported yet",
             "'9 Lives' must start with an ASCII letter",
             "the organization 'A/B' is not a valid directory name",
             "the identifier 'nodots' must be reverse-DNS",
             "exclude pattern 0",
             "exclude pattern 1",
             "the startup scene 'assets:/scenes/main.scene' does not exist",
             "module 'Bad-Name' is not a module name",
             "module 'ghost' has no sources",
         })
    {
        CAPTURE(part);
        CHECK(error.find(part) != std::string::npos);
    }
}

TEST_CASE("validation rejects product names that make no directory and outputs inside the project's content")
{
    ExportProject project(R"({"productName":"Bad:Name"})");
    CHECK(startError(project.options()).find("the product name 'Bad:Name' is not a valid directory name") !=
          std::string::npos);

    ExportProject excludedScene(R"({"exclude":["scenes/**"]})");
    CHECK(startError(excludedScene.options()).find("is excluded from the export") != std::string::npos);

    ExportProject inside(R"({})");
    ExportOptions options = inside.options();
    options.outputDirectory = inside.root / "assets" / "out";
    CHECK(startError(options).find("must not be inside the project's assets directory") != std::string::npos);
    options.outputDirectory = "relative/out";
    CHECK(startError(options).find("must be an absolute path") != std::string::npos);
}

TEST_CASE("an export without export settings uses the project's name")
{
    const ExportProject project("", "");
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    INFO(exported->outputTail());
    REQUIRE(exported->succeeded());
    CHECK(std::filesystem::is_regular_file(project.output / utf8Path(executableFile("Blocks", "blocks"))));
    CHECK(exported->report().skipped.empty());
}

TEST_CASE("a destroyed export leaves no game directory and no partial install")
{
    const ExportProject project;
    {
        const std::unique_ptr<ProjectExport> exported = start(project.options());
        while (exported->stage() != ProjectExport::Stage::Install && !exported->update())
        {
        }
        CHECK(exported->stage() == ProjectExport::Stage::Install);
        CHECK(exported->progress() > 0.5);
    }
    CHECK_FALSE(std::filesystem::exists(project.output / utf8Path(gameDirectoryName("Space Blocks 2"))));
    CHECK_FALSE(std::filesystem::exists(project.output / "Space Blocks 2.partial"));
}

TEST_CASE("a failed cook ends the export without a game directory")
{
    const ExportProject project;
    writeFile(project.root / "assets" / "textures" / "broken.png", "not a png");
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    ProjectExport::Stage last = exported->stage();
    while (!exported->update())
    {
        last = exported->stage();
    }
    CHECK_FALSE(exported->succeeded());
    CHECK(last == ProjectExport::Stage::Cook);
    CHECK(exported->error() == "cooking the assets failed (exit code 1)");
    CHECK(exported->outputTail().find("broken.png") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(project.output));
}

TEST_CASE("toolchain descriptions name illusion_pack relative to their file")
{
    TempDirectory directory;
    const std::filesystem::path file =
        directory.path / "lib" / "cmake" / "illusion" / "Debug" / "illusion-sdk.json";
    writeFile(file, R"({"format": "illusion.sdk", "version": 2, "cmake": "cmake", "generator": "Ninja",)"
                    R"( "config": "Debug", "package": "..", "player": true,)"
                    R"( "pack": "../../../../bin/Debug/illusion_pack", "modulePrefix": "",)"
                    R"( "moduleSuffix": ".dll", "arguments": []})");
    const Expected<ModuleToolchain, std::string> toolchain = readModuleToolchain(file);
    REQUIRE(toolchain.hasValue());
    CHECK(toolchain->pack == (directory.path / "bin" / "Debug" / "illusion_pack").lexically_normal());
    CHECK(parseModuleToolchain(R"({"format": "illusion.sdk", "version": 2, "cmake": "c", "generator": "g",)"
                               R"( "config": "Debug", "package": "p", "player": true, "pack": 3,)"
                               R"( "modulePrefix": "", "moduleSuffix": ".so", "arguments": []})")
              .error() == "\"pack\" must be a string");
    CHECK(exportStageName(ProjectExport::Stage::Cook) == "cook");
    CHECK(exportPackageName(ExportPackage::Archive) == "archive");
}

TEST_CASE("export settings are project settings in settings:/export.json")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const SettingsType* type = editor.settings().find("illusion.ExportSettings", SettingsScope::Project);
    REQUIRE(type != nullptr);
    CHECK(type->file == "settings:/export.json");
    ok(editor, "project.settings_set",
       R"({"type":"illusion.ExportSettings","value":{"productName":"Blocks","exclude":["raw/**"]}})");
    const std::string text = readText(fixture.directory.path / "project" / "settings" / "export.json");
    CHECK(text.find("\"productName\": \"Blocks\"") != std::string::npos);
    CHECK(text.find("raw/**") != std::string::npos);
}

TEST_CASE("export.build validates its params and names the configurations the SDK has")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const CommandError missing = fails(editor, "export.build", R"({"configuration":"Shipping"})");
    CHECK(missing.kind == ErrorKind::NotFound);
    CHECK(missing.message.find("no configuration Shipping") != std::string::npos);
    CHECK(missing.message.find("available: Debug") != std::string::npos);
    CHECK(fails(editor, "export.build", R"({"configuration":"../x"})").errors.at(0).pointer ==
          "/configuration");
    CHECK(fails(editor, "export.build", R"({"package":"zip"})").errors.at(0).pointer == "/package");
    CHECK(fails(editor, "export.build", R"({"signCommand":"signtool\n{file}"})").errors.at(0).pointer ==
          "/signCommand");
    CHECK(fails(editor, "export.build", R"({"notaryProfile":"a\tb"})").errors.at(0).pointer ==
          "/notaryProfile");
    CHECK(fails(editor, "export.build", R"({"linuxRuntime":"sniper"})").errors.at(0).pointer ==
          "/linuxRuntime");
    CHECK(fails(editor, "export.build", R"({"output":""})").errors.at(0).pointer == "/output");
    const CommandError inside = fails(editor, "export.build", R"({"output":"assets/out"})");
    CHECK(inside.kind == ErrorKind::PreconditionFailed);
    CHECK(inside.message.find("inside the project's assets directory") != std::string::npos);

    EditorFixture noSdk(1, EditorContextOptions{.moduleSdk = fixture.directory.path / "missing.json"});
    CHECK(fails(*noSdk.editor, "export.build", "{}").kind == ErrorKind::Unsupported);
}

TEST_CASE("export.build reports a failed step in its result and leaves no game behind")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets(); // assets:/textures/red.png is not an image: cooking fails
    const std::string output = (fixture.directory.path / "exported").generic_string();
    const JsonDocument result = ok(*fixture.editor, "export.build", R"({"output":")" + output + R"("})");
    CHECK(result.root().find("succeeded").asBool() == false);
    CHECK(result.root().find("stage").asString() == "cook");
    CHECK(result.root().find("error").asString() == "cooking the assets failed (exit code 1)");
    CHECK(result.root().find("output").asString()->find("red.png") != std::string_view::npos);
    CHECK(result.root().find("files").size() == 0);
    CHECK_FALSE(std::filesystem::exists(fixture.directory.path / "exported"));
}

TEST_CASE("export.build exports the fixture project once its assets cook")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const std::string output = (fixture.directory.path / "exported").generic_string();
    const JsonDocument result = ok(editor, "export.build", R"({"output":")" + output + R"(","clean":true})");
    INFO(result.root().find("output").asString().value_or(""));
    REQUIRE(result.root().find("succeeded").asBool() == true);
    CHECK(result.root().find("stage").asString() == "done");
    CHECK(result.root().find("configuration").asString() == "Debug");
    CHECK(result.root().find("files").size() == gameFiles("Test", "test").size());
    // u64 values are decimal strings in JSON.
    CHECK(std::stoull(std::string(result.root().find("totalBytes").asString().value_or("0"))) > 0u);
    CHECK(std::filesystem::is_regular_file(fixture.directory.path / "exported" /
                                           utf8Path(executableFile("Test", "test"))));
}

namespace
{

#if defined(_WIN32) || defined(__APPLE__)
constexpr std::string_view kArchiveExtension = ".zip";
constexpr std::string_view kArchiveGenerator = "ZIP";
#else
constexpr std::string_view kArchiveExtension = ".tar.gz";
constexpr std::string_view kArchiveGenerator = "TGZ";
#endif

std::filesystem::path packageDirectory(const ExportProject& project)
{
    return project.root / ".illusion" / "export" / std::string(kPlatformName) / "package";
}

#if defined(_WIN32)
// What FakeCMake.cpp --sign appends to the file it signs.
constexpr std::string_view kSignature = "ILLUSION-TEST-SIGNATURE";

usize count(std::string_view text, std::string_view part)
{
    usize found = 0;
    for (usize at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size()))
    {
        ++found;
    }
    return found;
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> result;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
    {
        result.push_back(line);
    }
    return result;
}

// FakeCMake.cpp as a recording sign command.
std::string fakeSignCommand(const std::filesystem::path& log, std::string_view file = "{file}")
{
    return "\"" + std::string(ILLUSION_FAKE_CMAKE) + "\" --sign \"" + log.generic_string() + "\" " +
           std::string(file);
}
#endif

} // namespace

TEST_CASE("an archive export packages the installed game with CPack and reports the package")
{
    const ExportProject project;
    ExportOptions options = project.options();
    options.package = ExportPackage::Archive;
    const std::unique_ptr<ProjectExport> exported = start(options);
    finish(*exported);
    INFO(exported->outputTail());
    REQUIRE(exported->succeeded());
    const ExportReport& report = exported->report();
    const std::filesystem::path package =
        project.output /
        utf8Path("Space Blocks 2-1.2.3-" + std::string(kPlatformName) + std::string(kArchiveExtension));
    CHECK(report.package == package);
    REQUIRE(std::filesystem::is_regular_file(package));
    CHECK(readText(package).starts_with(std::string(kArchiveGenerator) + " package\n"));
    CHECK(report.files.size() == gameFiles("Space Blocks 2", "space_blocks_2").size());
    CHECK_FALSE(report.signedBinaries);

    const std::string arguments = readText(packageDirectory(project) / "cpack-args.txt");
    for (const std::string& part :
         {"-G\n" + std::string(kArchiveGenerator) + "\n", std::string("-C\nDebug\n"),
          std::string("CPackConfig.cmake\n"), std::string("-D\nCPACK_PACKAGE_FILE_NAME=Space Blocks 2\n")})
    {
        CAPTURE(part);
        CHECK(arguments.find(part) != std::string::npos);
    }
    // The package moved to the output directory.
    CHECK_FALSE(std::filesystem::exists(packageDirectory(project) /
                                        utf8Path("Space Blocks 2" + std::string(kArchiveExtension))));
}

TEST_CASE("a configure error, such as a macOS SDK machine without MoltenVK, fails the export without a game")
{
    const ExportProject project;
    // FakeCMake.cpp fails the configure step with a CMake error when the source has FAIL_CONFIGURE.
    writeFile(project.root / ".illusion" / "export" / std::string(kPlatformName) / "FAIL_CONFIGURE", "");
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    ProjectExport::Stage last = exported->stage();
    while (!exported->update())
    {
        last = exported->stage();
    }
    CHECK_FALSE(exported->succeeded());
    CHECK(last == ProjectExport::Stage::Configure);
    CHECK(exported->error() == "the build of the game failed");
    bool reported = false;
    for (const BuildDiagnostic& diagnostic : exported->diagnostics())
    {
        reported = reported || (diagnostic.severity == DiagnosticSeverity::Error &&
                                diagnostic.file.ends_with("CMakeLists.txt") && diagnostic.line == 6);
    }
    CHECK(reported);
    CHECK_FALSE(std::filesystem::exists(project.output / utf8Path(gameDirectoryName("Space Blocks 2"))));
    CHECK_FALSE(std::filesystem::exists(project.output / "Space Blocks 2.partial"));
}

TEST_CASE(
    "every platform's package formats, sign command and codesign arguments are built the same everywhere")
{
    using namespace illusion::editor::detail;
    CHECK(exportPlatformName(ExportPlatform::MacOS) == "macos");
    CHECK(exportPlatformName(kExportPlatform) == kPlatformName);
    CHECK(exportPackageFormat(ExportPackage::Installer, ExportPlatform::Windows).generator == "NSIS");
    CHECK(exportPackageFormat(ExportPackage::Installer, ExportPlatform::Windows).suffix == "-setup.exe");
    CHECK(exportPackageFormat(ExportPackage::Archive, ExportPlatform::Windows).extension == ".zip");
    CHECK(exportPackageFormat(ExportPackage::Installer, ExportPlatform::MacOS).generator == "DragNDrop");
    CHECK(exportPackageFormat(ExportPackage::Archive, ExportPlatform::MacOS).generator == "ZIP");
    CHECK(exportPackageFormat(ExportPackage::Installer, ExportPlatform::Linux).extension == ".tar.gz");
    CHECK(exportPackageFormat(ExportPackage::Archive, ExportPlatform::Linux).generator == "TGZ");

    const std::vector<std::string> parts =
        splitCommandLine("\"C:\\Program Files\\signtool.exe\"  sign /p \"pass word\"\t{file} \"\"");
    CHECK(parts == std::vector<std::string>{R"(C:\Program Files\signtool.exe)", "sign", "/p", "pass word",
                                            "{file}", ""});
    CHECK(splitCommandLine("  ").empty());
    CHECK(substituteFile({"/f", "{file}", "x{file}y{file}"}, "C:\\a b.exe") ==
          std::vector<std::string>{"/f", "C:\\a b.exe", "xC:\\a b.exey" + std::string("C:\\a b.exe")});

    CHECK(codesignArguments("-", "/out/Game.app", "/e.plist") ==
          std::vector<std::string>{"--force", "--sign", "-", "--options", "runtime", "--entitlements",
                                   "/e.plist", "/out/Game.app"});
    CHECK(codesignArguments("Developer ID Application: X", "/lib.dylib", "") ==
          std::vector<std::string>{"--force", "--sign", "Developer ID Application: X", "--options", "runtime",
                                   "--timestamp", "/lib.dylib"});
    CHECK(notarizeArguments("/out/Game.dmg", "profile") ==
          std::vector<std::string>{"notarytool", "submit", "/out/Game.dmg", "--wait", "--keychain-profile",
                                   "profile"});
    CHECK(stapleArguments("/out/Game.dmg") == std::vector<std::string>{"stapler", "staple", "/out/Game.dmg"});

    const std::string adHoc = codesignEntitlements(true);
    const std::string team = codesignEntitlements(false);
    CHECK(adHoc.find("com.apple.security.cs.disable-library-validation") != std::string::npos);
    CHECK(team.find("<key>") == std::string::npos);
    CHECK(team.starts_with("<?xml"));
    CHECK(team.ends_with("</dict>\n</plist>\n"));
}

TEST_CASE("validation checks the signing settings against the platform")
{
    const ExportProject project;
    ExportOptions options = project.options();
    options.signing.signCommand = "signtool sign";
    options.signing.codesignIdentity = "-";
    options.signing.notaryProfile = "profile";
    const std::string error = startError(options);
#if defined(_WIN32)
    CHECK(error.find("the sign command must name a program and {file}") != std::string::npos);
    CHECK(error.find("codesignIdentity and notaryProfile sign macOS exports") != std::string::npos);
#elif defined(__APPLE__)
    CHECK(error.find("signCommand signs Windows exports") != std::string::npos);
    CHECK(error.find("notarization needs a Developer ID codesignIdentity") != std::string::npos);
    CHECK(error.find("export with package 'installer'") != std::string::npos);
#else
    CHECK(error.find("signCommand signs Windows exports") != std::string::npos);
    CHECK(error.find("codesignIdentity and notaryProfile sign macOS exports") != std::string::npos);
#endif
    options.signing =
        ExportSigning{.signCommand = "a\nb {file}", .codesignIdentity = {}, .notaryProfile = {}};
    CHECK(startError(options).find("single lines of text") != std::string::npos);
}

TEST_CASE("validation checks the icon of the export settings")
{
    for (const auto& [icon, message] : {
             std::pair{"assets:/textures/checker.png",
                       "must be a square PNG of at least 256 pixels (it is 2x2)"},
             std::pair{"assets:/missing.png", "'assets:/missing.png' does not exist"},
             std::pair{"assets:/scenes/main.scene", "must be a PNG file under assets:/"},
             std::pair{"assets:/../outside.png", "is not a valid asset path"},
         })
    {
        CAPTURE(icon);
        const ExportProject project(R"({"icon":")" + std::string(icon) + R"("})");
        const std::string error = startError(project.options());
        INFO(error);
        CHECK(error.find(message) != std::string::npos);
    }
}

TEST_CASE("the icon, identifier and copyright of the export settings reach the generated build")
{
    const ExportProject project(R"({"productName":"Space Blocks 2","identifier":"com.example.blocks",)"
                                R"("copyright":"(c) 2026 Example","icon":"assets:/icons/game.png"})");
    const std::filesystem::path icon = project.root / "assets" / "icons" / "game.png";
    std::filesystem::create_directories(icon.parent_path());
    const std::vector<std::byte> png = encodePng(makeEngineIcon(256));
    std::ofstream(icon, std::ios::binary)
        .write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    const std::unique_ptr<ProjectExport> exported = start(project.options());
    finish(*exported);
    INFO(exported->outputTail());
    REQUIRE(exported->succeeded());
    const std::string text =
        readText(project.root / ".illusion" / "export" / std::string(kPlatformName) / "CMakeLists.txt");
    for (const std::string& line : {"    ICON [=[" + icon.generic_string() + "]=]",
                                    std::string("    IDENTIFIER [=[com.example.blocks]=]"),
                                    std::string("    COPYRIGHT [=[(c) 2026 Example]=])")})
    {
        CAPTURE(line);
        CHECK(text.find(line + "\n") != std::string::npos);
    }
}

#if defined(_WIN32)
TEST_CASE("a sign command signs the executable in the build tree before the install and the archive copy it")
{
    const ExportProject project;
    const std::filesystem::path log = project.directory.path / "signed.txt";
    ExportOptions options = project.options();
    options.package = ExportPackage::Archive;
    options.signing.signCommand = fakeSignCommand(log);
    const std::unique_ptr<ProjectExport> exported = start(options);
    finish(*exported);
    const std::string tail = exported->outputTail();
    INFO(tail);
    REQUIRE(exported->succeeded());
    const ExportReport& report = exported->report();
    CHECK(report.signedBinaries);
    const std::vector<std::string> signedFiles = lines(readText(log));
    REQUIRE(signedFiles.size() == 1);
    CHECK(signedFiles[0].ends_with("/.illusion/export/windows/build/game/space_blocks_2.exe"));
    CHECK(count(readText(project.output / "Space Blocks 2" / "space_blocks_2.exe"), kSignature) == 1);
    CHECK(count(readText(report.package), kSignature) == 1);
    // The sign command's arguments may hold a password: the output names only the file.
    CHECK(tail.find("(signing space_blocks_2.exe)") != std::string::npos);
    CHECK(tail.find("--sign") == std::string::npos);
    CHECK(tail.find("signed.txt") == std::string::npos);
}

TEST_CASE("an installer export signs the installer after packaging, or names makensis when NSIS is missing")
{
    const ExportProject project;
    const std::filesystem::path log = project.directory.path / "signed.txt";
    ExportOptions options = project.options();
    options.package = ExportPackage::Installer;
    options.signing.signCommand = fakeSignCommand(log);
    Expected<std::unique_ptr<ProjectExport>, std::string> started = ProjectExport::start(options);
    if (!started)
    {
        CHECK(started.error().find("the installer needs makensis (NSIS") != std::string::npos);
        return;
    }
    finish(**started);
    INFO((*started)->outputTail());
    REQUIRE((*started)->succeeded());
    const std::filesystem::path package = project.output / "Space Blocks 2-1.2.3-windows-setup.exe";
    CHECK((*started)->report().package == package);
    const std::vector<std::string> signedFiles = lines(readText(log));
    REQUIRE(signedFiles.size() == 2);
    CHECK(signedFiles[0].ends_with("/build/game/space_blocks_2.exe"));
    CHECK(signedFiles[1].ends_with("/out/Space Blocks 2-1.2.3-windows-setup.exe"));
    // The fake installer holds the signed executable and was signed itself.
    CHECK(count(readText(package), kSignature) == 2);
    const std::string arguments = readText(packageDirectory(project) / "cpack-args.txt");
    CHECK(arguments.find("-G\nNSIS\n") != std::string::npos);
    CHECK(arguments.find("-D\nCPACK_NSIS_EXECUTABLE=") != std::string::npos);
}

TEST_CASE("a failing sign command fails the export with the tool's output and leaves no game")
{
    const ExportProject project;
    ExportOptions options = project.options();
    options.signing.signCommand = fakeSignCommand(project.directory.path / "signed.txt", "{file}.missing");
    const std::unique_ptr<ProjectExport> exported = start(options);
    ProjectExport::Stage last = exported->stage();
    while (!exported->update())
    {
        last = exported->stage();
    }
    CHECK_FALSE(exported->succeeded());
    CHECK(last == ProjectExport::Stage::Build);
    CHECK(exported->error().starts_with("signing space_blocks_2.exe failed (exit code 1):\n"
                                        "SignTool Error: File not found:"));
    CHECK_FALSE(exported->report().signedBinaries);
    CHECK_FALSE(std::filesystem::exists(project.output / "Space Blocks 2"));
    CHECK_FALSE(std::filesystem::exists(project.output / "Space Blocks 2.partial"));
}

TEST_CASE("export.build signs with ILLUSION_SIGN_COMMAND unless signCommand turns signing off")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const std::filesystem::path log = fixture.directory.path / "signed.txt";
    const ScopedVariable variable("ILLUSION_SIGN_COMMAND", fakeSignCommand(log));
    const std::string output = (fixture.directory.path / "exported").generic_string();
    const JsonDocument result =
        ok(editor, "export.build", R"({"output":")" + output + R"(","package":"archive"})");
    INFO(result.root().find("output").asString().value_or(""));
    REQUIRE(result.root().find("succeeded").asBool() == true);
    CHECK(result.root().find("signedBinaries").asBool() == true);
    CHECK(std::string(result.root().find("package").asString().value_or("")).ends_with("-windows.zip"));
    CHECK(lines(readText(log)).size() == 1);

    const JsonDocument unsignedResult =
        ok(editor, "export.build", R"({"output":")" + output + R"(","signCommand":""})");
    REQUIRE(unsignedResult.root().find("succeeded").asBool() == true);
    CHECK(unsignedResult.root().find("signedBinaries").asBool() == false);
    CHECK(unsignedResult.root().find("package").asString() == "");
}
#else
TEST_CASE("export.build reads ILLUSION_SIGN_COMMAND only on Windows")
{
    EditorFixture fixture;
    const ScopedVariable variable("ILLUSION_SIGN_COMMAND", "signtool sign {file}");
    const std::string output = (fixture.directory.path / "exported").generic_string();
    const JsonDocument result = ok(*fixture.editor, "export.build", R"({"output":")" + output + R"("})");
    INFO(result.root().find("output").asString().value_or(""));
    REQUIRE(result.root().find("succeeded").asBool() == true);
    CHECK(result.root().find("signedBinaries").asBool() == false);
}
#endif

#if defined(ILLUSION_MODULE_SDK)
TEST_CASE("a compile error in a module fails the export with its file and line and no game directory")
{
    const ExportProject project(R"({"productName":"Broken"})", "");
    writeFile(project.root / "modules" / "broken" / "CMakeLists.txt",
              "illusion_add_module(broken SHARED SOURCES broken.cpp USES runtime)\n");
    writeFile(project.root / "modules" / "broken" / "broken.cpp",
              "#include <illusion/core/Module.hpp>\n\nint broken() { return undeclaredName; }\n");
    Expected<ModuleToolchain, std::string> toolchain =
        readModuleToolchain(std::filesystem::path(std::u8string_view(u8"" ILLUSION_MODULE_SDK)));
    REQUIRE(toolchain.hasValue());
    const std::unique_ptr<ProjectExport> exported = start(project.options(std::move(*toolchain)));
    finish(*exported);
    INFO(exported->outputTail());
    CHECK_FALSE(exported->succeeded());
    CHECK(exported->error() == "the build of the game failed");
    bool found = false;
    for (const BuildDiagnostic& diagnostic : exported->diagnostics())
    {
        if (diagnostic.severity == DiagnosticSeverity::Error && diagnostic.file.ends_with("broken.cpp"))
        {
            found = true;
            CHECK(diagnostic.line == 3);
        }
    }
    CHECK(found);
    CHECK_FALSE(std::filesystem::exists(project.output / "Broken"));
    CHECK_FALSE(std::filesystem::exists(project.output / "Broken.partial"));
}
#endif
