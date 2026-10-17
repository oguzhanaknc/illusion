# Reflection ve Sahneler

Reflection, Component tiplerine açık ve kararlı bir ad ile adlandırılmış alanlardan oluşan bir liste
verir. Sahne dosyaları, editör ve araçlar C++ tip adları yerine bunu kullanır; çünkü C++ tip adları
derleyiciden derleyiciye farklıdır ve kod yeniden düzenlendiğinde değişir.

Header'lar: `<illusion/ecs/Reflection.hpp>`, `<illusion/ecs/SceneSerializer.hpp>`,
`<illusion/core/Version.hpp>`

## Component'leri yansıtma

Kaydedilmesi gereken her Component'i başlangıçta bir kez yansıtın (reflect):

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

- Kararlı adlar harf, rakam, `_` ve `.` kullanır; başlarına oyununuzun veya modülünüzün adını ekleyin.
  Bir kararlı ad tek bir tipe aittir: başka bir tip için yeniden kullanmak ölümcül bir hatadır.
- Alan adları harf, rakam ve `_` kullanır. Yansıtılan ad aynı kaldıkça bir C++ üyesini yeniden
  adlandırmak kaydedilmiş veriyi değiştirmez.
- Desteklenen alan tipleri: `bool`, `i32`, `u32`, `i64`, `u64`, `f32`, `f64`, `Vec2`, `Vec3`, `Vec4`,
  `Quat`, `std::string`, `Entity` ve `EnumReflection` özelleştirmesi olan enum'lar (özelleştirme, enum
  yansıtılmadan önce bildirilmelidir). Diğer tipler derlenmez.
- Yansıtılan Component'ler varsayılan olarak oluşturulabilir (default constructible) olmalıdır. Boş
  struct'lar (tag'ler) da yansıtılabilir.
- Bir tipi yeniden yansıtmak önceki yansıtmanın yerini alır. Tipleri, diğer thread'ler reflection
  verisini okumadan önce, başlangıçta yansıtın.
- İsteğe bağlı sürüm (`reflect<T>("game.Spinner", 2)`) sahne dosyalarına yazılır; yüklerken farklı bir
  sürüm bir uyarı üretir.

## Sahneleri kaydetme ve yükleme

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

- `saveScene(world)` canlı her Entity'yi kaydeder; `saveScene(world, entities)` bir listeyi kaydeder
  (örneğin bir seçimi veya bir prefab'ı). Listenin dışını gösteren Entity alanları `null` olarak
  kaydedilir.
- Yalnızca yansıtılmış Component'ler kaydedilir; `SceneSaveResult::skippedComponents` diğerlerini
  sayar. `MeshRenderer` ve `SkyLight` GPU kaynaklarına başvurur ve henüz kaydedilmez (sahnelerdeki
  Asset başvuruları editörle birlikte gelecek).
- Bir kayan noktalı alan sonsuz veya NaN ise kaydetme başarısız olur, çünkü JSON bunu saklayamaz.
- `loadScene()` sahnenin Entity'lerini World'e ekler ve onları dosyadaki sırayla döndürür. Entity
  alanları yeni Entity'lere eşlenir. Dosya geçersizse hiçbir şey eklenmez ve `error` sorunun nerede
  olduğunu söyler.
- Yükleme kod değişikliklerine toleranslıdır: dosyada olmayan alanlar varsayılan değerlerini korur;
  bilinmeyen Component tipleri ve alanlar bir uyarıyla atlanır.

## Dosya biçimi

Sahne dosyaları iki boşluk girintili UTF-8 JSON'dur; böylece sürüm kontrolünde iyi diff'lenir ve
birleştirilir:

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

- `rotation`, glTF'in kullandığı sırayla `[x, y, z, w]` olarak yazılır.
- Enum değerleri adlarıyla yazılır; adı olmayan değerler sayı olarak yazılır.
- Bir Entity alanı, `entities` dizisindeki bir Entity'nin indeksidir ya da `null`'dır.
- Kayan noktalı sayılar en kısa kesin biçimleriyle yazılır; böylece bit bit aynı olarak geri yüklenir.

## Tipi silinmiş erişim

Editör ve araçlar C++ tipleri yerine `ComponentId`'lerle çalışır. `World` bunun için `entities()`,
`componentsOf()`, `componentData()`, `emplaceDefault()` ve `remove(Entity, ComponentId)` sunar:

```cpp
std::vector<illusion::Entity> entities;
world.entities(entities);
const illusion::ComponentRegistry& registry = world.registry();
for (const illusion::Entity entity : entities)
{
    for (const illusion::ComponentId id : world.componentsOf(entity))
    {
        if (const illusion::ComponentReflection* reflection = registry.reflection(id))
        {
            for (const illusion::FieldInfo& field : reflection->fields)
            {
                // field.name, field.type, field.offset into world.componentData(entity, id)
            }
        }
    }
}
```

Boyutu tipe bağlı olan enum alanları için `readEnumField()` ve `writeEnumField()` kullanın.

## Anlamsal sürümler

`SemanticVersion`, [anlamsal sürümleri](https://semver.org) (2.0.0) ayrıştırır, karşılaştırır ve
denetler. Modül sistemi, bir oyun modülünün uyumlu bir motor için derlendiğini denetlemek için bunu
kullanır:

```cpp
const auto required = illusion::SemanticVersion::parse("0.1.0");
if (!illusion::engineVersion().isCompatibleWith(*required))
{
    ILL_LOG_ERROR("Game", "needs illusion {}, found {}", required->toString(),
                  illusion::engineVersion().toString());
}
```

`isCompatibleWith(required)`, aynı ana sürüm için (0.x'te aynı ara sürüm için) ve `required`'dan eski
olmayan sürümler için true döner. Ön sürümler (pre-release) kendi sürümlerinden eskidir.
