# Reflection

Reflection describes Component types and plain structs to the engine: a stable name, named fields and
metadata that tells the editor how to show them. Scene files, the editor, its command API and tools all
read and write values through reflection, without knowing the C++ types.

Headers: `<illusion/ecs/Reflection.hpp>`, `<illusion/ecs/StructRegistry.hpp>`,
`<illusion/ecs/ComponentJson.hpp>`

## Reflecting a type

Reflect every type once at startup (or in a module's load function). Structs used as fields are
reflected first:

```cpp
namespace game
{
struct Difficulty
{
    illusion::u32 enemyCount = 4;
    float spawnDelay = 2.0f;
};

struct Turret
{
    float turnRate = 1.5f;
    illusion::Vec3 beamColor{1.0f, 0.2f, 0.1f};
    illusion::u32 layer = 0;
    Difficulty difficulty;
    std::vector<illusion::Entity> targets;
};
} // namespace game

void registerGameTypes()
{
    using namespace illusion;
    reflectStruct<game::Difficulty>("game.Difficulty")
        .field("enemyCount", &game::Difficulty::enemyCount, {.min = 0.0})
        .field("spawnDelay", &game::Difficulty::spawnDelay, {.unit = Unit::Seconds, .min = 0.0});

    reflect<game::Turret>("game.Turret")
        .meta({.displayName = "Turret",
               .category = "Gameplay/Defence",
               .icon = "my_location",
               .description = "Shoots at the nearest target."})
        .field("turnRate", &game::Turret::turnRate,
               {.unit = Unit::Radians, .displayUnit = Unit::Degrees, .min = 0.0, .hint = FieldHint::Angle})
        .field("beamColor", &game::Turret::beamColor, {.hint = FieldHint::HdrColor})
        .field("layer", &game::Turret::layer, {.min = 0.0, .max = 15.0, .hint = FieldHint::Layer})
        .field("difficulty", &game::Turret::difficulty)
        .field("targets", &game::Turret::targets);
}
```

- Stable names use letters, digits, `_` and `.`; prefix them with your game or module name. Components
  and structs have separate name spaces.
- Field names use letters, digits and `_`. Renaming a C++ member does not change saved data as long as
  the reflected name stays the same.
- Reflected types must be default constructible and copyable.
- Reflecting a type again replaces its reflection. The old reflection stays readable until the registry
  is destroyed, so pointers to `TypeReflection` and `FieldInfo` never dangle.
- Every string (names, metadata, enumeration names) is copied, so the reflection of a hot-reloaded
  module outlives its code. Function tables (copying values, migrations) stay in the module and may be
  used only while it is loaded.

## Field types

| C++ type | `FieldType` | JSON |
| --- | --- | --- |
| `bool` | `Bool` | `true` / `false` |
| `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32` | `I8` ... `U32` | number |
| `u64` | `U64` | decimal string, such as `"18446744073709551615"` |
| `f32`, `f64` | `F32`, `F64` | number, or a unit-tagged number such as `{"deg": 60}` |
| `Vec2`, `Vec3`, `Vec4` | `Vector2` ... `Vector4` | `[x, y, z, w]`; colours also `"#rrggbb"` |
| `Quat` | `Quaternion` | `[x, y, z, w]` |
| `std::string` | `String` | string |
| `Entity` | `Entity` | through an `EntityResolver` (scene files: an index) |
| enumeration with `EnumReflection` | `Enum` | value name |
| enumeration with `kFlags = true` | `Flags` | array of value names |
| struct reflected with `reflectStruct()` | `Struct` | object |
| `std::array<T, N>` | `FixedArray` | array of N values |
| `std::vector<T>` (not `bool`) | `Array` | array |
| `std::optional<T>` | `Optional` | `null` or the value |
| `JsonText` | `Json` | any JSON value |
| `AssetRef` | `Asset` | the asset's path, `""` for none ([asset references](assets.md#asset-references)) |

`u64` values are written as strings so that readers which keep numbers as doubles (JavaScript, many MCP
clients) stay exact; numbers are accepted when reading. Enumeration values without a name are kept as
numbers. A flag enumeration declares its bits:

```cpp
template <>
struct illusion::EnumReflection<game::Senses>
{
    static constexpr bool kFlags = true;
    static constexpr std::array kValues{illusion::EnumValue{"Sight", 1}, illusion::EnumValue{"Hearing", 2}};
};
```

## Metadata

`FieldMeta` is the third argument of `field()`, `TypeMeta` goes to `meta()`. Designated initializers
must follow the member order.

| `FieldMeta` | Meaning |
| --- | --- |
| `displayName`, `description`, `category` | Label, tooltip and group in English (the label defaults to the field name split into words) |
| `unit`, `displayUnit` | Unit of the stored value and the unit shown, for example radians shown in degrees |
| `min`, `max` | Valid range; reading JSON rejects values outside it |
| `softMin`, `softMax`, `step` | Slider range and drag increment; values outside are still valid |
| `hint` | Widget: `Color`, `HdrColor`, `Angle`, `Slider`, `LogSlider`, `Multiline`, `Layer`, `LayerMask` |
| `readOnly`, `hidden`, `advanced` | Shown but not editable; not shown; shown when advanced fields are expanded |
| `renamedFrom` | Name of the field in older data |
| `assetKind` | `Asset` fields: the kind of asset expected (`Mesh`, `Material`, `Texture`, ...), for asset pickers |
| `validate` | Function that checks a value read from JSON; a rejected value fails the read with its message |

| `TypeMeta` | Meaning |
| --- | --- |
| `displayName`, `category`, `icon`, `description`, `docsAnchor` | Name, menu path ("Physics/Bodies"), Material Symbols icon, description, manual page |
| `requiredComponents` | Stable names of Components an Entity with this one also needs |
| `singleton` | At most one Entity per World should have it |
| `transient` | Runtime state: never saved, copied or edited |

Units are `Meters`, `Radians`, `Degrees`, `Seconds`, `Kilograms`, `Hertz`, `Newtons`, `MetersPerSecond`,
`Ratio`, `Percent` and `Decibels`. `convertUnit()` converts between units of one quantity (radians and
degrees, ratio and percent). Reflection checks metadata when it is registered: an `Angle` needs an angle
unit, colours must be vectors, ranges must be numeric and `min` must not exceed `max`.

Every engine Component is annotated this way; for example `Camera::fovY` is stored in radians and shown
in degrees, light colours are HDR colours, and `RigidBody` requires a `Collider`.

## Runtime state

Components that only hold runtime state (`MotionHistory`, `PhysicsBody`, `CharacterBody`, `PhysicsJoint`,
`AudioPlayback`) are reflected with `reflectRuntime()`. They get a stable name, so the editor can list
them, and are marked `transient`: scene files skip them.

```cpp
illusion::reflectRuntime<game::PathCache>("game.PathCache", {.displayName = "Path cache"});
```

## Values

`FieldValue` owns a copy of one field of any reflected type, for undo, multi-edit and generic tools. Values
up to 48 bytes are stored inline:

```cpp
const illusion::FieldInfo& speed = *reflection.field("speed");
illusion::FieldValue before = illusion::readField(&turret, speed);
illusion::writeField(&otherTurret, speed, before);
const bool same = illusion::fieldsEqual(&turret, &otherTurret, speed);
const illusion::FieldValue reset = illusion::defaultValue(reflection, speed);
if (const float* value = reset.get<float>())
{
    ILL_LOG_INFO("Game", "default speed {}", *value);
}
```

## JSON

`writeValue()` and `readValue()` convert a whole Component or struct; `getPath()` and `setPath()` one
value addressed by a dotted path such as `"difficulty.spawnDelay"` or `"targets.2"`:

```cpp
illusion::JsonMutDocument doc;
const auto written = illusion::writeValue(doc, reflection, &turret);

const auto parsed = illusion::JsonDocument::parse(R"({"turnRate": {"deg": 90}, "beamColor": "#ff8000"})");
const illusion::ValueReadResult read =
    illusion::readValue(reflection, &turret, parsed->root(), {.mode = illusion::JsonReadMode::Merge});
for (const illusion::ValueError& error : read.errors)
{
    ILL_LOG_ERROR("Game", "{}: {}", error.pointer, error.message); // e.g. "/beamColor: must be ..."
}

const auto value = illusion::JsonDocument::parse("6");
const auto set = illusion::setPath(reflection, &turret, "difficulty.enemyCount", value->root());
```

- `JsonReadMode::Replace` (the default) gives fields that the JSON does not mention their default values;
  `Merge` keeps their current values.
- Reading is atomic: if anything is wrong, the object is unchanged. Every problem is reported with a JSON
  Pointer such as `/position/1`, and members that match no field are listed in `unknownKeys`. Malformed
  input never asserts.
- Numbers with a unit accept unit-tagged input, `{"deg": 60}` or `{"%": 50}`, converted to the stored unit.
  Colour fields accept sRGB hex strings, converted to linear values.
- `Entity` fields go through an `EntityResolver`. Scene files store indices; without a resolver, handles
  are written as process-local numbers.

## Versions and migrations

Increase a type's version when the meaning of saved data changes, and register the steps that convert
older JSON. `renamedFrom` handles renamed fields on its own:

```cpp
illusion::reflect<game::Lamp>("game.Lamp", 2)
    .field("brightness", &game::Lamp::brightness, {.renamedFrom = "power"})
    .field("angle", &game::Lamp::angle, {.unit = illusion::Unit::Radians, .hint = illusion::FieldHint::Angle})
    .migrate(1, [](illusion::JsonMutDocument& doc, illusion::JsonMutValue object) {
        // Version 1 stored the angle in degrees.
        if (const auto degrees = object.find("angle").asF64())
        {
            object.set("angle", doc.makeF64(*degrees * std::numbers::pi / 180.0));
        }
    });
```

When `ValueReadOptions::savedVersion` is older than the type, fields are renamed first and then every
step from that version on runs in order; scene files pass the version from their `types` table. Data
saved by a newer version loads as far as its fields are known, with a warning. Only the object itself is
migrated, not the structs inside it.
