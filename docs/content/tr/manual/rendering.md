# Rendering

Render modülü GPU cihazını oluşturur, görüntüleri pencerelere sunar (present) ve ECS sahnelerini
fiziksel tabanlı Material'lerle çizer. Vulkan 1.3 (macOS'ta MoltenVK) üzerinde
[NVRHI](https://github.com/NVIDIA-RTX/NVRHI) ile kuruludur; oyun kodu GPU işlerini NVRHI command
list'leri ile kaydeder.

Header'lar: `#include <illusion/render/RenderDevice.hpp>`, `<illusion/render/Swapchain.hpp>`

!!! note "Geliştirme sürüyor"
    Sahne renderer'ı Mesh'leri PBR Material'ler, doğrudan ve görüntü tabanlı aydınlatma, yumuşak
    gölgeler, ambient occlusion, post-processing, GPU occlusion culling ve bindless multi-draw rendering
    ile çizer. Sırada streaming'li Asset hattı var (yol haritasına bakın).

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
- `render()` Entity'leri eler ([Culling](#culling) bölümüne bakın), Pipeline, Material ve Mesh'e göre
  sıralar ve aynı (Mesh, submesh, Material) üçlüsünün her dizisini geçiş başına tek bir instanced draw
  ile çizer. `stats()` adayları, frustum içindeki Entity'leri, draw call'ları ve üçgenleri raporlar.
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

### Culling

`Inactive` etiketli Entity'ler (kendileri veya bir ataları [Disabled](ecs.md#disabled-ve-inactive)) çizilmez;
ışıkları ve gökyüzü ışıkları da etki etmez. Renderer her karede iki adımda eleme yapar:

1. CPU'da, Job System üzerinde paralel olarak **frustum culling**: görüşün (ve tüm gölge
   cascade'lerinin) dışındaki Entity'ler atlanır.
2. Opak ve alpha-mask'li Mesh'ler için GPU'da **occlusion culling**: başka nesnelerin arkasında
   kalan nesneler çizilmez. İki aşamada çalışır. Önce nesneler önceki karenin derinliğine karşı test
   edilir ve geçenler depth prepass'e çizilir. Ardından kalan nesneler bu yeni derinliğin bir derinlik
   piramidine karşı test edilir ve sonuçta görünürlerse çizilir. Böylece görüşe giren bir nesne aynı
   karede çizilir; hiçbir şey geç belirmez.

```cpp
renderer->setOcclusionCulling(true); // the default
renderer->render(commands, target, world, view, &jobs);
// ... after the GPU finished the frame (for example in a statistics overlay one frame later):
ILL_LOG_INFO("Game", "{} of {} opaque instances drawn", renderer->readGpuDrawnInstances(),
             renderer->stats().visible);
```

- GPU, indirect draw'ların instance sayılarını kendisi yazar; bu yüzden CPU, culling sonuçlarını
  beklemez. `readGpuDrawnInstances()` bunları araçlar ve istatistikler için geri okur; yalnızca GPU
  kareyi bitirdikten sonra çağırın.
- Occlusion culling, çok sayıda nesne ve gerçek engeller (binalar, arazi) olduğunda kazandırır. Birkaç
  büyük nesneden oluşan sahnelerde `setOcclusionCulling(false)` derinlik piramidi geçişlerinden
  tasarruf sağlar.
- Blend Mesh'ler ve gölge düşürenler yalnızca frustum'a göre elenir.

### Bindless rendering

Destekleyen GPU'larda renderer tüm opak Mesh'leri birkaç çağrıyla çizer:

- Tüm Mesh'ler tek bir vertex havuzunu ve tek bir index havuzunu paylaşır; bu yüzden Mesh değiştirmek
  yeni buffer gerektirmez.
- Tüm Material'ler tek bir tablodaki kayıtlardır ve tüm Texture'lar tek bir bindless descriptor
  dizisindedir; böylece bir draw Material'leri karıştırabilir: her instance kendi Material'ini belirtir.
- Aynı Pipeline'a (opak veya alpha-mask'li, tek veya çift yüzlü) sahip batch'ler, geçiş başına tek bir
  multi-draw indirect çağrısıyla çizilir.

```cpp
const illusion::GpuInfo& gpu = device->gpu();
ILL_LOG_INFO("Game", "bindless: {}, multi-draw indirect: {}", gpu.bindless, gpu.multiDrawIndirect);

illusion::SceneRendererDesc desc;
desc.bindless = true; // the default; set false to force the per-material path
auto renderer = illusion::SceneRenderer::create(*device, resources, desc);
// After the first render():
ILL_LOG_INFO("Game", "rendering bindless: {}", renderer->usesBindless());
```

- Descriptor indexing ve multi-draw indirect gerektirir (`GpuInfo::bindless`,
  `GpuInfo::multiDrawIndirect`); güncel NVIDIA, AMD ve Intel sürücülerinde ve Mesa'da mevcuttur. Diğer
  durumlarda veya `SceneRendererDesc::bindless = false` ile her batch kendi Material bağlamalarıyla
  çizilir; görüntü aynıdır.
- En fazla `kBindlessTextureCapacity` (16384) Texture kullanılabilir.
- Blend Mesh'ler ve gölge düşürenler her iki modda da batch başına çizilir.

### Gölgeler

