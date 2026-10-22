# Scenes

Scene files save Entities and their Components as JSON. They store reflected Components (see
[Reflection](reflection.md)) by stable type and field name instead of C++ type names, which differ
between compilers and change when code is refactored.

Headers: `<illusion/ecs/Reflection.hpp>`, `<illusion/ecs/SceneSerializer.hpp>`,
`<illusion/core/Version.hpp>`

## Reflecting Components

Reflect every Component that should be saved, once at startup:

```cpp
namespace game
{
enum class Team : illusion::u8
{
    Red,
    Blue,
};

struct Spinner
{
    illusion::Vec3 axis{0.0f, 1.0f, 0.0f};
    float speed = 1.0f;
    Team team = Team::Red;
    illusion::Entity target; // saved as a reference to another entity in the scene
};
} // namespace game

template <>
struct illusion::EnumReflection<game::Team>
{
    static constexpr std::array kValues{illusion::EnumValue{"Red", 0}, illusion::EnumValue{"Blue", 1}};
};

void registerGameComponents()
{
    illusion::reflectCoreComponents();   // illusion.Transform
    illusion::reflectRenderComponents(); // illusion.Camera, illusion.DirectionalLight, illusion.PointLight
    illusion::reflect<game::Spinner>("game.Spinner")
        .field("axis", &game::Spinner::axis)
        .field("speed", &game::Spinner::speed)
        .field("team", &game::Spinner::team)
        .field("target", &game::Spinner::target);
}
```

- Stable names use letters, digits, `_` and `.`; prefix them with your game or module name. A stable
  name belongs to one type: reusing it for another type is a fatal error.
- Field names use letters, digits and `_`. Renaming a C++ member does not change saved data as long as
  the reflected name stays the same.
- Fields can be numbers, vectors, strings, entities, enumerations, flags, reflected structs, arrays,
  optionals and JSON values; [Reflection](reflection.md#field-types) lists them with their JSON forms.
  Other types fail to compile.
- Reflected Components must be default constructible and copyable. Empty structs (tags) can be reflected
  too.
- Reflecting a type again replaces its earlier reflection. Reflect types during startup, before other
  threads read reflection data.
- The optional version (`reflect<T>("game.Spinner", 2)`) is written to scene files. Loading older data
  runs the type's [migrations](reflection.md#versions-and-migrations); a different version also produces
  a warning.

## Saving and loading scenes

```cpp
const illusion::SceneSaveResult saved = illusion::saveScene(world);
if (saved.ok())
{
    files.writeText("game:/scenes/level1.scene", saved.json);
}

std::string json;
files.readText("game:/scenes/level1.scene", json);
const illusion::SceneLoadResult loaded = illusion::loadScene(world, json);
if (!loaded.ok())
{
    ILL_LOG_ERROR("Scene", "cannot load level1: {}", loaded.error);
}
for (const std::string& warning : loaded.warnings)
{
    ILL_LOG_WARN("Scene", "{}", warning);
}
```

- `saveScene(world)` saves every live Entity; `saveScene(world, entities)` saves a list (for example a
  selection or a prefab). Entity fields that point outside the list are saved as `null`.
- Only reflected Components are saved; `SceneSaveResult::skippedComponents` counts the others and the
  runtime state that is reflected as transient (`PhysicsBody`, `AudioPlayback`, ...), which is never
  saved. `MeshRenderer` and `SkyLight` refer to GPU resources and are not saved yet (asset references in
  scenes come with the editor).
- Saving fails if a floating-point field is infinite or NaN, since JSON cannot store it.
- `loadScene()` adds the scene's entities to the World and returns them in file order. Entity fields
  are remapped to the new entities. If the file is invalid, nothing is added and `error` says where
  the problem is, for example `entity 0: field 'quat' of 'test.Everything' must be a number (at
  /quat/3)`.
- Loading is tolerant of code changes: fields missing from the file keep their default values, and
  unknown Component types and fields, and runtime Component types, are skipped with a warning.

## File format

Scene files are UTF-8 JSON with two-space indentation, so they diff and merge well in version control:

```json
{
  "format": "illusion.scene",
  "version": 1,
  "types": {
    "illusion.Transform": 1,
    "game.Spinner": 1
  },
  "entities": [
    {
      "illusion.Transform": {
        "position": [0.0, 1.0, 0.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "game.Spinner": {
        "axis": [0.0, 1.0, 0.0],
        "speed": 2.5,
        "team": "Blue",
        "target": null
      }
    }
  ]
}
```

- `rotation` is written as `[x, y, z, w]`, the order used by glTF.
- Enumeration values are written by name; values without a name are written as numbers.
- An Entity field is the index of an Entity in the `entities` array, or `null`.
- Values have the JSON forms of [reflection](reflection.md#json), except that version 1 scene files
  write `u64` values as numbers. Scenes are written and read through `writeValue()` and `readValue()`, so
  a Component's JSON is the same in a scene file and in the editor's commands.
- Floating-point numbers are written in their shortest exact form, so they load back bit for bit.

## Type-erased access

The editor and tools work with `ComponentId`s instead of C++ types. `World` offers `entities()`,
`componentsOf()`, `componentData()`, `emplaceDefault()` and `remove(Entity, ComponentId)` for this:

```cpp
std::vector<illusion::Entity> entities;
world.entities(entities);
const illusion::ComponentRegistry& registry = world.registry();
for (const illusion::Entity entity : entities)
{
    for (const illusion::ComponentId id : world.componentsOf(entity))
    {
        if (const illusion::TypeReflection* reflection = registry.reflection(id))
        {
            for (const illusion::FieldInfo& field : reflection->fields)
            {
                // field.name, field.type, field.offset into world.componentData(entity, id)
            }
        }
    }
}
```

Use `readEnumField()` and `writeEnumField()` for enumeration fields, whose size depends on the type, and
`readField()`, `writeField()` and the JSON functions of [Reflection](reflection.md) for any field.

Ids that come from outside the program (a file, a tool, a remote command) may be invalid, and functions
such as `registry.info(id)` assert on them. Check them first with `registry.contains(id)`, or use
`registry.tryInfo(id)`, which returns `nullptr` for an unknown id.

## Semantic versions

`SemanticVersion` parses, compares and checks [semantic versions](https://semver.org) (2.0.0). The
module system uses it to check that a game module was built for a compatible engine:

```cpp
const auto required = illusion::SemanticVersion::parse("0.1.0");
if (!illusion::engineVersion().isCompatibleWith(*required))
{
    ILL_LOG_ERROR("Game", "needs illusion {}, found {}", required->toString(),
                  illusion::engineVersion().toString());
}
```

`isCompatibleWith(required)` is true for the same major version (for 0.x, the same minor version) that
is not older than `required`. Pre-releases are older than their release.
