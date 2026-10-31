// Project modules built by the editor (ADR 0043): the diagnostics parser, the toolchain description, and
// module.create, module.build and module.reload with the stand-in for CMake (FakeCMake.cpp).

#include "EditorCoreTestSupport.hpp"

#include <illusion/editor/core/ExternalTools.hpp>
#include <illusion/editor/core/ModuleBuild.hpp>

#include <chrono>
#include <fstream>
#include <iterator>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

std::vector<BuildDiagnostic> parseLines(std::initializer_list<std::string_view> lines)
{
    BuildOutputParser parser;
    std::vector<BuildDiagnostic> out;
    for (const std::string_view line : lines)
    {
        parser.feed(line, out);
    }
    return out;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("GCC and Clang diagnostics are parsed with file, line, column, severity and warning flag")
{
    const std::vector<BuildDiagnostic> found = parseLines({
        "/home/a/game/modules/tetris/tetris.cpp:12:5: error: use of undeclared identifier 'boardd'",
        "C:/proj/modules/tetris/tetris.cpp:3:10: warning: unused variable 'x' [-Wunused-variable]",
        "In file included from /home/a/x.cpp:1:",
        "/home/a/x.hpp:7: note: declared here",
        "/home/a/x.cpp:2:1: fatal error: 'missing.hpp' file not found",
    });
    REQUIRE(found.size() == 4);
    CHECK(found[0].file == "/home/a/game/modules/tetris/tetris.cpp");
    CHECK(found[0].line == 12);
    CHECK(found[0].column == 5);
    CHECK(found[0].severity == DiagnosticSeverity::Error);
    CHECK(found[0].message == "use of undeclared identifier 'boardd'");
    CHECK(found[1].file == "C:/proj/modules/tetris/tetris.cpp");
    CHECK(found[1].severity == DiagnosticSeverity::Warning);
    CHECK(found[1].code == "-Wunused-variable");
    CHECK(found[1].message == "unused variable 'x'");
    CHECK(found[2].severity == DiagnosticSeverity::Note);
    CHECK(found[2].line == 7);
    CHECK(found[2].column == 0);
    CHECK(found[3].severity == DiagnosticSeverity::Error);
}

TEST_CASE("MSVC, clang-cl and linker diagnostics are parsed with their codes")
{
    const std::vector<BuildDiagnostic> found = parseLines({
        R"(C:\proj\modules\tetris\tetris.cpp(12,5): error C2065: 'boardd': undeclared identifier)",
        R"(C:\proj\modules\tetris\tetris.cpp(40): warning C4100: 'context': unreferenced parameter)",
        R"(C:\proj\modules\tetris\tetris.cpp(8,1): error: expected ';' after expression)",
        "tetris.cpp.obj : error LNK2019: unresolved external symbol foo referenced in function bar",
        "LINK : fatal error LNK1104: cannot open file 'tetris.dll'",
    });
    REQUIRE(found.size() == 5);
    CHECK(found[0].file == R"(C:\proj\modules\tetris\tetris.cpp)");
    CHECK(found[0].line == 12);
    CHECK(found[0].column == 5);
    CHECK(found[0].code == "C2065");
    CHECK(found[0].message == "'boardd': undeclared identifier");
    CHECK(found[1].severity == DiagnosticSeverity::Warning);
    CHECK(found[1].column == 0);
    CHECK(found[1].code == "C4100");
    CHECK(found[2].code.empty());
    CHECK(found[2].message == "expected ';' after expression");
    CHECK(found[3].file == "tetris.cpp.obj");
    CHECK(found[3].code == "LNK2019");
    CHECK(found[3].line == 0);
    CHECK(found[4].file == "LINK");
    CHECK(found[4].code == "LNK1104");
}

TEST_CASE("CMake errors collect their indented message lines")
{
    const std::vector<BuildDiagnostic> found = parseLines({
        "CMake Error at tetris/CMakeLists.txt:3 (illusion_add_module):",
        "  illusion_add_module(tetris): unknown USES entry 'scripting'",
        "  (no target illusion::scripting_api)",
        "",
        "CMake Warning (dev) at CMakeLists.txt:1:",
        "",
        "  No project() command is present.",
        "-- Configuring incomplete, errors occurred!",
        "CMake Error: CMAKE_CXX_COMPILER not set, after EnableLanguage",
    });
    REQUIRE(found.size() == 3);
    CHECK(found[0].file == "tetris/CMakeLists.txt");
    CHECK(found[0].line == 3);
    CHECK(found[0].message ==
          "illusion_add_module(tetris): unknown USES entry 'scripting' (no target illusion::scripting_api)");
    CHECK(found[1].severity == DiagnosticSeverity::Warning);
    CHECK(found[1].message == "No project() command is present.");
    CHECK(found[2].file.empty());
    CHECK(found[2].message == "CMAKE_CXX_COMPILER not set, after EnableLanguage");
}

TEST_CASE("ordinary build output is not mistaken for diagnostics")
{
    CHECK(parseLines({
                         "[3/10] Building CXX object tetris/CMakeFiles/tetris.dir/tetris.cpp.obj",
                         "FAILED: tetris/CMakeFiles/tetris.dir/tetris.cpp.obj",
                         "-- The CXX compiler identification is MSVC 19.51",
                         "ninja: build stopped: subcommand failed.",
                         "note: this is not a located diagnostic",
                         "a(b): error",
                         "",
                     })
              .empty());
    CHECK(parseLines({std::string(5000, 'x') + ":1:1: error: long"}).empty());
}

TEST_CASE("build progress is read from Ninja and Makefile lines")
{
    const std::optional<BuildProgress> ninja = parseBuildProgress("[3/10] Building CXX object");
    REQUIRE(ninja);
    CHECK(ninja->done == 3.0);
    CHECK(ninja->total == 10.0);
    const std::optional<BuildProgress> make = parseBuildProgress("[ 50%] Linking CXX shared module");
    REQUIRE(make);
    CHECK(make->done == 50.0);
    CHECK(make->total == 100.0);
    CHECK_FALSE(parseBuildProgress("[0/0] nothing"));
    CHECK_FALSE(parseBuildProgress("[x/2] nothing"));
    CHECK_FALSE(parseBuildProgress("[120%] too much"));
    CHECK_FALSE(parseBuildProgress("Building"));
}

TEST_CASE("toolchain descriptions are validated")
{
    constexpr std::string_view kValid =
        R"({"format":"illusion.sdk","version":1,"cmake":"/usr/bin/cmake","generator":"Ninja","config":"Debug",)"
        R"("package":"/b/sdk","modulePrefix":"lib","moduleSuffix":".so","arguments":["-DCMAKE_BUILD_TYPE=Debug"]})";
    const Expected<ModuleToolchain, std::string> toolchain = parseModuleToolchain(kValid);
    REQUIRE(toolchain.hasValue());
    CHECK(toolchain->generator == "Ninja");
    CHECK(toolchain->modulePrefix == "lib");
    CHECK(toolchain->arguments == std::vector<std::string>{"-DCMAKE_BUILD_TYPE=Debug"});

    CHECK_FALSE(parseModuleToolchain("{").hasValue());
    CHECK_FALSE(parseModuleToolchain(R"({"format":"illusion.project"})").hasValue());
    CHECK_FALSE(parseModuleToolchain(R"({"format":"illusion.sdk","version":2})").hasValue());
    CHECK_FALSE(parseModuleToolchain(R"({"format":"illusion.sdk","version":1,"cmake":3})").hasValue());
    std::string noArguments(kValid);
    noArguments.replace(noArguments.find("\"arguments\""), 9, "\"argumentz");
    CHECK_FALSE(parseModuleToolchain(noArguments).hasValue());
    std::string emptyCmake(kValid);
    emptyCmake.replace(emptyCmake.find("/usr/bin/cmake"), 14, "");
    CHECK_FALSE(parseModuleToolchain(emptyCmake).hasValue());
    CHECK_FALSE(readModuleToolchain("no/such/illusion-sdk.json").hasValue());
}

TEST_CASE("module.create writes a module from the template and undo removes it")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    const std::filesystem::path modules = f.directory.path / "project" / "modules";
    std::filesystem::remove(modules / "CMakeLists.txt");
    const JsonDocument created = ok(editor, "module.create", R"({"name":"tetris"})");
    CHECK(created.root().find("directory").asString() == "modules:/tetris");
    CHECK(created.root().find("files").size() == 3);
    CHECK(readText(modules / "CMakeLists.txt").find("illusion_project_modules()") != std::string::npos);
    CHECK(readText(modules / "tetris" / "CMakeLists.txt").find("illusion_add_module(tetris SHARED") !=
          std::string::npos);
    CHECK(readText(modules / "tetris" / "tetris.cpp").find("ILL_MODULE(tetris, kModule)") !=
          std::string::npos);

    CHECK(fails(editor, "module.create", R"({"name":"tetris"})").kind == ErrorKind::AlreadyExists);
    CHECK(fails(editor, "module.create", R"({"name":"Tetris"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "module.create", R"({"name":"9lives"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "module.create", R"({"name":""})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "module.create", R"({"name":"a-b"})").kind == ErrorKind::ValidationFailed);

    // A second module keeps the existing root file.
    CHECK(ok(editor, "module.create", R"({"name":"scores"})").root().find("files").size() == 2);
    ok(editor, "history.undo");
    ok(editor, "history.undo");
    CHECK_FALSE(std::filesystem::exists(modules / "tetris" / "tetris.cpp"));
    CHECK_FALSE(std::filesystem::exists(modules / "CMakeLists.txt"));
}

TEST_CASE("module.build configures once, builds and loads new modules, then hot-reloads them")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    const JsonDocument first = ok(editor, "module.build");
    CHECK(first.root().find("succeeded").asBool() == true);
    CHECK(first.root().find("configured").asBool() == true);
    CHECK(first.root().find("errors").asU64() == 0u);
    REQUIRE(first.root().find("libraries").size() == 1);
    REQUIRE(first.root().find("loaded").size() == 1);
    CHECK(first.root().find("loaded")[0].asString() == "game");
    CHECK(first.root().find("reloaded").size() == 0);
    CHECK(first.root().find("output").asString()->find("[2/2] Linking") != std::string_view::npos);

    // The configure step gets the SDK package, the output directory and the toolchain's arguments.
    const std::filesystem::path build = f.directory.path / "project" / ".illusion" / "build" / "modules";
    const std::string arguments = readText(build / "configure-args.txt");
    CHECK(arguments.find("-Dillusion_DIR=sdk") != std::string::npos);
    CHECK(arguments.find("-DILLUSION_MODULE_OUTPUT_DIRECTORY=") != std::string::npos);
    CHECK(arguments.find("-DCMAKE_BUILD_TYPE=Debug") != std::string::npos);
    CHECK(arguments.find("Ninja") != std::string::npos);

    const JsonDocument second = ok(editor, "module.build", R"({"modules":["game","game"]})");
    CHECK(second.root().find("configured").asBool() == false);
    CHECK(second.root().find("loaded").size() == 0);
    REQUIRE(second.root().find("reloaded").size() == 1);
    CHECK(second.root().find("reloaded")[0].asString() == "game");

    std::filesystem::remove(build / "configure-args.txt");
    const JsonDocument third = ok(editor, "module.build", R"({"reconfigure":true,"load":false})");
    CHECK(third.root().find("configured").asBool() == true);
    CHECK(third.root().find("reloaded").size() == 0);
    CHECK(std::filesystem::exists(build / "configure-args.txt"));
}

TEST_CASE("a failed compile returns its diagnostics with virtual paths and logs them")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    const std::filesystem::path game = f.directory.path / "project" / "modules" / "game";
    const std::string source = (game / "game.cpp").generic_string();
    std::ofstream(game / "ERRORS") << source << ":4:7: error: unknown type name 'Bord'\n"
                                   << source << ":9:1: warning: unused function 'f' [-Wunused-function]\n"
                                   << "/elsewhere/x.hpp:2:3: note: from here\n";
    const JsonDocument result = ok(editor, "module.build");
    CHECK(result.root().find("succeeded").asBool() == false);
    CHECK(result.root().find("errors").asU64() == 1u);
    CHECK(result.root().find("warnings").asU64() == 1u);
    CHECK(result.root().find("loaded").size() == 0);
    const JsonValue diagnostics = result.root().find("diagnostics");
    REQUIRE(diagnostics.size() == 3);
    CHECK(diagnostics[0].find("file").asString() == "modules:/game/game.cpp");
    CHECK(diagnostics[0].find("line").asU64() == 4u);
    CHECK(diagnostics[0].find("column").asU64() == 7u);
    CHECK(diagnostics[0].find("severity").asString() == "error");
    CHECK(diagnostics[1].find("code").asString() == "-Wunused-function");
    CHECK(diagnostics[2].find("file").asString() == "/elsewhere/x.hpp");
    CHECK(diagnostics[2].find("severity").asString() == "note");

    const JsonDocument log = ok(editor, "log.read", R"({"category":"Build"})");
    CHECK(log.root().dump().find("unknown type name 'Bord'") != std::string::npos);
}

TEST_CASE("a failed configure returns the CMake error")
{
    EditorFixture f;
    std::ofstream(f.directory.path / "project" / "modules" / "FAIL_CONFIGURE") << "";
    const JsonDocument result = ok(*f.editor, "module.build");
    CHECK(result.root().find("succeeded").asBool() == false);
    CHECK(result.root().find("configured").asBool() == true);
    const JsonValue diagnostics = result.root().find("diagnostics");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].find("file").asString() == "CMakeLists.txt");
    CHECK(diagnostics[0].find("line").asU64() == 6u);
    CHECK(diagnostics[0].find("message").asString()->find("provided by \"illusion\"") !=
          std::string_view::npos);
    CHECK(result.root().find("output").asString()->find("cmake exited with code 1") !=
          std::string_view::npos);
}

