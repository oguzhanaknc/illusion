# Rendering

Render modülü GPU cihazını oluşturur, görüntüleri pencerelere sunar (present) ve ECS sahnelerini
fiziksel tabanlı Material'lerle çizer. Vulkan 1.3 (macOS'ta MoltenVK) üzerinde
[NVRHI](https://github.com/NVIDIA-RTX/NVRHI) ile kuruludur; oyun kodu GPU işlerini NVRHI command
list'leri ile kaydeder.

Header'lar: `#include <illusion/render/RenderDevice.hpp>`, `<illusion/render/Swapchain.hpp>`

!!! note "Geliştirme sürüyor"
    Sahne renderer'ı Mesh'leri PBR Material'ler, doğrudan ışıklar ve görüntü tabanlı aydınlatmayla
    çizer. Gölgeler, ambient occlusion ve post-processing renderer milestone'unun sonraki adımlarında
    gelecek (yol haritasına bakın).

## Gereksinimler

- Vulkan 1.3 destekli bir GPU sürücüsü (dynamic rendering, synchronization2, timeline semaphore).
  Güncel tüm NVIDIA, AMD ve Intel sürücüleri ile Mesa (CI'da kullanılan lavapipe yazılım renderer'ı
  dahil) bu koşulu karşılar.
- Derlemek veya çalıştırmak için Vulkan SDK **gerekmez**. Validation layer'larını elde etmek için
  kurabilirsiniz; motor, mevcut olduklarında Debug build'lerinde bunları otomatik olarak açar.

## Cihazı oluşturma

```cpp
illusion::RenderDeviceDesc desc;
desc.applicationName = "My Game";
auto device = illusion::RenderDevice::create(desc);
if (!device)
{
    return 1; // no suitable GPU; the reason is in the log
}
ILL_LOG_INFO("Game", "rendering on {}", device->gpu().name);
nvrhi::IDevice* gpu = device->device();
```

- Vulkan loader çalışma zamanında açılır; hiçbir şey Vulkan SDK'ya link edilmez.
- Birden fazla GPU uygun olduğunda `gpuPreference` harici (discrete, varsayılan) veya tümleşik
  (integrated) GPU'yu seçer.
- `headless = true` test'ler, araçlar ve sunucular için present desteği olmayan bir cihaz oluşturur.
  Aksi halde native bir `Platform` var olmalıdır; çünkü hangi surface extension'larının gerektiğine
  pencere sistemi karar verir.
- Aynı anda yalnızca bir `RenderDevice` var olabilir. Ondan önce tüm NVRHI nesnelerini ve swapchain'leri
  yok edin.

### Validation

`validation` (`ValidationMode::Default`: motor assertion'larla derlendiğinde, yani Debug ve CI
build'lerinde açık), kuruluysa Vulkan validation layer'ını ve NVRHI'nin kendi validation layer'ını açar. Mesajlar log'a yazılır; `validationErrorCount()` hataları
sayar. GPU test'leri ve örnek uygulama bu değer sıfır değilse başarısız olur.

## Bir pencereye present etme

```cpp
auto swapchain = illusion::Swapchain::create(*device, *window);
nvrhi::CommandListHandle commands = device->device()->createCommandList();

while (!window->shouldClose())
{
    platform->pollEvents();
    if (swapchain->beginFrame()) // false while the window is minimized
    {
        commands->open();
        commands->clearTextureFloat(swapchain->currentBackBuffer(), nvrhi::AllSubresources,
                                    nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
        // ... draw into swapchain->currentFramebuffer() ...
        commands->close();
        device->device()->executeCommandList(commands);
        swapchain->present();
    }
}
device->waitIdle();
```

- `beginFrame()`, `framesInFlight` (varsayılan 2) kare yuvasından biri boşalana kadar bekler, ardından
  bir back buffer alır. Pencere boyutu değiştiyse önce swapchain'i yeniden oluşturur.
- Ekran destekliyorsa back buffer'lar sRGB formatı kullanır; böylece shader'lar doğrusal (linear) renk
  yazar.
- `vsync` (varsayılan açık) FIFO present modunu seçer; vsync kapalıyken mailbox veya immediate mod
  kullanılır. `setVsync()` bunu bir sonraki karede değiştirir.

## Bir sahne çizme

`SceneRenderer`, `Transform` ve `MeshRenderer` Component'lerine sahip her Entity'yi, `DirectionalLight`
ve `PointLight` Entity'leri ile aydınlatarak çizer. `RenderResources` GPU'daki Mesh, Texture ve
Material'lerin sahibidir ve Component'lerin sakladığı küçük Handle'lar verir.

Header'lar: `<illusion/render/RenderResources.hpp>`, `<illusion/render/SceneRenderer.hpp>`,
`<illusion/render/Components.hpp>`, `<illusion/ecs/Transform.hpp>`

```cpp
illusion::RenderResources resources(*device);
illusion::SceneRendererDesc rendererDesc;
rendererDesc.colorFormat = swapchain->format();
auto renderer = illusion::SceneRenderer::create(*device, resources, rendererDesc);

illusion::World world;
const illusion::MeshHandle sphere = resources.createMesh(illusion::makeSphere(0.5f), "Sphere");
illusion::MaterialDesc gold;
gold.parameters.baseColorFactor = illusion::Vec4(1.0f, 0.77f, 0.34f, 1.0f);
gold.parameters.metallicFactor = 1.0f;
gold.parameters.roughnessFactor = 0.3f;
const illusion::MaterialHandle material = resources.createMaterial(gold);

world.create(illusion::Transform{}, illusion::MeshRenderer{sphere, 0, material});

illusion::Transform sun;
sun.rotation = glm::quatLookAt(glm::normalize(illusion::Vec3(-0.4f, -1.0f, -0.6f)), illusion::Vec3(0.0f, 1.0f, 0.0f));
world.create(sun, illusion::DirectionalLight{illusion::Vec3(1.0f), 3.0f});
```

Her karede kameradan bir `RenderView` oluşturun ve bir renk hedefine çizin:

```cpp
illusion::Transform camera;
camera.position = illusion::Vec3(0.0f, 1.0f, 4.0f);
const illusion::UVec2 size = swapchain->size();
const auto view = illusion::RenderView::fromCamera(camera, illusion::Camera{},
                                                   static_cast<float>(size.x) / static_cast<float>(size.y));
commands->open();
renderer->render(commands, swapchain->currentBackBuffer(), world, view, &jobs);
commands->close();
device->device()->executeCommandList(commands);
ILL_LOG_DEBUG("Game", "{} draw calls", renderer->stats().drawCalls);
```

- `MeshRenderer` bir Mesh'i, onun submesh'lerinden birini ve bir Material'i belirtir (atanmamış
  Material varsayılan beyaz, pürüzlü Material'i kullanır).
- Işıklar Transform'larının -z ekseni boyunca (`DirectionalLight`) veya konumlarından (`PointLight`,
  `range` mesafesinde sıfıra iner) yayılır. İlk 4 yönlü ve 16 noktasal ışık kullanılır.
- `render()` hedefi temizler, Entity'leri görüş frustum'una göre eler (verirseniz Job System
  üzerinde), Pipeline, Material ve Mesh'e göre sıralar ve aynı (Mesh, submesh, Material) üçlüsünün her
  dizisi için tek bir instanced draw call yapar. `stats()` adayları, görünen Entity'leri, draw
  call'ları ve üçgenleri raporlar.
- Renderer, hedefin boyutunu izleyen bir derinlik buffer'ına sahiptir. Çizdiğiniz her renk formatı için
  ayrı bir `SceneRenderer` oluşturun.
- sRGB formatlı renk hedefleri doğrusal renk alır; UNORM hedefler sRGB kodlanmış renk alır, böylece
  ikisi de aynı görünür.

### Material'ler ve gölgelendirme

Material'ler glTF 2.0 metallic-roughness modelini izler: base colour, metallic, roughness, normal,
occlusion ve emissive; her biri isteğe bağlı bir Texture'la birlikte bir çarpandır. Alpha modları
opak, mask (bir eşik değeriyle) ve blend'dir; çift yüzlü Material'ler arka yüz elemeyi kapatır.
Aydınlatma GGX dağılımı, yüksekliğe bağlı Smith görünürlüğü ve Schlick Fresnel kullanır; ardından
pozlama ve ACES tonemapping uygulanır. Dolaylı ışık bir `SkyLight`'tan gelir (aşağıya bakın).

Bir glTF modelini çizmek için onu `loadGltf()` ile yükleyin ([Asset'ler ve Modeller](assets.md)
sayfasına bakın), `RenderResources::uploadModel()` ile GPU'ya yükleyin ve Entity'lerini `spawnModel()`
ile oluşturun.

### Görüntü tabanlı aydınlatma

Bir `SkyLight`, sahneyi bir ortamla aydınlatır: çevredeki her şeyin equirectangular bir görüntüsü,
genellikle bir HDR fotoğraf. Ücretsiz ve yüksek kaliteli HDRI'ler (CC0)
[Poly Haven](https://polyhaven.com/hdris) adresinden `.hdr` dosyası olarak indirilebilir.

```cpp
std::vector<std::byte> bytes;
files.readFile("game:/skies/meadow_2k.hdr", bytes);
std::optional<illusion::Image> sky = illusion::decodeImage(bytes, /*srgb=*/false);
if (!sky)
{
    sky = illusion::makeSkyImage(1024); // procedural fallback
}
const illusion::EnvironmentHandle environment = resources.createEnvironment(*sky);

illusion::Transform orientation;
orientation.rotation = glm::angleAxis(glm::radians(90.0f), illusion::Vec3(0.0f, 1.0f, 0.0f));
world.create(orientation, illusion::SkyLight{environment, 1.0f, true});
```

- `createEnvironment()` görüntüyü GPU'da bir kez işler (bake): bir arka plan cube map'i, artan
  roughness için önceden filtrelenmiş bir specular cube map ve spherical harmonics olarak diffuse
  irradiance (CPU'da). Ortamları yükleme sırasında hazırlayın: 2K bir HDRI masaüstü bir makinede
  100 ms'den kısa sürer. `EnvironmentDesc` çözünürlükleri ve örnek sayısını belirler.
- Renderer ilk `SkyLight`'ı kullanır. Transform'u ortamı döndürür; `intensity` onu ölçekler ve
  `background = false` sahnenin arkasında temizleme rengini bırakır.
- `SkyLight` yoksa onun yerine `SceneRendererDesc::ambient` tekdüze bir ortam ışığı ekler.
- Metaller çevrelerini yansıtır; bu yüzden ancak bir ortamla doğru görünürler.
- Gölgelendirme enerjiyi korur: tekdüze beyaz bir ortamda her beyaz Material, roughness veya
  metalness değeri ne olursa olsun arka planla aynı görünür (GPU test'lerindeki "white furnace"
  testi).

### glTF görüntüleyici

`illusion_gltf_viewer` bir modeli yörünge kamerasıyla gösterir (döndürmek için sol fare sürükleme veya
sağ çubuk, yakınlaştırmak için fare tekerleği veya tetikler). Model verilmezse, metallic değeri soldan
sağa artan ve roughness değeri yukarıdan aşağıya azalan bir küre ızgarası gösterir. `--environment`,
sahneyi yerleşik prosedürel gökyüzü yerine bir HDR dosyasıyla aydınlatır. `--screenshot` son görüntüyü
1280x720 PNG olarak kaydeder.

```bash
./build/linux-clang/samples/Debug/illusion_gltf_viewer path/to/model.glb
./build/linux-clang/samples/Debug/illusion_gltf_viewer path/to/model.glb --environment meadow_2k.hdr
./build/linux-clang/samples/Debug/illusion_gltf_viewer --frames 60 --screenshot scene.png
```

## Koordinat kuralları

Renderer, Direct3D tarzı clip space'i izler: **y yukarıyı gösterir** ve derinlik 0'dan 1'e gider. NVRHI
bunu sağlamak için Vulkan viewport'unu çevirir; bu yüzden projeksiyon matrislerinde y çevirmesi
gerekmez. Texture koordinatlarının başlangıç noktası sol üst köşedir.

Dünya uzayı sağ elli ve y yukarıdır; kameralar kendi -z eksenleri boyunca bakar ve ön yüzler saat
yönünün tersindedir. Sahne renderer'ı sonsuz uzak düzlemli **reversed-Z** kullanır: yakın düzlem
derinlik 1'e, sonsuz ise 0'a eşlenir; bu, derinlik hassasiyetini mesafe boyunca neredeyse eşit tutar.
Projeksiyon matrislerini `glm::perspective()` ile değil, `perspectiveReversedZ()` (veya
`RenderView::fromCamera()`) ile oluşturun.

## Shader'lar

Shader'lar [Slang](https://shader-slang.org/) (HLSL uyumlu bir dil) ile yazılır ve build sırasında
SPIR-V'ye derlenir. Derlenen bytecode programa gömülür; bu yüzden dağıtılacak veya yüklenecek shader
dosyası yoktur.

```cmake
illusion_add_shaders(my_game
    NAMESPACE my_game::shaders
    HEADER my_game/Shaders.hpp
    SHADERS
        meshVertex shaders/Mesh.slang vertexMain vertex
        meshFragment shaders/Mesh.slang fragmentMain fragment)
```

Her satır bir C++ fonksiyon adını, kaynak dosyayı, entry point'i ve stage'i belirtir. Üretilen header
`std::span<const std::byte> meshVertex() noexcept;` gibi bildirimler içerir; bu byte'ları
`nvrhi::IDevice::createShader()`'a verin. Shader uyarıları hata sayılır ve build, `#include` edilen
dosyaları takip eder.

Slang derleyicisi otomatik olarak indirilir (sabitlenmiş sürüm). Başka bir `slangc` kullanmak için
`ILLUSION_SLANGC_EXECUTABLE` değişkenini ayarlayın.

## Tanılama

`DebugTriangle`, hiçbir buffer kullanmadan renkli bir üçgen çizer. `illusion_hello_triangle` örneği,
shader derlemesinden present'e kadar tüm yolu doğrulamak için bunu kullanır:

```bash
./build/linux-clang/samples/Debug/illusion_hello_triangle --frames 300
```
