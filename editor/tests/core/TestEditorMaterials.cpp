// Material shaders in the editor (ADR 0063): material.compile and material.validate with locations for
// agents, parameters in material files, the Shader asset kind and the editor recompiling shaders that
// changed.

#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/ShaderLoader.hpp>

#include <fstream>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

constexpr std::string_view kGlow = R"(struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float4 tint;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(3)] int bands;
    [Default(1)] bool pulse;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    output.emission = p.tint.rgb * p.intensity;
    return output;
}
)";

void writeText(EditorContext& editor, std::string_view path, std::string_view text)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("path", doc.makeString(path));
    root.add("text", doc.makeString(text));
    doc.setRoot(root);
    std::string params;
    REQUIRE(doc.write(params, JsonWriteOptions{.pretty = false}).hasValue());
    ok(editor, "project.file_write", params);
}

} // namespace

TEST_CASE("material.compile builds a shader and lists what it declares")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    writeText(editor, "assets:/shaders/glow.shader", kGlow);
    const JsonDocument result = ok(editor, "material.compile", R"({"path":"assets:/shaders/glow.shader"})");
    CHECK(result.root().find("status").asString() == "ok");
    CHECK(result.root().find("shadingModel").asString() == "Default");
    const JsonValue parameters = result.root().find("parameters");
    REQUIRE(parameters.size() == 5);
    CHECK(parameters[0].find("name").asString() == "tint");
    CHECK(parameters[0].find("type").asString() == "float4");
    CHECK(parameters[0].find("color").asBool() == true);
    CHECK(parameters[1].find("minimum").asF64() == 0.0);
    CHECK(parameters[1].find("maximum").asF64() == 10.0);
    CHECK(parameters[1].find("defaultValue").asF64() == 2.5);
    CHECK(parameters[2].find("type").asString() == "int");
    CHECK(parameters[3].find("defaultValue").asBool() == true);
    CHECK(parameters[4].find("type").asString() == "texture");
    CHECK(parameters[4].find("usage").asString() == "mask");

    fails(editor, "material.compile", R"({"path":"assets:/shaders/missing.shader"})");
    fails(editor, "material.compile", R"({"path":"assets:/materials/red.material"})");
}

TEST_CASE("material.compile reports a shader that does not compile with file, line and column")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    writeText(
        editor, "assets:/shaders/broken.shader",
        "struct Parameters\n{\n};\n\nSurfaceOutput surface(SurfaceInput input)\n{\n    return nothing;\n}\n");
    const JsonDocument result = ok(editor, "material.compile", R"({"path":"assets:/shaders/broken.shader"})");
    CHECK(result.root().find("status").asString() == "failed");
    bool located = false;
    for (const JsonValue diagnostic : result.root().find("diagnostics").elements())
    {
        located = located || (diagnostic.find("severity").asString() == "error" &&
                              diagnostic.find("file").asString() == "assets:/shaders/broken.shader" &&
                              diagnostic.find("line").asI64() == 7 && diagnostic.find("column").asI64() > 0);
    }
    CHECK(located);
    const JsonDocument validated =
        ok(editor, "material.validate", R"({"path":"assets:/shaders/broken.shader"})");
    CHECK(validated.root().find("valid").asBool() == false);
    CHECK(validated.root().find("kind").asString() == "Shader");
    CHECK(std::string(validated.root().find("error").asString().value_or(""))
              .starts_with("assets:/shaders/broken.shader:7:"));
}

