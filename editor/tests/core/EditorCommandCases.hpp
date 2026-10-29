#pragma once

// Valid params of every core command on the fixture scene (EditorFixture::buildScene()), for the tests that
// run every command: success, dry runs, output schemas and fuzzing.

#include <string_view>

namespace illusion::test
{

struct CommandCase
{
    std::string_view name;
    std::string_view params;
};

inline constexpr CommandCase kCommandCases[] = {
    {"asset.create_folder", R"({"path":"assets:/models"})"},
    {"asset.delete", R"({"path":"assets:/textures/red.png","force":true})"},
    {"asset.dependencies", R"({"path":"assets:/scenes/main.scene"})"},
    {"asset.info", R"({"path":"assets:/materials/red.material"})"},
    {"asset.list", R"({"path":"assets:/","recursive":true,"kind":"Material"})"},
    {"asset.move", R"({"from":"assets:/materials/red.material","to":"assets:/materials/crimson.material"})"},
    {"asset.references", R"({"path":"assets:/materials/red.material"})"},
    {"camera.bookmark_go", R"({"slot":1,"animate":false})"},
    {"camera.bookmark_set", R"({"slot":1})"},
    {"clipboard.copy", R"({"entities":["/Game/Board"]})"},
    {"clipboard.cut", R"({"entities":["/Game/Player"]})"},
    {"clipboard.paste", R"({"parent":"/Game","index":0})"},
    {"component.add", R"({"entity":"/Light","type":"test.Link","value":{"weight":2}})"},
    {"component.copy", R"({"entity":"/Game/Player","type":"test.Link"})"},
    {"component.get", R"({"entity":"/Game/Board","type":"illusion.LocalTransform"})"},
    {"component.paste", R"({"entities":["/Light"]})"},
    {"component.remove", R"({"entity":"/Game/Player","type":"test.Link"})"},
    {"component.reset", R"({"entity":"/Game/Board","type":"illusion.LocalTransform"})"},
    {"component.schema", R"({"type":"illusion.Transform"})"},
    {"component.set",
     R"({"entity":"/Game/Board","type":"illusion.LocalTransform","value":{"position":[1,2,3]}})"},
    {"component.set_field",
     R"({"entity":"/Game/Board","type":"illusion.LocalTransform","path":"position","value":[0,5,0]})"},
    {"component.types", R"({})"},
    {"editor.batch",
     R"({"label":"Two","steps":[{"as":"made","method":"entity.create","params":{"name":"Made","parent":"/Game"}},{"method":"entity.rename","params":{"entity":"$made.entity.id","name":"Renamed"}}]})"},
    {"editor.describe", R"({"command":"entity.create"})"},
    {"editor.list_commands", R"({})"},
    {"editor.screenshot", R"({"width":128,"height":96})"},
    {"editor.wait", R"({"frames":3})"},
    {"entity.create", R"({"name":"New","parent":"/Game","components":{"test.Link":{"weight":3}}})"},
    {"entity.destroy", R"({"entities":["/Game/Board"]})"},
    {"entity.duplicate", R"({"entities":["/Game/Board"]})"},
    {"entity.find", R"({"name":"Cell*"})"},
    {"entity.get", R"({"entity":"/Game/Board"})"},
    {"entity.rename", R"({"entity":"/Game/Board","name":"Field"})"},
    {"entity.set_enabled", R"({"entity":"/Game/Board","enabled":false})"},
    {"hierarchy.children", R"({"entity":"/Game"})"},
    {"hierarchy.reorder", R"({"entity":"/Game/Player","index":0})"},
    {"hierarchy.set_parent", R"({"entity":"/Game/Player","parent":"/Game/Board"})"},
    {"history.clear", R"({})"},
    {"history.jump", R"({"position":0})"},
    {"history.list", R"({})"},
    {"history.redo", R"({})"},
    {"history.undo", R"({})"},
    {"log.clear", R"({})"},
    {"log.read", R"({"minLevel":"trace","limit":10})"},
    {"log.set_level", R"({"level":"warn"})"},
    {"material.create",
     R"({"path":"assets:/materials/blue.material","value":{"material":{"metallicFactor":1}}})"},
    {"material.get", R"({"path":"assets:/materials/red.material"})"},
    {"material.set",
     R"({"path":"assets:/materials/red.material","value":{"material":{"roughnessFactor":0.25}}})"},
    {"preferences.get", R"({})"},
    {"preferences.set", R"({"value":{"theme":"light","uiScale":1.25}})"},
    {"project.create",
     R"({"directory":"second","name":"Second","template":"3d-starter","discardChanges":true})"},
    {"project.file_delete", R"({"path":"assets:/scenes/main.scene"})"},
    {"project.file_list", R"({"path":"assets:/","recursive":true})"},
    {"project.file_mkdir", R"({"path":"assets:/levels/one"})"},
    {"project.file_move", R"({"from":"assets:/scenes/main.scene","to":"assets:/scenes/moved.scene"})"},
    {"project.file_read", R"({"path":"assets:/scenes/main.scene"})"},
    {"project.file_stat", R"({"path":"assets:/scenes/main.scene"})"},
    {"project.file_write", R"({"path":"assets:/notes.txt","text":"hello"})"},
    {"project.info", R"({})"},
    {"project.open", R"({"directory":"project","discardChanges":true})"},
    {"project.recover", R"({})"},
    {"project.settings_get", R"({})"},
    {"project.settings_set", R"({"type":"test.Controls","value":{"invertY":true}})"},
    {"render.diagnostics", R"({})"},
    {"render.gpu_info", R"({})"},
    {"render.stats", R"({"viewport":"scene"})"},
    {"scene.info", R"({})"},
    {"scene.new", R"({"discardChanges":true})"},
    {"scene.open", R"({"path":"assets:/scenes/main.scene","discardChanges":true})"},
    {"scene.query", R"({"has":["illusion.Hierarchy"],"name":"*","limit":2})"},
    {"scene.save", R"({})"},
    {"scene.save_as", R"({"path":"assets:/scenes/copy.scene"})"},
    {"scene.settings_get", R"({})"},
    {"scene.settings_set",
     R"({"type":"test.Difficulty","value":{"enemies":12,"music":"assets:/audio/theme.ogg"}})"},
    {"scene.tree", R"({"depth":1})"},
    {"selection.add", R"({"entities":["/Light"]})"},
    {"selection.clear", R"({})"},
    {"selection.get", R"({})"},
    {"selection.remove", R"({"entities":["/Game/Board"]})"},
    {"selection.set", R"({"entities":["/Game/Board","/Light"]})"},
    {"transaction.begin", R"({"label":"Group"})"},
    {"transaction.cancel", R"({})"},
    {"transaction.commit", R"({})"},
    {"transform.get", R"({"entity":"/Game/Board","space":"local"})"},
    {"transform.reset", R"({"entities":["/Game/Board"],"scale":false})"},
    {"transform.rotate", R"({"entities":["/Game/Board","/Light"],"euler":[0,45,0],"pivot":"center"})"},
    {"transform.scale", R"({"entities":["/Game"],"factor":[2,2,2],"point":[0,0,0]})"},
    {"transform.set", R"({"entities":["/Game/Board"],"position":[1,2,3],"euler":[0,90,0]})"},
    {"transform.translate", R"({"entities":["/Game/Board","/Light"],"offset":[1,0,0],"space":"self"})"},
    {"ui.layout_apply", R"({"name":"default"})"},
    {"ui.layout_delete", R"({"name":"saved"})"},
    {"ui.layout_reset", R"({})"},
    {"ui.layout_save", R"({"name":"mine"})"},
    {"ui.notify", R"({"message":"Board built","level":"success"})"},
    {"ui.palette", R"({"mode":"entities","query":"Board"})"},
    {"ui.quit", R"({})"},
    {"ui.show_panel", R"({"panel":"panel.console"})"},
    {"ui.state", R"({})"},
    {"view.get", R"({})"},
    {"view.set_entities", R"({"entities":["/Game/Board"],"hidden":true})"},
    {"viewport.camera_get", R"({"viewport":"scene"})"},
    {"viewport.camera_set", R"({"pivot":[0,1,0],"eye":[6,4,6],"animate":false})"},
    {"viewport.close", R"({"viewport":"scene-2"})"},
    {"viewport.create", R"({})"},
    {"viewport.frame", R"({"entities":["/Game/Board"]})"},
    {"viewport.list", R"({})"},
    {"viewport.pick", R"({"x":640,"y":360,"cycle":true,"select":"replace"})"},
    {"viewport.pick_rect", R"({"x":100,"y":80,"width":400,"height":300,"select":"replace"})"},
    {"viewport.project", R"({"entity":"/Game/Board"})"},
    {"viewport.sample", R"({"x":640,"y":360,"radius":2})"},
    {"viewport.screenshot", R"({"viewport":"scene"})"},
    {"viewport.set_aspect", R"({"aspect":"fixed","width":640,"height":360})"},
    {"viewport.set_mode", R"({"shading":"normals","wireframe":true})"},
    {"viewport.set_overlays", R"({"grid":false,"selectionOutline":true})"},
    {"viewport.view", R"({"view":"top","animate":false})"},
};

// Commands that need another one to run first (with {} params) on the fixture.
inline constexpr CommandCase kCommandSetups[] = {
    {"camera.bookmark_go", "camera.bookmark_set"},
    {"transaction.cancel", "transaction.begin"},
    {"transaction.commit", "transaction.begin"},
    {"viewport.close", "viewport.create"},
};

/// The command to run before @p name on the fixture ("" for none).
constexpr std::string_view setupOf(std::string_view name)
{
    for (const CommandCase& entry : kCommandSetups)
    {
        if (entry.name == name)
        {
            return entry.params;
        }
    }
    return {};
}

} // namespace illusion::test
