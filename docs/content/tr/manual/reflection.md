# Reflection

Reflection, Component tiplerini ve düz struct'ları motora tanıtır: kararlı bir ad, adlandırılmış alanlar
ve editöre bunları nasıl göstereceğini söyleyen metadata. Sahne dosyaları, editör, editörün komut API'si
ve araçlar değerleri C++ tiplerini bilmeden reflection üzerinden okur ve yazar.

Header'lar: `<illusion/ecs/Reflection.hpp>`, `<illusion/ecs/StructRegistry.hpp>`,
`<illusion/ecs/ComponentJson.hpp>`

## Bir tipi yansıtma

Her tipi başlangıçta (veya bir modülün yükleme fonksiyonunda) bir kez yansıtın. Alan olarak kullanılan
struct'lar önce yansıtılır:

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

- Kararlı adlar harf, rakam, `_` ve `.` kullanır; başlarına oyununuzun veya modülünüzün adını ekleyin.
  Component'lerin ve struct'ların ad alanları ayrıdır.
- Alan adları harf, rakam ve `_` kullanır. Yansıtılan ad aynı kaldıkça bir C++ üyesini yeniden
  adlandırmak kaydedilmiş veriyi değiştirmez.
- Yansıtılan tipler varsayılan olarak oluşturulabilir (default constructible) ve kopyalanabilir olmalıdır.
- Bir tipi yeniden yansıtmak reflection'ının yerini alır. Eski reflection registry yok edilene kadar
  okunabilir kalır; bu yüzden `TypeReflection` ve `FieldInfo` işaretçileri hiçbir zaman geçersiz olmaz.
- Tüm metinler (adlar, metadata, enum adları) kopyalanır; böylece hot-reload edilen bir modülün
  reflection'ı kodundan uzun yaşar. Fonksiyon tabloları (değer kopyalama, migration'lar) modülde kalır ve
  yalnızca modül yüklüyken kullanılabilir.

## Alan tipleri

| C++ tipi | `FieldType` | JSON |
| --- | --- | --- |
| `bool` | `Bool` | `true` / `false` |
| `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32` | `I8` ... `U32` | sayı |
| `u64` | `U64` | `"18446744073709551615"` gibi ondalık metin |
| `f32`, `f64` | `F32`, `F64` | sayı veya `{"deg": 60}` gibi birim etiketli sayı |
| `Vec2`, `Vec3`, `Vec4` | `Vector2` ... `Vector4` | `[x, y, z, w]`; renkler ayrıca `"#rrggbb"` |
| `Quat` | `Quaternion` | `[x, y, z, w]` |
| `std::string` | `String` | metin |
| `Entity` | `Entity` | bir `EntityResolver` üzerinden (sahne dosyalarında: bir indeks) |
| `EnumReflection` özelleştirmesi olan enum | `Enum` | değer adı |
| `kFlags = true` olan enum | `Flags` | değer adlarından oluşan dizi |
| `reflectStruct()` ile yansıtılan struct | `Struct` | nesne |
| `std::array<T, N>` | `FixedArray` | N değerlik dizi |
| `std::vector<T>` (`bool` değil) | `Array` | dizi |
| `std::optional<T>` | `Optional` | `null` veya değer |
| `JsonText` | `Json` | herhangi bir JSON değeri |
| `AssetRef` | `Asset` | Asset'in yolu, yoksa `""` ([Asset referansları](assets.md#asset-referanslar)) |

`u64` değerleri metin olarak yazılır; böylece sayıları double olarak tutan okuyucular (JavaScript,
birçok MCP istemcisi) tam değeri korur. Okurken sayılar da kabul edilir. Adı olmayan enum değerleri sayı
olarak korunur. Bir flag enum'u bitlerini şöyle bildirir:

```cpp
template <>
struct illusion::EnumReflection<game::Senses>
{
    static constexpr bool kFlags = true;
    static constexpr std::array kValues{illusion::EnumValue{"Sight", 1}, illusion::EnumValue{"Hearing", 2}};
};
```

## Metadata

`FieldMeta`, `field()`'ın üçüncü argümanıdır; `TypeMeta` ise `meta()`'ya verilir. Designated
initializer'lar üye sırasını izlemelidir.

| `FieldMeta` | Anlamı |
| --- | --- |
| `displayName`, `description`, `category` | İngilizce etiket, ipucu ve grup (etiketin varsayılanı, kelimelere ayrılmış alan adıdır) |
| `unit`, `displayUnit` | Saklanan değerin birimi ve gösterilen birim; örneğin derece olarak gösterilen radyan |
| `min`, `max` | Geçerli aralık; JSON okunurken aralık dışındaki değerler reddedilir |
| `softMin`, `softMax`, `step` | Slider aralığı ve sürükleme adımı; dışındaki değerler yine geçerlidir |
| `hint` | Widget: `Color`, `HdrColor`, `Angle`, `Slider`, `LogSlider`, `Multiline`, `Layer`, `LayerMask` |
| `readOnly`, `hidden`, `advanced` | Gösterilir ama düzenlenemez; gösterilmez; gelişmiş alanlar açıldığında gösterilir |
| `renamedFrom` | Alanın eski verideki adı |
| `assetKind` | `Asset` alanları: beklenen Asset türü (`Mesh`, `Material`, `Texture`, ...), Asset seçiciler için |
| `validate` | JSON'dan okunan bir değeri denetleyen fonksiyon; reddettiği değer, mesajıyla okumayı başarısız kılar |

