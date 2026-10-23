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
    {"clipboard.copy", R"({"entities":["/Game/Board"]})"},
    {"clipboard.cut", R"({"entities":["/Game/Player"]})"},
    {"clipboard.paste", R"({"parent":"/Game","index":0})"},
    {"component.add", R"({"entity":"/Light","type":"test.Link","value":{"weight":2}})"},
    {"component.get", R"({"entity":"/Game/Board","type":"illusion.LocalTransform"})"},
    {"component.remove", R"({"entity":"/Game/Player","type":"test.Link"})"},
    {"component.reset", R"({"entity":"/Game/Board","type":"illusion.LocalTransform"})"},
    {"component.schema", R"({"type":"illusion.Transform"})"},
    {"component.set",
     R"({"entity":"/Game/Board","type":"illusion.LocalTransform","value":{"position":[1,2,3]}})"},
    {"component.set_field",
     R"({"entity":"/Game/Board","type":"illusion.LocalTransform","path":"position","value":[0,5,0]})"},
    {"component.types", R"({})"},
    {"editor.describe", R"({"command":"entity.create"})"},
    {"editor.list_commands", R"({})"},
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
    {"transform.get", R"({"entity":"/Game/Board","space":"local"})"},
    {"transform.reset", R"({"entities":["/Game/Board"],"scale":false})"},
    {"transform.rotate", R"({"entities":["/Game/Board","/Light"],"euler":[0,45,0],"pivot":"center"})"},
    {"transform.scale", R"({"entities":["/Game"],"factor":[2,2,2],"point":[0,0,0]})"},
    {"transform.set", R"({"entities":["/Game/Board"],"position":[1,2,3],"euler":[0,90,0]})"},
    {"transform.translate", R"({"entities":["/Game/Board","/Light"],"offset":[1,0,0],"space":"self"})"},
};

} // namespace illusion::test
