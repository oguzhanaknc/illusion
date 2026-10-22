# Sahneler

Sahne dosyaları Entity'leri ve Component'lerini JSON olarak kaydeder. Yansıtılmış Component'leri (bkz.
[Reflection](reflection.md)) C++ tip adları yerine kararlı tip ve alan adlarıyla saklarlar; çünkü C++ tip
adları derleyiciden derleyiciye farklıdır ve kod yeniden düzenlendiğinde değişir.

Header'lar: `<illusion/ecs/Reflection.hpp>`, `<illusion/ecs/SceneSerializer.hpp>`,
`<illusion/ecs/EntityGuid.hpp>`, `<illusion/ecs/Name.hpp>`, `<illusion/core/Version.hpp>`

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
- Alanlar sayı, vektör, metin, Entity, enum, flag, yansıtılmış struct, dizi, optional ve JSON değeri
  olabilir; [Reflection](reflection.md#alan-tipleri) bunları JSON biçimleriyle listeler. Diğer tipler
  derlenmez.
- Yansıtılan Component'ler varsayılan olarak oluşturulabilir (default constructible) ve kopyalanabilir
  olmalıdır. Boş struct'lar (tag'ler) da yansıtılabilir.
- Bir tipi yeniden yansıtmak önceki yansıtmanın yerini alır. Tipleri, diğer thread'ler reflection
  verisini okumadan önce, başlangıçta yansıtın.