TEST_CASE("materials name a shader and its parameters, which material.set and material.validate check")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "material.create", R"({"path":"assets:/materials/red.material"})");
    writeText(editor, "assets:/shaders/glow.shader", kGlow);
    ok(editor, "material.compile", R"({"path":"assets:/shaders/glow.shader"})");

    ok(editor, "material.create",
       R"({"path":"assets:/materials/glow.material","value":{"shader":"assets:/shaders/glow.shader",)"
       R"("parameters":{"intensity":4,"tint":"#ff8000"}}})");
    const JsonDocument got = ok(editor, "material.get", R"({"path":"assets:/materials/glow.material"})");
    CHECK(got.root().find("shader").find("status").asString() == "ok");
    CHECK(got.root().find("shader").find("parameters").size() == 5);

    // Parameters merge by name; null removes one.
    ok(editor, "material.set",
       R"({"path":"assets:/materials/glow.material","value":{"parameters":{"bands":7}}})");
    ok(editor, "material.set",
       R"({"path":"assets:/materials/glow.material","value":{"parameters":{"intensity":null}}})");
    const JsonDocument merged = ok(editor, "material.get", R"({"path":"assets:/materials/glow.material"})");
    const JsonValue value = merged.root().find("value").find("parameters");
    CHECK(value.find("bands").asI64() == 7);
    CHECK(value.find("tint").isString());
    CHECK(value.find("intensity").isAbsent());

    const JsonDocument valid =
        ok(editor, "material.validate", R"({"path":"assets:/materials/glow.material"})");
    CHECK(valid.root().find("valid").asBool() == true);
    CHECK(valid.root().find("kind").asString() == "Material");

    // Unknown names and values of the wrong type are errors with their JSON Pointer.
    const CommandError unknown =
        fails(editor, "material.set",
              R"({"path":"assets:/materials/glow.material","value":{"parameters":{"glow":1}}})");
    REQUIRE_FALSE(unknown.errors.empty());
    CHECK(unknown.errors[0].pointer == "/value/parameters/glow");
    const CommandError wrong =
        fails(editor, "material.set",
              R"({"path":"assets:/materials/glow.material","value":{"parameters":{"bands":1.5}}})");
    REQUIRE_FALSE(wrong.errors.empty());
    CHECK(wrong.errors[0].pointer == "/value/parameters/bands");
    fails(editor, "material.set",
          R"({"path":"assets:/materials/red.material","value":{"parameters":{"anything":1}}})");

    // A file edited outside the editor is checked by material.validate.
    writeText(editor, "assets:/materials/bad.material",
              R"({"format":"illusion.material","version":2,"shader":"assets:/shaders/glow.shader",)"
              R"("parameters":{"pulse":3,"glow":1}})");
    const JsonDocument bad = ok(editor, "material.validate", R"({"path":"assets:/materials/bad.material"})");
    CHECK(bad.root().find("valid").asBool() == false);
    CHECK(bad.root().find("errorPath").asString() == "/parameters/glow");
    writeText(editor, "assets:/materials/missing.material",
              R"({"format":"illusion.material","version":2,"shader":"assets:/shaders/none.shader"})");
    const JsonDocument missing =
        ok(editor, "material.validate", R"({"path":"assets:/materials/missing.material"})");
    CHECK(missing.root().find("valid").asBool() == false);
    CHECK(missing.root().find("errorPath").asString() == "/shader");

    // Version 1 files and materials without a shader are valid.
    CHECK(ok(editor, "material.validate", R"({"path":"assets:/materials/red.material"})")
              .root()
              .find("valid")
              .asBool() == true);
}

TEST_CASE("the editor builds a shader again when its file changes, and shaders are an asset kind")
{
    EditorContextOptions options;
    options.assetWatching = FileWatchBackend::Polling;
    options.assetPollInterval = 0.0;
    EditorFixture fixture(1, options);
    EditorContext& editor = *fixture.editor;
    REQUIRE(editor.assets().scanned());
    writeText(editor, "assets:/shaders/glow.shader", kGlow);
    CHECK(ok(editor, "asset.list", R"({"path":"assets:/shaders","kind":"Shader"})")
              .root()
              .pointer("/assets/0/path")
              .asString() == "assets:/shaders/glow.shader");
    editor.endFrame(0.016); // a new file is built
    ShaderLoader& loader = editor.shaders();
    loader.waitIdle();
    const u64 before = loader.state("assets:/shaders/glow.shader").completed;
    CHECK(before >= 1);

    // An edit made outside the editor: the catalog finds it and the editor asks for a build.
    std::ofstream(fixture.directory.path / "project" / "assets" / "shaders" / "glow.shader", std::ios::binary)
        << kGlow << "\n// edited\n";
    editor.endFrame(0.016);
    loader.waitIdle();
    CHECK(loader.state("assets:/shaders/glow.shader").completed > before);
    CHECK(ok(editor, "material.validate", R"({"path":"assets:/shaders/glow.shader"})")
              .root()
              .find("valid")
              .asBool() == true);

    // A material that names the shader is a referrer of it.
    ok(editor, "material.create",
       R"({"path":"assets:/materials/glow.material","value":{"shader":"assets:/shaders/glow.shader"}})");
    CHECK(ok(editor, "asset.references", R"({"path":"assets:/shaders/glow.shader"})")
              .root()
              .pointer("/references/0/source")
              .asString() == "assets:/materials/glow.material");
}