TEST_CASE("module.build checks the project, the module names and the SDK before it starts")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    CHECK(fails(editor, "module.build", R"({"modules":["missing"]})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "module.build", R"({"modules":["Bad Name"]})").kind == ErrorKind::ValidationFailed);

    std::filesystem::remove(f.directory.path / "project" / "modules" / "CMakeLists.txt");
    CHECK(fails(editor, "module.build", "{}").kind == ErrorKind::PreconditionFailed);

    EditorFixture broken(1, EditorContextOptions{.moduleSdk = f.directory.path / "no-sdk.json"});
    CHECK(fails(*broken.editor, "module.build", "{}").kind == ErrorKind::Unsupported);

    std::ofstream(f.directory.path / "missing-cmake.json")
        << R"({"format":"illusion.sdk","version":1,"cmake":"no/such/cmake","generator":"Ninja","config":"Debug",)"
        << R"("package":"sdk","modulePrefix":"","moduleSuffix":".dll","arguments":[]})";
    EditorFixture missing(1, EditorContextOptions{.moduleSdk = f.directory.path / "missing-cmake.json"});
    CHECK(fails(*missing.editor, "module.build", "{}").kind == ErrorKind::BuildFailed);

    EditorContext closed;
    CHECK(fails(closed, "module.build", "{}").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("module.reload reloads a named module or the changed ones")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "module.build");
    const JsonDocument named = ok(editor, "module.reload", R"({"name":"game"})");
    REQUIRE(named.root().find("reloaded").size() == 1);
    CHECK(named.root().find("reloaded")[0].asString() == "game");
    CHECK(ok(editor, "module.reload").root().find("reloaded").size() == 0);
    CHECK(ok(editor, "module.reload", R"({"name":"game","dryRun":true})").root().find("reloaded").size() ==
          0);
    CHECK(fails(editor, "module.reload", R"({"name":"nope"})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "module.reload", R"({"name":"fake"})").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("code editor command lines are split and their placeholders replaced")
{
    const auto arguments = [](std::string_view command) {
        Expected<std::vector<std::string>, std::string> parsed =
            codeEditorArguments(command, "C:/p/a b.cpp", 42, 0);
        REQUIRE_MESSAGE(parsed.hasValue(), (parsed ? std::string() : parsed.error()));
        return std::move(parsed).value();
    };
    CHECK(arguments("code -g {file}:{line}:{column}") ==
          std::vector<std::string>{"code", "-g", "C:/p/a b.cpp:42:1"});
    CHECK(arguments(R"("C:/Program Files/Editor/editor.exe"  --line {line})") ==
          std::vector<std::string>{"C:/Program Files/Editor/editor.exe", "--line", "42", "C:/p/a b.cpp"});
    CHECK(arguments(R"(subl "{file}:{line}" "")") == std::vector<std::string>{"subl", "C:/p/a b.cpp:42", ""});
    CHECK(arguments("vim") == std::vector<std::string>{"vim", "C:/p/a b.cpp"});
    CHECK_FALSE(codeEditorArguments("", "f", 1, 1).hasValue());
    CHECK_FALSE(codeEditorArguments("   ", "f", 1, 1).hasValue());
    CHECK_FALSE(codeEditorArguments(R"("" -g {file})", "f", 1, 1).hasValue());
    CHECK_FALSE(codeEditorArguments(R"("code -g {file})", "f", 1, 1).hasValue());

    const std::vector<std::string> fallback = defaultOpenArguments("x.cpp");
    REQUIRE(fallback.size() == 2);
    CHECK_FALSE(fallback[0].empty());
    CHECK(fallback[1] == "x.cpp");
}

TEST_CASE("editor.open_source starts the code editor of the preferences at the line")
{
    EditorFixture f;
    f.buildScene();
    EditorContext& editor = *f.editor;
    const std::filesystem::path opened = f.directory.path / "opened.txt";
    const std::filesystem::path scene =
        (f.directory.path / "project" / "assets" / "scenes" / "main.scene").make_preferred();

    const JsonDocument dry =
        ok(editor, "editor.open_source", R"({"path":"assets:/scenes/main.scene","line":7,"dryRun":true})");
    CHECK(dry.root().find("program").asString() == std::string_view(ILLUSION_FAKE_CMAKE));
    CHECK_FALSE(std::filesystem::exists(opened));

    const JsonDocument result =
        ok(editor, "editor.open_source", R"({"path":"assets:/scenes/main.scene","line":7,"column":3})");
    const JsonValue arguments = result.root().find("arguments");
    REQUIRE(arguments.size() == 3);
    CHECK(arguments[0].asString() == "--record");
    // The editor runs detached; wait until it has written what it was asked to open.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::string text;
    while (text.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline)
    {
        text = readText(opened);
    }
    const std::u8string native = scene.u8string();
    CHECK(text == std::string(native.begin(), native.end()) + ":7:3\n");

    // An absolute path works too.
    const std::string absolute = R"({"path":")" + scene.generic_string() + R"(","dryRun":true})";
    CHECK(
        ok(editor, "editor.open_source", absolute).root().find("arguments")[2].asString()->ends_with(":1:1"));
}

TEST_CASE("editor.open_source rejects bad paths and unusable code editor preferences")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    CHECK(fails(editor, "editor.open_source", R"({"path":""})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "editor.open_source", R"({"path":"relative/file.cpp"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "editor.open_source", R"({"path":"nomount:/x.cpp"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "editor.open_source", R"({"path":"modules:/game/missing.cpp"})").kind ==
          ErrorKind::NotFound);
    CHECK(fails(editor, "editor.open_source", R"({"path":"modules:/game"})").kind == ErrorKind::NotFound);

    Preferences preferences = editor.preferences();
    preferences.codeEditor = R"("unclosed {file})";
    REQUIRE(editor.setPreferences(preferences).hasValue());
    CHECK(fails(editor, "editor.open_source", R"({"path":"modules:/CMakeLists.txt"})").kind ==
          ErrorKind::PreconditionFailed);
    preferences.codeEditor = "illusion-no-such-editor {file}";
    REQUIRE(editor.setPreferences(preferences).hasValue());
    CHECK(fails(editor, "editor.open_source", R"({"path":"modules:/CMakeLists.txt"})").kind ==
          ErrorKind::NotFound);
    preferences.codeEditor = "no/such/editor {file}";
    REQUIRE(editor.setPreferences(preferences).hasValue());
    CHECK(fails(editor, "editor.open_source", R"({"path":"modules:/CMakeLists.txt"})").kind ==
          ErrorKind::IoError);
}