`castShadows` ayarı açık olan (varsayılan) ilk `DirectionalLight`, opak veya alpha-mask'li her Mesh'ten
yumuşak gölgeler düşürür. Blend Material'ler gölge düşürmez.

```cpp
illusion::Transform sun;
sun.rotation = glm::quatLookAt(glm::normalize(illusion::Vec3(-0.4f, -1.0f, -0.6f)), illusion::Vec3(0.0f, 1.0f, 0.0f));
illusion::DirectionalLight light{illusion::Vec3(1.0f, 0.95f, 0.9f), 3.0f};
light.angularDiameter = 0.02f; // a little softer than the real sun
world.create(sun, light);

illusion::ShadowSettings shadows = renderer->shadowSettings();
shadows.maxDistance = 120.0f;
shadows.resolution = 4096;
renderer->setShadowSettings(shadows);
```

- Gölgeler, görüşü `maxDistance` mesafesine kadar kaplayan en fazla dört cascade kullanır; her birinin
  kendi `resolution`² boyutunda shadow map'i vardır. Yakın cascade'ler daha küçüktür; bu yüzden yakındaki
  gölgeler daha keskindir. Cascade'ler, kamera hareket ettiğinde veya döndüğünde kenarları titremeyecek
  şekilde yerleştirilir.
- `angularDiameter`, ışığın radyan cinsinden görünür boyutudur (güneş yaklaşık 0.0093). Gölgeler,
  gölgeyi düşüren nesnenin yüzeye değdiği yerde keskindir ve uzaklaştıkça gerçek gölgeler gibi
  yumuşar (percentage-closer soft shadows). Yarı gölgeler bir cascade genişliğinin %5'iyle sınırlıdır.
- Görüş dışındaki nesneler de ışığa doğru `casterDistance` mesafesine kadar görüşe gölge düşürür.
- Aydınlık yüzeylerde çizgiler (shadow acne) görürseniz `depthBias` veya `normalBias` değerini
  artırın; gölgeler nesnelerden kopuyorsa azaltın.
- `stats()`, `shadowCasters` ve `shadowDrawCalls` değerlerini raporlar; `shadowCascades()` son karenin
  cascade'lerini döndürür.

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

### HDR ve post-processing

Renderer, sahneyi dahili bir HDR görüntüye çizer ve her kareyi post-processing ile bitirir:

1. Opak ve alpha-mask'li Mesh'ler için bir depth prepass; bu, gölgelendirmedeki overdraw'ı da ortadan
   kaldırır.
2. **Ambient occlusion** (GTAO), depth buffer'dan hesaplanır ve kıvrımlarda ve temas noktalarında
   dolaylı ışığı karartır.
3. Gölgelendirme, gökyüzü, ardından blend Mesh'ler.
4. **Temporal anti-aliasing** (isteğe bağlı), kamerayı her karede bir pikselin küçük bir kesri kadar
   kaydırıp kareleri biriktirerek kenarları ve yumuşak gölgelerin gürültüsünü yumuşatır.
5. **Bloom**, parlak piksellerden ışığı çevreye yayar.
6. Pozlama (`Camera::exposure`), ACES tonemapping ve UNORM hedefler için sRGB kodlaması;
   sonuç `render()`'a verilen hedefe yazılır.

```cpp
illusion::PostProcessSettings post = renderer->postProcessSettings();
post.ambientOcclusion.radius = 1.0f;
post.bloom.intensity = 0.06f;
post.temporalAntiAliasing.enabled = true;
renderer->setPostProcessSettings(post);

// After a camera cut, start the TAA history again so the old view does not smear into the new one.
renderer->resetTemporalHistory();
```

- `SceneRendererDesc::clearColor` bir radiance değeridir: diğer her şey gibi pozlama ve tonemapping'den
  geçer.
- Bloom enerjiyi korur: ışık eklemek yerine görüntünün `bloom.intensity` kadarını bulanık kopyasıyla
  değiştirir.
- TAA, depth prepass'in yazdığı piksel başına hareket vektörleriyle hareket eden nesneleri izler.
  Renderer, her MeshRenderer'ın önceki dünya matrisini kendi eklediği bir `MotionHistory`
  Component'inde tutar; bir nesneyi ışınladıktan sonra, sıçramanın bulanıklaşmaması için
  `MotionHistory::previousWorld` değerini yeni matrisine ayarlayın. TAA varsayılan olarak kapalıdır;
  en keskin görüntüden çok yumuşak kenarlara ihtiyaç duyan sahnelere uygundur.
- `motionVectors()`, hata ayıklama görünümleri için hareket vektörü Texture'ını döndürür.
- `stats().drawCalls` her draw'ı sayar: occlusion culling ile opak bir batch, hem depth prepass'te hem de
  gölgelendirmede culling aşaması başına bir indirect draw'a sahiptir.

### glTF görüntüleyici

`illusion_gltf_viewer` bir modeli yörünge kamerasıyla gösterir (döndürmek için sol fare sürükleme veya
sağ çubuk, yakınlaştırmak için fare tekerleği veya tetikler). Model verilmezse, metallic değeri soldan
sağa artan ve roughness değeri yukarıdan aşağıya azalan bir küre ızgarası gösterir. `--environment`,
sahneyi yerleşik prosedürel gökyüzü yerine bir HDR dosyasıyla aydınlatır; `--taa` temporal
anti-aliasing'i açar. `--screenshot` son görüntüyü 1280x720 PNG olarak kaydeder.

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