- İsteğe bağlı sürüm (`reflect<T>("game.Spinner", 2)`) sahne dosyalarına yazılır. Eski veri yüklenirken
  tipin [migration'ları](reflection.md#surumler-ve-migrationlar) çalışır; farklı bir sürüm ayrıca bir
  uyarı üretir.

## Sahneleri kaydetme ve yükleme

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

- `saveScene(world)` canlı her Entity'yi kaydeder. `SceneSaveOptions::subset` bunun yerine bir listeyi
  kaydeder (örneğin bir seçimi veya bir prefab'ı); listenin dışını gösteren Entity alanları `null` olarak
  kaydedilir.
- Kaydedilen her Entity'nin bir `EntityGuid`'i olmalıdır (bkz. [Entity id'leri](#entity-idleri));
  birinde yoksa veya ikisi aynı id'yi paylaşıyorsa kaydetme başarısız olur. Entity'ler id sırasıyla
  kaydedilir; böylece aynı sahne her zaman aynı dosyayı verir. `SceneOrder::AsGiven` alt kümenin sırasını,
  alt küme yoksa `World::entities()` sırasını korur.
- Yalnızca yansıtılmış Component'ler kaydedilir; `SceneSaveResult::skippedComponents` diğerlerini ve
  transient olarak yansıtılan, hiçbir zaman kaydedilmeyen çalışma zamanı durumunu (`PhysicsBody`,
  `AudioPlayback`, ...) sayar. `MeshRenderer` ve `SkyLight` GPU kaynaklarına başvurur ve henüz
  kaydedilmez (sahnelerdeki Asset başvuruları editörle birlikte gelecek).
- Bir kayan noktalı alan sonsuz veya NaN ise kaydetme başarısız olur, çünkü JSON bunu saklayamaz.
- `loadScene()` sahnenin Entity'lerini World'e ekler ve onları dosyadaki sırayla döndürür. Entity
  alanları yeni Entity'lere eşlenir. Dosya geçersizse hiçbir şey eklenmez ve `error` sorunun nerede
  olduğunu söyler; örneğin `entity 0: field 'quat' of 'test.Everything' must be a number (at
  /quat/3)`.
- Yükleme kod değişikliklerine toleranslıdır: dosyada olmayan alanlar varsayılan değerlerini korur;
  bilinmeyen alanlar ve çalışma zamanı Component tipleri bir uyarıyla atlanır; bilinmeyen tiplerdeki
  Component'ler [korunur](#bilinmeyen-tipler).

## Entity id'leri

Bir `Entity` handle'ı yalnızca onu oluşturan World ve süreç içinde geçerlidir. Kaydedilen Entity'lerin
bunun yerine kalıcı bir id'si vardır: `EntityGuid` Component'i (`illusion.EntityGuid`, editörde salt
okunur). Sahne dosyaları, editörün geri alma geçmişi ve Editor commands Entity'lere bu id ile başvurur:

```cpp
illusion::World world;
const illusion::Entity player = world.create(illusion::Transform{}, illusion::Name{"Player"});
illusion::assignMissingGuids(world); // every entity without an id gets one

const illusion::EntityGuid id = world.get<illusion::EntityGuid>(player);
ILL_LOG_INFO("Game", "player id: {}", illusion::toString(id)); // e.g. 3f9a0c1e5b7d2a40

const illusion::GuidEntityResolver ids(world);
const illusion::Entity found = ids.find(id); // == player
```

- Id'ler, 16 küçük harfli onaltılık rakamla yazılan 64 bitlik sayılardır; `0` "id yok" demektir.
  `parseGuid()` metin biçimini geri okur.
- Id'leri `GuidGenerator` üretir. Varsayılan üreteç anahtarını işletim sisteminin güvenli rastgele
  kaynağından alır; böylece farklı oturumlarda ve makinelerde üretilen id'ler çakışmaz, tek bir üreteç de
  bir id'yi asla tekrarlamaz. `GuidGenerator(seed)` her platformda aynı id'leri verir; testler ve
  tekrarlanabilir araçlar içindir. `GuidGenerator::shared()` süreç genelindeki üreteçtir; `next()` birden
  çok thread'den çağrılabilir.
- `GuidEntityResolver` bir World'ün `EntityResolver`'ıdır (bkz. [Reflection](reflection.md#json)): onunla
  `writeValue()` ve `readValue()` Entity alanlarını id olarak yazar ve geri okur. Yapısal değişikliklerden
  sonra `refresh()` çağırın.

Yüklenen Entity'lerin hangi id'leri alacağına `SceneLoadOptions::ids` karar verir:

- `IdPolicy::Keep` (varsayılan; bir sahneyi açmak için) dosyadaki id'leri korur. World'deki bir
  Entity'nin zaten sahip olduğu bir id yükleme hatasıdır.
- `IdPolicy::Regenerate` (bir sahneyi yapıştırmak veya birden çok kez örneklemek için) her Entity'ye yeni
  bir id verir. Dosyanın içindeki başvurular her iki durumda da yeniden eşlenir ve
  `SceneLoadResult::fileIds` her Entity'nin dosyadaki id'sini tutar:

```cpp
const illusion::Entity selection[] = {door, frame};
const illusion::SceneSaveResult copied = illusion::saveScene(world, {.subset = selection});

const illusion::SceneLoadResult pasted =
    illusion::loadScene(world, copied.json, {.ids = illusion::IdPolicy::Regenerate});
// pasted.fileIds[i] is the id that pasted.entities[i] had in the copied text.
```

Tek bir dosyadaki yinelenen id'ler yükleme hatasıdır.

## Adlar

`Name` (`illusion.Name`), editörün gösterdiği isteğe bağlı bir etikettir; adların benzersiz olması
gerekmez. Bir ad en fazla 256 bayt UTF-8'dir ve `/` (Entity yolları için ayrılmıştır) ile kontrol
karakterleri içermez. Bir sahne dosyasından veya bir editör komutundan Name okunurken diğer adlar alanın
doğrulayıcısıyla (`FieldMeta::validate`, bkz. [Reflection](reflection.md#metadata)) reddedilir;
`isValidEntityName()` ve `entityNameError()` adları kodda denetler.

## Bilinmeyen tipler

Bir sahne, kayıtlı olmayan tiplerde Component'ler içerebilir; örneğin bir oyun modülü yüklü değilken. Bunlar
kaybolmaz: yükleme JSON'larını ve kaydedildikleri sürümü, hiçbir motor System'inin sorgulamadığı çalışma
zamanı Component'i `UnknownComponents` içinde saklar; kaydetme de onları değiştirmeden geri yazar. Böylece
bir sahneyi açıp kaydetmek başka bir modülün verisini asla düşürmez. Tipleri bilinmediği için içlerindeki
başvurular `IdPolicy::Regenerate` ile yeniden eşlenmez. Bilinen tiplerin bilinmeyen alanları ise
[yeniden adlandırmalar](reflection.md#surumler-ve-migrationlar) uygulandıktan sonra yine bir uyarıyla
atlanır.

## Ayarlar

Bir sahne, Component olmayan yansıtılmış struct'ları (bkz. [Reflection](reflection.md)), örneğin render
veya hava durumu ayarlarını, bir `"settings"` nesnesinde taşıyabilir:

```cpp
game::Weather weather; // reflected with reflectStruct<game::Weather>("game.Weather")
const illusion::SceneSettings settings[] = {illusion::sceneSettings(weather)};
const illusion::SceneSaveResult saved = illusion::saveScene(world, {.settings = settings});

game::Weather loadedWeather;
const illusion::SceneSettingsTarget targets[] = {illusion::sceneSettingsTarget(loadedWeather)};
const illusion::SceneLoadResult loaded = illusion::loadScene(otherWorld, saved.json, {.settings = targets});
```

Hedefler yalnızca sahnenin tamamı yüklenirse değişir. Hiçbir hedefin istemediği ayarlar, bilinmeyen
tiplerdekiler dahil, `SceneLoadResult::preservedSettings` içinde döner; yeniden kaydetmek için onları
`SceneSaveOptions::preservedSettings`'e verin.

## Dosya biçimi

Sahne dosyaları iki boşluk girintili UTF-8 JSON'dur; böylece sürüm kontrolünde iyi diff'lenir ve
birleştirilir:

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

- Her Entity, `id`'si ve kararlı ada göre sıralanmış `components`'idir. `types` tablosu dosyadaki her
  Component ve ayar tipinin sürümünü listeler.
- `rotation`, glTF'in kullandığı sırayla `[x, y, z, w]` olarak yazılır.
- Enum değerleri adlarıyla yazılır; adı olmayan değerler sayı olarak yazılır.
- Bir Entity alanı, dosyadaki bir Entity'nin `id`'sidir ya da `null`'dır.
- Değerler [reflection](reflection.md#json) JSON biçimlerini kullanır. Sahneler `writeValue()` ve
  `readValue()` ile yazılıp okunur; böylece bir Component'in JSON'u sahne dosyasında ve editör
  komutlarında aynıdır.
- Kayan noktalı sayılar en kısa kesin biçimleriyle yazılır; böylece bit bit aynı olarak geri yüklenir.

### Sürüm 1 dosyaları

Entity id'lerinden önce yazılan sahne dosyaları (`"version": 1`) hâlâ yüklenir. Entity'lerinin id'si
olmadığı için yeni id alırlar; `entities` dizisine indeks olan Entity alanları yeniden eşlenir. Sonraki
kaydetme sürüm 2 yazar. Sürüm 1 dosyalarında her Entity nesnesi Component'lerini doğrudan tutar ve `u64`
değerleri sayıdır.

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

`World::forEachMatching(all, none, fn)`, `ComponentId` ile bir Query'dir: `all` içindeki her
Component'e sahip olup `none` içindekilerin hiçbirine sahip olmayan Entity'lerin her Chunk'ı için `fn`'i
bir kez, handle'ları ve ham sütun pointer'larıyla çağırır:

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

Boyutu tipe bağlı olan enum alanları için `readEnumField()` ve `writeEnumField()`, herhangi bir alan için
ise `readField()`, `writeField()` ve [Reflection](reflection.md) sayfasındaki JSON fonksiyonlarını
kullanın.

Program dışından gelen id'ler (bir dosya, bir araç, uzaktan bir komut) geçersiz olabilir ve
`registry.info(id)` gibi fonksiyonlar bu durumda assert tetikler. Önce `registry.contains(id)` ile kontrol
edin veya bilinmeyen bir id için `nullptr` döndüren `registry.tryInfo(id)`'yi kullanın.

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