| `TypeMeta` | Anlamı |
| --- | --- |
| `displayName`, `category`, `icon`, `description`, `docsAnchor` | Ad, menü yolu ("Physics/Bodies"), Material Symbols simgesi, açıklama, kılavuz sayfası |
| `requiredComponents` | Bu Component'e sahip bir Entity'nin ayrıca ihtiyaç duyduğu Component'lerin kararlı adları |
| `singleton` | Her World'de en fazla bir Entity'de bulunmalıdır |
| `transient` | Çalışma zamanı durumu: hiçbir zaman kaydedilmez, kopyalanmaz veya düzenlenmez |

Birimler `Meters`, `Radians`, `Degrees`, `Seconds`, `Kilograms`, `Hertz`, `Newtons`, `MetersPerSecond`,
`Ratio`, `Percent` ve `Decibels`'tır. `convertUnit()` aynı büyüklüğün birimleri arasında dönüştürür
(radyan ve derece, oran ve yüzde). Reflection, metadata'yı kaydedilirken denetler: `Angle` bir açı birimi
ister, renkler vektör olmalıdır, aralıklar sayısal olmalıdır ve `min`, `max`'tan büyük olamaz.

Motorun her Component'i bu şekilde açıklanmıştır; örneğin `Camera::fovY` radyan olarak saklanır ve
derece olarak gösterilir, ışık renkleri HDR renklerdir ve `RigidBody` bir `Collider` gerektirir.

## Çalışma zamanı durumu

Yalnızca çalışma zamanı durumu tutan Component'ler (`MotionHistory`, `PhysicsBody`, `CharacterBody`,
`PhysicsJoint`, `AudioPlayback`) `reflectRuntime()` ile yansıtılır. Editörün onları listeleyebilmesi için
kararlı bir ad alırlar ve `transient` olarak işaretlenirler: sahne dosyaları onları atlar.

```cpp
illusion::reflectRuntime<game::PathCache>("game.PathCache", {.displayName = "Path cache"});
```

## Değerler

`FieldValue`, herhangi bir yansıtılmış tipin bir alanının kopyasını sahiplenir; geri alma, çoklu
düzenleme ve genel araçlar içindir. 48 bayta kadar olan değerler satır içinde saklanır:

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

`writeValue()` ve `readValue()` bütün bir Component'i veya struct'ı dönüştürür; `getPath()` ve
`setPath()` ise `"difficulty.spawnDelay"` veya `"targets.2"` gibi noktalı bir yolla belirtilen tek bir
değeri:

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

- `JsonReadMode::Replace` (varsayılan), JSON'un bahsetmediği alanlara varsayılan değerlerini verir;
  `Merge` mevcut değerlerini korur.
- Okuma atomiktir: bir şey yanlışsa nesne değişmez. Her sorun `/position/1` gibi bir JSON Pointer ile
  bildirilir ve hiçbir alana karşılık gelmeyen üyeler `unknownKeys` içinde listelenir. Bozuk girdi asla
  assert tetiklemez.
- Birimi olan sayılar `{"deg": 60}` veya `{"%": 50}` gibi birim etiketli girdi kabul eder; değer saklanan
  birime dönüştürülür. Renk alanları sRGB hex metinlerini kabul eder; bunlar doğrusal değerlere
  dönüştürülür.
- `Entity` alanları bir `EntityResolver` üzerinden geçer. Sahne dosyaları indeks saklar; resolver
  olmadan Handle'lar yalnızca süreç içinde anlamlı sayılar olarak yazılır.

## Sürümler ve migration'lar

Kaydedilmiş verinin anlamı değiştiğinde tipin sürümünü artırın ve eski JSON'u dönüştüren adımları
kaydedin. `renamedFrom` yeniden adlandırılmış alanları kendisi halleder:

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

`ValueReadOptions::savedVersion` tipten eskiyse önce alanlar yeniden adlandırılır, ardından o sürümden
itibaren her adım sırayla çalışır; sahne dosyaları sürümü `types` tablolarından iletir. Daha yeni bir
sürümün kaydettiği veri, alanları bilindiği ölçüde bir uyarıyla yüklenir. Yalnızca nesnenin kendisi
migrate edilir, içindeki struct'lar değil.
