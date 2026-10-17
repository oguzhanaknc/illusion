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
const illusion::ModelHandles handles = resources.uploadModel(*model);
const std::vector<illusion::Entity> entities = illusion::spawnModel(world, *model, handles);
```

- Mesh'ler submesh'lerini (glTF primitive'leri) korur; her birinin kendi Material'i ve sınırlayıcı
  kutusu vardır. Eksik normaller hesaplanır; tangent'lar varsa okunur, yoksa shader'da türetilir.
- Material'ler base colour, metallic-roughness, normal, occlusion ve emissive Texture'ları, alpha
  modları ve `KHR_materials_emissive_strength` ile metallic-roughness modelini kullanır.
- Görüntüler (PNG, JPEG) çözülür ve tam mip zincirleri oluşturulur. Renk Texture'ları (base colour,
  emissive) sRGB'dir; diğerleri doğrusaldır (linear).
- Node'lar varsayılan sahnenin hiyerarşisini korur; `ModelData::worldMatrices()` bunu çözer ve
  `spawnModel()` her mesh node'u ve submesh için isteğe bağlı bir kök matrisle bir Entity oluşturur.
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
index'li bir üçgen listesidir. `makeCube()`, `makeSphere()` ve `makePlane()` prototipler ve test'ler
için prosedürel Mesh'ler oluşturur; `computeNormals()` yumuşak normalleri doldurur.

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

Asset'ler, normalleştirilmiş sanal yolun kararlı bir hash'i olan `AssetId` ile tanımlanır:

```cpp
const illusion::AssetId id = illusion::AssetId::fromPath("game:/models/helmet.glb");
```

Hazırlanmış dosyalar (`<illusion/assets/AssetFormat.hpp>` içindeki `AssetFile`, `serializeImage()`,
`serializeModel()`) sürümlüdür, checksum içerir ve okunurken tamamen doğrulanır; bu yüzden hasarlı bir
önbellek dosyasına güvenilmez, yok sayılır.
