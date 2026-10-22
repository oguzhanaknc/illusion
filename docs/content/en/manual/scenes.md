# Scenes

Scene files save Entities and their Components as JSON. They store reflected Components (see
[Reflection](reflection.md)) by stable type and field name instead of C++ type names, which differ
between compilers and change when code is refactored.

Headers: `<illusion/ecs/Reflection.hpp>`, `<illusion/ecs/SceneSerializer.hpp>`,
`<illusion/ecs/EntityGuid.hpp>`, `<illusion/ecs/Name.hpp>`, `<illusion/core/Version.hpp>`

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
illusion::assignMissingGuids(world); // saving needs an EntityGuid on every entity
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

- `saveScene(world)` saves every live Entity. `SceneSaveOptions::subset` saves a list instead (for example
  a selection or a prefab); Entity fields that point outside the list are saved as `null`.
- Every saved Entity needs an `EntityGuid` (see [Entity ids](#entity-ids)); saving fails if one has none
  or two share one. Entities are saved in the depth-first pre-order of the
  [transform hierarchy](ecs.md#transform-hierarchy): every parent before its children, siblings by
  `Hierarchy::order`, roots (also entities whose parent is not saved) by order with roots without a
  `Hierarchy` last, and ties by id, so the same scene always gives the same file. `SceneOrder::ById`
  sorts by id only, and `SceneOrder::AsGiven` keeps the order of the subset, or of `World::entities()`
  without one.
- The `Transform` of an Entity with a `LocalTransform` whose parent is saved too is derived, so it is not
  saved. After loading, `updateTransformHierarchy()` adds and computes it; `Disabled` is saved, `Inactive`
  is runtime state.
- Only reflected Components are saved; `SceneSaveResult::skippedComponents` counts the others and the
  runtime state that is reflected as transient (`PhysicsBody`, `AudioPlayback`, ...), which is never
  saved. `MeshRenderer` and `SkyLight` refer to GPU resources and are not saved yet (asset references in
  scenes come with the editor).
- Saving fails if a floating-point field is infinite or NaN, since JSON cannot store it.
- `loadScene()` adds the scene's entities to the World and returns them in file order. Entity fields
  are remapped to the new entities. If the file is invalid, nothing is added and `error` says where
  the problem is, for example `entity 0: field 'quat' of 'test.Everything' must be a number (at
  /quat/3)`.
- Loading is tolerant of code changes: fields missing from the file keep their default values, unknown
  fields and runtime Component types are skipped with a warning, and Components of unknown types are
  [preserved](#unknown-types).

## Entity ids

An `Entity` handle is valid only in the World and process that created it. Saved entities have a
persistent id instead, the `EntityGuid` Component (`illusion.EntityGuid`, read-only in the editor), which
scene files, the editor's undo history and its commands use to refer to them:

```cpp
illusion::World world;
const illusion::Entity player = world.create(illusion::Transform{}, illusion::Name{"Player"});
illusion::assignMissingGuids(world); // every entity without an id gets one

const illusion::EntityGuid id = world.get<illusion::EntityGuid>(player);
ILL_LOG_INFO("Game", "player id: {}", illusion::toString(id)); // e.g. 3f9a0c1e5b7d2a40

const illusion::GuidEntityResolver ids(world);
const illusion::Entity found = ids.find(id); // == player
```

- Ids are 64-bit numbers written as 16 lower-case hexadecimal digits; `0` means "no id". `parseGuid()`
  reads the text form back.
- `GuidGenerator` makes the ids. The default generator takes its key from the operating system's secure
  random source, so ids made in different sessions and on different machines do not collide, and one
  generator never repeats an id. `GuidGenerator(seed)` gives the same ids on every platform, for tests and
  reproducible tools. `GuidGenerator::shared()` is the process-wide generator; `next()` may be called from
  several threads.
- `GuidEntityResolver` is the `EntityResolver` of a World (see [Reflection](reflection.md#json)): with it,
  `writeValue()` and `readValue()` write Entity fields as ids and read them back. Call `refresh()` after
  structural changes.

`SceneLoadOptions::ids` decides which ids loaded entities get:

- `IdPolicy::Keep` (the default, to open a scene) keeps the ids of the file. An id that an entity of the
  World already has is a load error.
- `IdPolicy::Regenerate` (to paste or instance a scene) gives every entity a new id. References inside the
  file are remapped either way, and `SceneLoadResult::fileIds` holds the id each entity had in the file:

```cpp
const illusion::Entity selection[] = {door, frame};
const illusion::SceneSaveResult copied = illusion::saveScene(world, {.subset = selection});

const illusion::SceneLoadResult pasted =
    illusion::loadScene(world, copied.json, {.ids = illusion::IdPolicy::Regenerate});
// pasted.fileIds[i] is the id that pasted.entities[i] had in the copied text.
```

Duplicate ids in one file are a load error.

## Names

`Name` (`illusion.Name`) is an optional label that the editor shows; names need not be unique. A name has
at most 256 bytes of UTF-8 and contains no `/` (reserved for entity paths) and no control characters.
Reading a Name from a scene file or an editor command rejects other names, through the field's validator
(`FieldMeta::validate`, see [Reflection](reflection.md#metadata)); `isValidEntityName()` and
`entityNameError()` check names in code.

## Unknown types

A scene may contain Components of types that are not registered, for example when a game module is not
loaded. They are not lost: loading keeps their JSON and saved version in the runtime Component
`UnknownComponents`, which no engine System queries, and saving writes them back unchanged, so opening
and saving a scene never drops another module's data. References inside them are not remapped by
`IdPolicy::Regenerate`, because their types are unknown. Unknown fields of known types are still dropped
with a warning, after [renames](reflection.md#versions-and-migrations) are applied.

## Settings

A scene can carry reflected structs (see [Reflection](reflection.md)) that are not Components, such as its
render or weather settings, in a `"settings"` object:

```cpp
game::Weather weather; // reflected with reflectStruct<game::Weather>("game.Weather")
const illusion::SceneSettings settings[] = {illusion::sceneSettings(weather)};
const illusion::SceneSaveResult saved = illusion::saveScene(world, {.settings = settings});

game::Weather loadedWeather;
const illusion::SceneSettingsTarget targets[] = {illusion::sceneSettingsTarget(loadedWeather)};
const illusion::SceneLoadResult loaded = illusion::loadScene(otherWorld, saved.json, {.settings = targets});
```

Targets change only if the whole scene loads. Settings that no target asks for, including those of unknown
types, are returned in `SceneLoadResult::preservedSettings`; pass them to
`SceneSaveOptions::preservedSettings` to save them again.

## File format

Scene files are UTF-8 JSON with two-space indentation, so they diff and merge well in version control:

```json
{
  "format": "illusion.scene",
  "version": 2,
  "types": {
    "game.Spinner": 1,
    "illusion.Name": 1,
    "illusion.Transform": 1
  },
  "settings": {},
  "entities": [
    {
      "id": "3f9a0c1e5b7d2a40",
      "components": {
        "game.Spinner": {
          "axis": [0.0, 1.0, 0.0],
          "speed": 2.5,
          "team": "Blue",
          "target": "9b21d07c4e6f1a35"
        },
        "illusion.Name": {
          "value": "Turret"
        },
        "illusion.Transform": {
          "position": [0.0, 1.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "id": "9b21d07c4e6f1a35",
      "components": {
        "illusion.Name": {
          "value": "Player"
        }
      }
    }
  ]
}
```

- Each entity is its `id` and its `components`, ordered by stable name. The `types` table lists the
  version of every Component and settings type in the file.
- `rotation` is written as `[x, y, z, w]`, the order used by glTF.
- Enumeration values are written by name; values without a name are written as numbers.
- An Entity field is the `id` of an entity in the file, or `null`. A child's `illusion.Hierarchy` names
  its parent this way; children follow their parents in the file.
- Values have the JSON forms of [reflection](reflection.md#json). Scenes are written and read through
  `writeValue()` and `readValue()`, so a Component's JSON is the same in a scene file and in the editor's
  commands.
- Floating-point numbers are written in their shortest exact form, so they load back bit for bit.

### Version 1 files

Scene files written before entity ids (`"version": 1`) still load. Their entities have no ids, so they get
new ones, and Entity fields, which are indices into the `entities` array, are remapped. The next save
writes version 2. In version 1 files each entity object holds its Components directly and `u64` values
are numbers.

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

`World::forEachMatching(all, none, fn)` is a Query by `ComponentId`: it calls `fn` once per Chunk of
the entities that have every Component in `all` and none in `none`, with their handles and raw column
pointers:

```cpp
illusion::ComponentMask all;
const illusion::ComponentId guidId = illusion::componentId<illusion::EntityGuid>();
all.set(guidId);
world.forEachMatching(all, {}, [&](const illusion::RawChunk& chunk) {
    const auto* guids = static_cast<const illusion::EntityGuid*>(chunk.column(guidId));
    for (illusion::u32 i = 0; i < chunk.size(); ++i)
    {
        // chunk.entities()[i] has the id guids[i]
    }
});
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
