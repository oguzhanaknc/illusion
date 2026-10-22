# Asset'ler ve Modeller

Assets modülü dosyaları renderer'ın, araçların ve test'lerin kullanabileceği CPU tarafı verilere
dönüştürür: görüntüler, Mesh'ler ve tüm glTF modelleri. GPU'ya dokunmaz; veriyi `RenderResources`
yükler ([Rendering](rendering.md#bir-sahne-cizme) sayfasına bakın).

Header'lar: `#include <illusion/assets/Model.hpp>`, `<illusion/assets/Mesh.hpp>`,
`<illusion/assets/Image.hpp>`, `<illusion/assets/Environment.hpp>`

## Bir glTF modeli yükleme

[glTF 2.0](https://www.khronos.org/gltf/) motorun model formatıdır. `loadGltf()`, `.gltf` dosyalarını
(harici veya gömülü buffer'larla) ve ikili `.glb` dosyalarını [sanal dosya sistemi](filesystem.md)
üzerinden okur:

```cpp
illusion::VirtualFileSystem files;
files.mount("game", illusion::executableDirectory() / "data");

std::string error;
const std::optional<illusion::ModelData> model = illusion::loadGltf(files, "game:/models/helmet.glb", &error);
if (!model)
{
    ILL_LOG_ERROR("Game", "cannot load helmet: {}", error);
    return;
}
const std::vector<illusion::Entity> entities = illusion::spawnModel(world, *model, "game:/models/helmet.glb");
```

- Mesh'ler submesh'lerini (glTF primitive'leri) korur; her birinin kendi Material'i ve sınırlayıcı
  kutusu vardır. Eksik normaller hesaplanır; tangent'lar varsa okunur, yoksa shader'da türetilir.
- Material'ler base colour, metallic-roughness, normal, occlusion ve emissive Texture'ları, alpha
  modları ve `KHR_materials_emissive_strength` ile metallic-roughness modelini kullanır.
- Görüntüler (PNG, JPEG) çözülür ve tam mip zincirleri oluşturulur. Renk Texture'ları (base colour,
  emissive) sRGB'dir; diğerleri doğrusaldır (linear).
- Node'lar varsayılan sahnenin hiyerarşisini korur; `ModelData::worldMatrices()` bunu çözer.
  `spawnModel()`, isteğe bağlı bir kök matrisle Entity'lerden aynı [hiyerarşiyi](ecs.md#transform-hiyerarsisi)
  oluşturur: her node için `Name`, `Hierarchy`, `LocalTransform` ve `Transform` taşıyan bir Entity. Mesh'i
  tek submesh'li bir node, `<model>#mesh/N`'e başvuran bir `MeshRenderer` alır
  ([Asset referansları](#asset-referanslar)); birden çok submesh'li bir Mesh her submesh için
  `<node> (<submesh>)` adlı bir çocuk alır. Oluşturulan tüm Entity'leri, önce ebeveynler olmak üzere
  döndürür. World çizilirken modeli bir `RenderAssetResolver` yükler; `ModelHandles` alan `spawnModel()`
  ise kodla yüklenmiş bir model için `RenderMesh` Component'leri yazar (hemen çizilirler ama kaydedilmezler).
- Motorun desteklemediği zorunlu extension'lar (Draco, meshopt, KTX2 Texture'ları) bozuk görüntü
  çizmek yerine açık bir hatayla yüklemeyi başarısız kılar. `KHR_mesh_quantization` desteklenir.

Bir model içindeki göreli referanslar (`../textures/a.png`) kendi mount'u içinde çözülür; bir model
asla onun dışındaki dosyaları okuyamaz.

## Görüntüler

`Image`, RGBA piksellerini bir mip zinciriyle saklar: `RGBA8` (normal map gibi doğrusal veriler),
`RGBA8Srgb` (renkler) veya `RGBA32F` (HDR ortamlar). `decodeImage()` PNG, JPEG, TGA, BMP ve Radiance
HDR okur; `encodePng()` 8 bitlik görüntüleri yazar, örneğin ekran görüntüleri için. `readPixel()` formatı ne
olursa olsun herhangi bir pikseli doğrusal RGBA olarak döndürür.

```cpp
std::vector<std::byte> bytes;
files.readFile("game:/textures/rock.png", bytes);
std::optional<illusion::Image> image = illusion::decodeImage(bytes, /*srgb=*/true);
if (image)
{
    illusion::generateMips(*image);
    const illusion::TextureHandle texture = resources.createTexture(*image, "Rock");
}
```

`generateMips()` zinciri bir kutu filtresiyle oluşturur; sRGB görüntüler doğrusal uzayda filtrelenir,
böylece mip'ler kararmaz.

## Mesh'ler

`MeshData`, 48 byte'lık vertex'lere (konum, normal, tangent, UV), submesh'lere ve sınırlara sahip,
index'li bir üçgen listesidir. `makeCube()`, `makeSphere()`, `makePlane()`, `makeCapsule()` ve
`makeCylinder()` prototipler ve test'ler için prosedürel Mesh'ler oluşturur (`makeBuiltinMesh()`
`builtin:/mesh/...` Asset'lerini üretir); `computeNormals()` yumuşak normalleri doldurur.

```cpp
illusion::MeshData ground = illusion::makePlane(20.0f);
const illusion::MeshHandle groundMesh = resources.createMesh(ground, "Ground");
```

Tüm Mesh'ler sağ elli, y yukarı bir uzayda saat yönünün tersindeki ön yüzleri kullanır.

## Ortamlar

Header: `<illusion/assets/Environment.hpp>`

Ortam görüntüleri Poly Haven ve çoğu HDRI kütüphanesinin equirectangular düzenini kullanır: görüntünün
merkezi -z yönüne (döndürülmemiş bir kameranın baktığı yöne) bakar, u değeri +x yönüne doğru artar ve
en üst satır tam yukarıyı gösterir. `decodeImage()`, Radiance `.hdr` dosyalarını `RGBA32F`
görüntülere okur.

```cpp
const illusion::Vec3 front = illusion::equirectDirection(illusion::Vec2(0.5f, 0.5f)); // (0, 0, -1)
const illusion::ShIrradiance irradiance = illusion::computeShIrradiance(*sky);
const illusion::Vec3 fromAbove = irradiance.evaluate(illusion::Vec3(0.0f, 1.0f, 0.0f));
```

- `computeShIrradiance()` bir ortamı, kosinüs konvolüsyonu uygulanmış 9 spherical harmonics
  katsayısına izdüşürür; böylece `evaluate()` bir yöne bakan yüzeydeki irradiance değerini döndürür.
  `RenderResources::createEnvironment()` bunu sizin için çağırır.
- `makeSkyImage()` prototipler, test'ler ve yedek olarak prosedürel bir gökyüzü (zenitten ufka bir
  geçiş, zemin ve isteğe bağlı bir güneş diski) oluşturur.

## Asset referansları

Component'ler Asset'lere `AssetRef` ile başvurur; böylece sahneler çizdiklerini, çaldıklarını ve
çarpıştıklarını kaydeder
([ADR 0035](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0035-asset-references.md)). Bir
`AssetRef` bir yolun `AssetId`'sini tutar; süreç genelindeki `AssetPathTable` yolu hatırlar ve sahne
dosyaları yolu metin olarak saklar.

Header: `<illusion/core/AssetRef.hpp>`

```cpp
illusion::MeshRenderer ship;
ship.mesh = illusion::AssetRef("game:/models/ship.glb#mesh/0");
ship.material = illusion::AssetRef("game:/materials/hull.material");
world.create(illusion::Transform{}, ship);

ILL_LOG_INFO("Game", "{}", ship.mesh.path()); // game:/models/ship.glb#mesh/0
```

- Yollar kaydedilirken normalleştirilir (`game:/a/../b.png`, `game:/b.png` olur); bozuk bir yol boş bir
  referans verir.
- JSON'da bir `AssetRef` yolunun kendisidir, yoksa `""`; okurken `null` ve `{"path": "..."}` da kabul edilir.
- Bir referansı diğer alanlar gibi reflect edin; `FieldMeta::assetKind`, editörlere hangi tür Asset'in
  uyduğunu söyler.
- Asset'leri resolver'lar yükler: `MeshRenderer` ve `SkyLight` için `RenderAssetResolver`
  ([Rendering](rendering.md#meshrenderer-ve-rendermesh)), çarpışma Mesh'leri için `PhysicsAssetResolver`
  ([Fizik](physics.md#mesh-ve-convex-hull-colliderlar)); `AudioSource::clip`'i `AudioWorld` yükler.
- Eksik bir dosyaya başvuran bir sahne yine yüklenir ve referansı korur; `SceneLoadResult::missingAssets`
  onu listeler ([Sahneler](scenes.md#sahneleri-kaydetme-ve-yukleme)).
- Paylaşımlı [oyun modülleri](modules.md) host'un tablosunu kullanır; böylece referansları host'ta çözülür.

### Alt Asset'ler ve yerleşik Asset'ler

`#` sonrasındaki bir parça, bir modelin bir kısmını seçer:

| Yol | Asset |
| --- | --- |
| `game:/ship.glb` | modelin tamamı (Mesh olarak: ilk Mesh'i) |
| `game:/ship.glb#mesh/2` | 2 numaralı Mesh |
| `game:/ship.glb#material/0` | 0 numaralı Material |
| `game:/ship.glb#node/Hull` | `Hull` adlı node'un Mesh'i |

Ayrılmış `builtin:` mount'u dosya gerektirmeyen Asset'ler sağlar; `VirtualFileSystem` onu mount etmeyi reddeder:

| Yol | Asset |
| --- | --- |
| `builtin:/mesh/cube`, `sphere`, `plane`, `capsule`, `cylinder` | varsayılan boyutlarla `makeCube()` ... `makeCylinder()` |
| `builtin:/material/default` | varsayılan Material (beyaz, pürüzlü, metal olmayan) |
| `builtin:/sky/procedural` | Entity'nin `ProceduralSky`'ıyla şekillenen bir `makeSkyImage()` gökyüzü |

### Material Asset'leri

Bir `.material` dosyası bir Material'i model olmadan tanımlar; böylece birden çok Mesh onu paylaşabilir
ve bir `MeshRenderer` bir modelin Material'ini değiştirebilir:

```json
{
  "format": "illusion.material",
  "version": 1,
  "material": {"baseColorFactor": [0.8, 0.1, 0.1, 1], "roughnessFactor": 0.4, "metallicFactor": 0},
  "textures": {"baseColor": "game:/textures/paint.png", "normal": "game:/textures/paint_n.png"}
}
```

- `material`, `MaterialData` alanlarını adlarıyla tutar; eksik alanlar varsayılanlarını korur.
  `...Texture` üyeleri yalnızca sarma (wrap) modlarını belirler.
- `textures` görüntüleri adlandırır: `baseColor` ve `emissive` sRGB olarak, `metallicRoughness`, `normal`
  ve `occlusion` doğrusal veri olarak yüklenir.
- `parseMaterialAsset()` ve `writeMaterialAsset()` dosyaları okur ve yazar (önce bir kez
  `reflectAssetStructs()` çağırın); `materialAssetDecoder()` onları `AssetStreamer::loadFile()` ile
  akışla yükler.
- `cookAssets()` Material dosyalarını denetler ve Texture'larını bu ayarlarla cook eder. `.material`
  dosyalarının kendisi doğrudan okunur; onları oyunun dosyalarıyla birlikte gönderin.

Header: `<illusion/assets/MaterialAsset.hpp>`

## Asset veritabanı

PNG'leri çözmek, mip zincirleri oluşturmak ve glTF dosyalarını dönüştürmek zaman alır. `AssetDatabase`
bunu her kaynak dosya için bir kez yapar ve sonucu bir önbellekte tutar; sonraki yüklemeler bunun yerine
hazırlanmış (cooked) dosyayı okur.

Header: `<illusion/assets/AssetDatabase.hpp>`

```cpp
illusion::VirtualFileSystem files;
files.mount("game", illusion::executableDirectory() / "data");
files.mount("cache", illusion::userDataDirectory("MyStudio", "MyGame") / "cache",
            illusion::MountAccess::ReadWrite);

illusion::AssetDatabase assets(files, "cache");
std::optional<illusion::ModelData> helmet = assets.loadModel("game:/models/helmet.glb");

illusion::ImageImportSettings normalMap;
normalMap.srgb = false;
std::optional<illusion::Image> bumps = assets.loadImage("game:/textures/rock_normal.png", normalMap);
```

- Önbellek mount'u yazılabilir olmalıdır. Önbellek mount adı boş verilirse her yükleme kaynaktan içe
  aktarır.
- Bir önbellek kaydı, Asset'in geldiği her dosyanın içerik hash'ini hatırlar (glTF için: model, buffer'ları
  ve görüntüleri). Bunlardan biri değiştiğinde sonraki yükleme yeniden içe aktarır ve kaydı yeniler; elle
  hiçbir şeyin temizlenmesi gerekmez.
- Her içe aktarma ayarı kendi kaydını alır; böylece aynı görüntü hem sRGB renk hem de doğrusal veri olarak
  yüklenebilir.
- Mip'leriyle birlikte önbellekteki 512x512 bir Texture'ı yüklemek, PNG'yi çözmekten yaklaşık on kat
  hızlıdır.
- `statistics()` önbellek isabetlerini, içe aktarmaları ve önbellek yazmalarını sayar.

Asset'ler, normalleştirilmiş sanal yolun kararlı bir hash'i olan `AssetId`
(`<illusion/core/AssetId.hpp>`) ile tanımlanır:

```cpp
const illusion::AssetId id = illusion::AssetId::fromPath("game:/models/helmet.glb");
```

Hazırlanmış dosyalar (`<illusion/assets/AssetFormat.hpp>` içindeki `AssetFile`, `serializeImage()`,
`serializeModel()`) sürümlüdür, checksum içerir ve okunurken tamamen doğrulanır; bu yüzden hasarlı bir
önbellek dosyasına güvenilmez, yok sayılır.

## Streaming

`AssetStreamer` arka planda yükler; böylece Asset'ler gelirken oyun çalışmaya devam eder.
`AssetDatabase` ile aynı içe aktarma önbelleğini kullanır.

Header'lar: `<illusion/assets/AssetStreamer.hpp>`, `<illusion/render/UploadQueue.hpp>`

```cpp
illusion::AssetStreamer streamer(files, jobs); // the import cache defaults to the "cache" mount
illusion::UploadQueue uploads(resources);

const illusion::AssetHandle<illusion::ModelData> helmet = streamer.loadModel("game:/models/helmet.glb");
const std::shared_ptr<const illusion::ModelUpload> upload = uploads.upload(helmet);
bool spawned = false;

while (running)
{
    streamer.update();          // publishes finished loads
    uploads.process(16 << 20);  // at most ~16 MiB of GPU uploads this frame
    if (upload->uploaded && !spawned)
    {
        illusion::spawnModel(world, helmet.value(), upload->handles);
        spawned = true;
    }
    if (helmet.state() == illusion::AssetState::Failed)
    {
        ILL_LOG_ERROR("Game", "{}", helmet.error());
    }
    // ... render ...
}
```

- `loadImage()` ve `loadModel()` hemen bir handle döndürür. Bir handle, `update()` sonucunu `Ready`
  (bu durumda `value()` veriyi döndürür) veya `Failed` (bu durumda `error()` nedenini söyler) olarak
  yayımlayana kadar `Loading` durumundadır. Durumlar yalnızca `update()` içinde değişir; böylece bir
  handle asla bir karenin ortasında değişmez.
- Aynı dosya (ve aynı içe aktarma ayarları) için yapılan istekler tek bir handle'ı paylaşır. Son handle
  ortadan kalktığında veri serbest bırakılır.
- Ayrılmış bir I/O thread'i dosyaları okur; Job System önbelleği denetler, çözer ve deserialize eder. Bir
  glTF dosyasının ilk kez içe aktarılması I/O thread'inde gerçekleşir.
- `UploadQueue`, GPU yüklemelerini karelere yayar: `process()`, yüklenmiş modelleri istek sırasıyla byte
  bütçesi bitene kadar yükler, ancak her çağrıda en az bir tane yükler.
- `waitIdle()`, istenen her şey yayımlanana kadar bekler: yükleme ekranları ve araçlar için kullanışlıdır.
- Streamer'ı yok etmek, başlamamış yüklemeleri atlar, çalışanları bitirir ve onların önbellek
  kayıtlarını yazar.

### Diğer dosya türleri

`loadFile()`, başka modüllerin çözdüğü dosyaları stream eder. Dosyayı I/O thread'i okur, sizin
decoder'ınız Job System üzerinde çalışır; bu yüzden herhangi bir thread'den çağrılmaya uygun olmalıdır:

```cpp
// Any file, turned into any type on the Job System; the audio module loads its clips this way.
const illusion::AssetHandle<std::string> credits = streamer.loadFile<std::string>(
    "game:/text/credits.txt", "game.text",
    [](std::vector<std::byte>&& bytes, std::string& error) -> std::optional<std::string> {
        if (bytes.empty())
        {
            error = "no credits";
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    });
```

Aynı yol ve decoder adıyla yapılan istekler bir handle'ı paylaşır; bu yüzden ad, decoder'ı, ayarlarını ve
sonuç türünü tanımlamalıdır. İçe aktarma önbelleği kullanılmaz. Okunamayan, boş olan ya da decoder'ın
reddettiği bir dosya, nedeni `error()` içinde olacak şekilde `Failed` olur.

## Hazırlanmış Asset'leri dağıtma

Dağıtılan bir oyunun kaynak görüntülerine ve modellerine ihtiyacı yoktur: bunları önceden hazırlayın
(cook) ve içe aktarma önbelleğini bir arşiv olarak dağıtın.

```bash
illusion_pack --cook game data/ build/cooked.illpak
```

`--cook`, `game` olarak mount edilen `data/` altındaki her görüntüyü ve glTF modelini içe aktarır
(oyununuzun kullandığı mount adını verin) ve oluşan önbelleği paketler. Oyunda arşivi önbellek olarak
mount edin ve kaynak doğrulamayı kapatın:

```cpp
illusion::VirtualFileSystem files;
files.mountArchive("cache", illusion::executableDirectory() / "cooked.illpak");

illusion::AssetStreamerDesc desc;
desc.verifySources = false; // trust the cooked archive; the sources are not shipped
illusion::AssetStreamer streamer(files, jobs, desc);
auto helmet = streamer.loadModel("game:/models/helmet.glb"); // served from the cooked archive
```

- İstekler geliştirme sırasındaki sanal yolların aynısını kullanır; `game` mount'unun kendisinin var
  olması gerekmez.
- Tek başına görüntüler varsayılan içe aktarma ayarlarıyla hazırlanır; başka ayarlarla yüklenen
  görüntüler kaynaklarına (ya da bu ayarları bilen bir hazırlama adımına) ihtiyaç duyar.
- `cookAssets()` aynısını koddan yapar ve neyi hazırladığını ve hangi dosyaların başarısız olduğunu
  bildirir.
