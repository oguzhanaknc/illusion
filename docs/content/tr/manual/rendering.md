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

`SceneRenderer`, `Transform` ve `RenderMesh` Component'lerine sahip her Entity'yi, `DirectionalLight`
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

world.create(illusion::Transform{}, illusion::RenderMesh{sphere, 0, material});

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

- `RenderMesh` bir GPU Mesh'ini, onun submesh'lerinden birini ve bir Material'i belirtir (atanmamış
  Material varsayılan beyaz, pürüzlü Material'i kullanır). Sahneler bunun yerine Asset referanslı
  `MeshRenderer` kullanır (aşağıya bakın).
- Işıklar Transform'larının -z ekseni boyunca (`DirectionalLight`) veya konumlarından (`PointLight`,
  `range` mesafesinde sıfıra iner) yayılır. İlk 4 yönlü ve 16 noktasal ışık kullanılır.
- `render()` Entity'leri eler ([Culling](#culling) bölümüne bakın), Pipeline, Material ve Mesh'e göre
  sıralar ve aynı (Mesh, submesh, Material) üçlüsünün her dizisini geçiş başına tek bir instanced draw
  ile çizer. `stats()` adayları, frustum içindeki Entity'leri, draw call'ları ve üçgenleri raporlar.
- Renderer, hedefin boyutunu izleyen bir derinlik buffer'ına sahiptir. Her görünüm için hedefin renk formatına uygun
  ayrı bir `SceneRenderer` oluşturun.
- sRGB formatlı renk hedefleri doğrusal renk alır; UNORM hedefler sRGB kodlanmış renk alır, böylece
  ikisi de aynı görünür.

### MeshRenderer ve RenderMesh

Sahneler neyin çizileceğini, Asset'lere yol ile başvuran ([Asset referansları](assets.md#asset-referanslar))
kaydedilen `MeshRenderer` Component'iyle tanımlar; renderer ise GPU Handle'larını tutan çalışma zamanı
Component'i `RenderMesh`'i çizer. İkisini World başına bir `RenderAssetResolver` bağlar; aynı
`RenderResources` ile çizilen tüm World'ler bir `RenderAssetCache`'i paylaşır:

Header: `<illusion/render/RenderAssets.hpp>`

```cpp
illusion::RenderAssetCache cache(resources, streamer);
illusion::RenderAssetResolver resolver(cache);

world.create(illusion::Transform{}, illusion::MeshRenderer{.mesh = illusion::AssetRef("game:/models/ship.glb#mesh/0")});
world.create(illusion::Transform{}, illusion::MeshRenderer{.mesh = illusion::AssetRef("builtin:/mesh/cube"),
                                                           .material = illusion::AssetRef("game:/materials/paint.material")});
resolver.waitIdle(world, &jobs); // optional: finish loading before the first frame

// Every frame, before rendering:
streamer.update();
cache.update();
resolver.update(world, &jobs);
```

- `MeshRenderer`; `mesh`, `submesh`, `material` (boşsa modelin o submesh için verdiği Material),
  `layers`, `castShadows`, `visible` ve `lodBias` (bkz. [levels of detail](#levels-of-detail)) alanlarını
  tutar. Her `update()` her birini, `RenderMesh`'inin
  çözüldüğü anahtarla karşılaştırır (paralel ve bellek ayırmadan) ve yalnızca değişenleri yeniden çözer.
- Asset'ler arka planda yüklenir: bir renderer, yeni Asset'leri GPU'ya ulaşana kadar önceden çizdiğini
  çizmeye devam eder (yeni renderer'lar o zamana kadar hiçbir şey çizmez). Akışla gelen modeller
  `RenderAssetCacheDesc::uploadBytesPerUpdate` sınırı içinde yüklenir.
- Yüklenemeyen bir Mesh hiçbir şey çizmez; yüklenemeyen bir Material varsayılan Material ile çizilir.
  Önbellek nedenini log'a yazar, `RenderAssetCache::error()` onu döndürür ve
  `RenderAssetResolver::statistics().failures` etkilenen renderer'ları sayar.
- Bir `MeshRenderer` kaldırılınca `RenderMesh`'i de kaldırılır. Çalışma zamanında geometri üreten kod
  `RenderMesh`'i doğrudan yazar (`sourceKey` 0 ile); resolver ona dokunmaz. `RenderMesh` kaydedilmez.
- Önbellek her Asset'i renderer'lar ve World'ler arasında paylaşır (tek Mesh'in 100 renderer'ı onu bir
  kez yükler) ve her Asset'i kullanan resolver'ları sayar (`references()`).
- **Canlı yeniden yükleme.** `AssetStreamer::reload(path)` ([Streaming](assets.md#degisen-dosyalar-yeniden-yukleme)
  bölümüne bakın), önbelleğin o dosyadan üretilen her Asset'i yeniden yüklemesini sağlar. Yenisi GPU'ya
  gelene kadar önceki sürüm çizilmeye devam eder; yeni dosya yüklenemezse önceki sürüm kalır (`error()`
  nedenini söyler). Bir `.material` dosyası yerinde değişir; Mesh'ler, modeller ve ortamlar yeni Handle'lar
  alır ve her resolver bir sonraki `update()` çağrısında renderer'larını yeniden çözümler. Yüklenemeyen bir
  Asset, dosyası yeniden yüklenince tekrar yüklenir.
- **Bellekten çıkarma.** `RenderAssetResolver::releaseUnused(world)`, World'deki hiçbir renderer'ın artık
  başvurmadığı Asset'leri kullanmayı bırakır; `RenderAssetCache::evictUnused()` de hiçbir resolver'ın
  kullanmadığı Asset'lerin (ve yalnızca onların kullandığı model ve Texture'ların) GPU kaynaklarını
  serbest bırakır. İkisi de her şeyi dolaşır: her karede değil, başka bir sahne yüklendikten sonra gibi
  Asset'lerin gitmiş olabileceği anlarda çağırın.

### Kaynak ömrü

`RenderResources`, serbest bırakılması istenen kaynakları serbest bırakır. Handle'lar slotlarının nesil
sayısını taşır; bu yüzden serbest bırakılan bir Handle hemen geçersizleşir: renderer'lar onu atlar,
`isValid()` false döndürür ve onu yeniden serbest bırakmak ya da güncellemek assert'e takılır. Bellek
ancak GPU, serbest bırakmadan önce gönderilen kareleri bitirdikten sonra yeniden kullanılır: `collect()`
(oluşturma fonksiyonları ve `RenderAssetCache::update()` tarafından çağrılır) GPU'nun artık kullanmadığı
kaynakları serbest bırakır. Mesh'ler ortak vertex ve index havuzlarında yaşar; serbest kalan aralıklar
yeniden kullanılır, böylece bir modeli tekrar tekrar içe aktarmak havuzları aynı boyutta tutar.

```cpp
const illusion::MeshHandle mesh = resources.createMesh(illusion::makeCube());
resources.releaseMesh(mesh);           // stale at once: draws nothing, cannot be released again
ILL_ASSERT(!resources.isValid(mesh));
resources.collect();                   // once per frame: frees what the GPU no longer uses

illusion::MaterialDesc paint;
paint.parameters.baseColorFactor = illusion::Vec4(0.9f, 0.2f, 0.1f, 1.0f);
const illusion::MaterialHandle material = resources.createMaterial(paint);
paint.parameters.roughnessFactor = 0.2f;
resources.updateMaterial(material, paint); // same handle, no new binding set
```

- `releaseMesh()`, `releaseTexture()`, `releaseMaterial()`, `releaseEnvironment()` ve `releaseModel()` boş
  Handle'ları yok sayar. Varsayılan Material ve Texture'lar serbest bırakılamaz.
- `updateMaterial()`, bir Material'in katsayılarını ve Texture'larını aynı Handle altında yeniden yazar.
  Yeni bir binding set'i yalnızca bir Texture ya da sarma (wrapping) değiştiğinde oluşturur; bu yüzden
  katsayıları her karede düzenlemek ucuzdur. Bir Texture'ı serbest bırakmadan önce onu kullanan
  Material'leri güncelleyin ya da serbest bırakın.
- `statistics()`, havuzların kullanımını ve kapasitesini, serbest aralıklarını ve kaç serbest bırakılmış
  kaynağın hâlâ GPU'yu beklediğini bildirir.

### Material'ler ve gölgelendirme

Material'ler glTF 2.0 metallic-roughness modelini izler: base colour, metallic, roughness, normal,
occlusion ve emissive; her biri isteğe bağlı bir Texture'la birlikte bir çarpandır. Alpha modları
opak, mask (bir eşik değeriyle) ve blend'dir; çift yüzlü Material'ler arka yüz elemeyi kapatır.
Aydınlatma GGX dağılımı, yüksekliğe bağlı Smith görünürlüğü ve Schlick Fresnel kullanır; ardından
pozlama ve ACES tonemapping uygulanır. Dolaylı ışık bir `SkyLight`'tan gelir (aşağıya bakın).

Bir glTF modelini çizmek için onu `loadGltf()` ile yükleyin ([Asset'ler ve Modeller](assets.md)
sayfasına bakın), `RenderResources::uploadModel()` ile GPU'ya yükleyin ve Entity'lerini `spawnModel()`
ile oluşturun.

### Rüzgar

Bir `Wind` Component'i (sahne başına bir tane) bitki örtüsünü hareket ettirir ve parçacıkları sürükler. Bir
yönü, m/s cinsinden ortalama bir `speed` değeri ve esintileri vardır: `gustStrength` (0 sabit, 1 durgundan iki
katı hıza) ve `gustFrequency` (bir noktada saniyedeki esinti sayısı). Esinti cepheleri rüzgarın hızıyla rüzgar
yönünde ilerler; böylece bir çimen tarlası dalga dalga eğilir.

Nasıl tepki vereceklerini Material'ler seçer; böylece bir modelin gövdesi, yaprakları ve kayaları farklı
davranabilir:

- `windSway` tüm nesneyi rüzgardan uzağa eğer: orijinin 1 m üzerinde m/s başına metre cinsinden kayma; yüksekliğin
  karesiyle büyür, yani taban yerinde kalır ve tepe eğilir. Tepe eğildikçe biraz alçalır ve gövdenin boyu korunur.
- `windFlutter` her vertex'e kendi fazıyla hızlı bir titreşim ekler (yapraklar, çimen yaprakları): m/s başına
  metre, rüzgarın `turbulence` değeriyle çarpılır.

Kaymalar her geçişin vertex Shader'ında hesaplanır: gölgelendirme, derinlik prepass'i, Entity ID'leri, gölge
haritaları ve hareket vektörleri (önceki karenin rüzgarıyla); böylece TAA ve gölgeler kararlı kalır. Rüzgarlı
gölge düşürenler, gölge haritalarında yalnızca Mesh'e göre gruplanmak yerine kendi Material'leriyle çizilir.
`ParticleEmitter::windFactor` parçacıkların rüzgarın hızını almasını sağlar (bkz. [Parçacıklar](particles.md)).

```cpp
world.create(illusion::Name{"Wind"}, illusion::Wind{.direction = {1.0f, 0.0f, 0.4f}, .speed = 4.0f});

illusion::MaterialData leaves = illusion::MaterialData{};
leaves.windSway = 0.004f;    // a 6 m tree leans about 0.6 m in a 4 m/s wind
leaves.windFlutter = 0.006f; // leaves shake by about 2 cm
emitter.windFactor = 1.5f;   // smoke drifts downwind

illusion::advanceWind(world, deltaSeconds); // GameSession and the editor call this for you
```

Rüzgarın saati çalışma zamanı durumu olan `WindState`'tir; `advanceWind()` onu ekler ve ilerletir.
`windVelocity()` oyun kodu için bir noktadaki havanın hızını, renderer'ın gösterdiğiyle aynı şekilde verir.

### Yarı saydamlık

Yapraklar, çimen yaprakları, kâğıt ve kumaş gibi ince yüzeyler ışığı geçirir. `diffuseTransmission` (0 ile
1 arası), bir Material'in diffuse ışığının öbür tarafa geçen payıdır: arka yüze ulaşan ışık ön yüzden
yumuşak diffuse ışık olarak çıkar; rengi base colour ile `diffuseTransmissionColor`'ın çarpımıdır. Aydınlık
tarafta yansıyan ışık da aynı oranda azalır, böylece yüzey enerji yaratmaz. Bu yüzden düzgün bir gökyüzü
altında beyaz, yarı saydam bir yüzey opak bir yüzeyle aynı görünür; güneşe karşı ise parlar. Metaller ışık
geçirmez. Bu, glTF'nin `KHR_materials_diffuse_transmission` eklentisidir: onu kullanan modellerin
çarpanları içe aktarılır (eklentinin iki Texture'ı okunmaz).

Gölgeler yine geçerlidir: başka bir yaprağın ya da bir gövdenin arkasındaki yaprak karanlık kalır. Yarı
saydam bir yüzeyin gölge testi ışığın ulaştığı tarafı kullanır, böylece tek bir yaprak kendini gölgelemez.

```cpp
illusion::MaterialData leaf = illusion::MaterialData{};
leaf.alphaMode = illusion::AlphaMode::Mask;
leaf.doubleSided = true;
leaf.diffuseTransmission = 0.4f;                    // 40% of the diffuse light passes through
leaf.diffuseTransmissionColor = {0.9f, 1.0f, 0.5f}; // the light inside a leaf turns yellow-green
```

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

### Levels of detail

Bir submesh en fazla üç daha kaba **level of detail** (`Submesh::lods`) taşıyabilir: daha az üçgen kullanan
sadeleştirilmiş sürümler. İçe aktarılan glTF modelleri bunları otomatik olarak alır
(bkz. [içe aktarma ayarları](assets.md#ice-aktarma-ayarlar)); kendi Mesh'leriniz için `generateLods()`
çağırın. Culling Job'ları her instance için, ekrana yansıtılan geometrik hatası `RenderView::lodPixelError`
pikseli (varsayılan 1) içinde kalan en kaba seviyeyi seçer; böylece nesneler siluetlerini korurken uzaktakiler
üçgenlerinin küçük bir kısmına mal olur.

```cpp
illusion::MeshData tree = illusion::makeSphere(1.0f, 64, 32);
illusion::generateLods(tree); // up to three coarser levels, each about half the triangles

illusion::RenderView view = illusion::RenderView::fromCamera(cameraTransform, camera, aspect);
view.lodPixelError = 1.0f;   // the default; 0 always draws full detail
view.lodFadeSeconds = 0.25f; // the default; 0 switches levels at once
view.deltaSeconds = frameSeconds;
renderer->render(commands, target, world, view, &jobs);
ILL_LOG_INFO("Game", "{} instances at full detail, {} at the coarsest level, {} cross-fading",
             renderer->stats().lodInstances[0], renderer->stats().lodInstances[illusion::kMaxSubmeshLods],
             renderer->stats().lodFades);
```

- Seviye, kameranın uzaklığından ve instance'ın ölçeğinden karede bir kez seçilir ve aynı seviye gölge
  haritalarına da çizilir; böylece gölgeler onları düşüren yüzeylerle örtüşür.
- Bir Mesh'in farklı seviyelerdeki instance'ları ayrı batch'ler oluşturur; GPU her birini eskisi gibi tek bir
  indirect draw ile eler ve çizer.
- Bir instance'ın seviyesi değiştiğinde `lodFadeSeconds` boyunca yenisine cross-fade yapar: iki seviye de
  çizilir ve piksel başına birbirini tamamlayan desenlerle dither'lanır; böylece her piksel ikisinden birini
  gösterir ve hiçbir şey birden belirmez. Geçiş depth prepass'i, gölgelendirmeyi, Entity ID'lerini ve gölge
  haritalarını kapsar; bıraktığı seviyeye geri dönmek geçişi tersine çevirir. Görüşe giren bir instance
  geçiş yapmadan kendi seviyesinde başlar.
- Renderer her Entity'nin seviyesini ve geçişini kareler arasında saklar; bu yüzden `deltaSeconds`'a gerçek
  kare süresini verin. `RenderStats::lodFades` geçiş yapan instance'ları sayar (`lodInstances` onları iki
  seviyede de sayar).
- `setDebugView(RenderDebugView::LevelOfDetail)` her yüzeyi çizdiği seviyeye göre renklendirir: tam detay
  yeşil, ardından sarı, turuncu ve kırmızı.
- `MeshRenderer::lodBias` (kod için `RenderMesh::lodBias`) tek bir renderer'ın payını çarpar: 2 her daha kaba
  seviyeye yarı uzaklıkta, 0.5 iki kat uzaklıkta geçer, 0 ise her zaman tam detay çizer. Oyuncunun yalnızca
  uzaktan gördüğü nesnelere ya da daha erken seyrelebilecek bitki örtüsüne verin.
- Kartlardan oluşan bitki örtüsü (yapraklar, iğne dalcıkları: vertex paylaşmayan çok sayıda küçük parça) kapalı
  bir yüzey gibi sadeleştirilemez. Bunun yerine seviyeleri, bir önceki seviyenin kartlarının eşit dağılmış
  yarısını tutar ve diğerlerinin yaprak alanını telafi etmek için her birini merkezi etrafında büyütür; böyle
  bir seviyenin hatası kartlar arasındaki boşlukların ne kadar büyüdüğüdür. Büyütülen kartlar Mesh'in ek
  vertex'leridir.

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

### Noktasal ışık gölgeleri {#point-light-shadows}

`castShadows` ayarı açık olan (varsayılan olarak kapalı) bir `PointLight` her yöne gölge düşürür: bir kamp
ateşi ağaçların ve çevresindeki insanların arkasındaki zemini gölgeler; ışık titrediğinde veya gölge
düşüren nesneler rüzgârda hareket ettiğinde gölgeler de hareket eder.

```cpp
illusion::Transform fire;
fire.position = illusion::Vec3(0.0f, 0.6f, 0.0f);
illusion::PointLight light{illusion::Vec3(1.0f, 0.55f, 0.22f), 40.0f, 14.0f};
light.castShadows = true;
light.sourceRadius = 0.25f;    // a wide flame: soft shadow edges
light.shadowNearPlane = 0.3f;  // the logs right under the flame do not darken the whole camp
world.create(fire, light);

illusion::ShadowSettings shadows = renderer->shadowSettings();
shadows.pointLightCount = 2;   // shadow maps for the two lights nearest the camera
shadows.pointResolution = 1024;
renderer->setShadowSettings(shadows);
```

- Gölgeli her ışık, `range` mesafesindeki opak ve alpha-mask'li Mesh'lerden bir küp shadow map'i (her biri
  `pointResolution`² texel olan altı yüz) çizer. Her karede en fazla `ShadowSettings::pointLightCount`
  ışık (en çok `kMaxShadowedPointLights`, yani 4) bir tane alır: menzili görüşe uzananlar, kameraya en
  yakın olandan başlayarak. Diğerleri sahneyi yine aydınlatır, ama gölgesiz.
- `sourceRadius`, parlayan gövdenin metre cinsinden boyutudur. Güneşte olduğu gibi gölgeler, gölgeyi
  düşüren nesnenin yüzeye değdiği yerde keskindir ve uzaklaştıkça yumuşar; yarı gölgeler bir küp yüzünün
  1/32'siyle sınırlıdır.
- Işığa `shadowNearPlane` mesafesinden yakın nesneler gölge düşürmez; böylece bir fenerin kendi camı veya
  gövdesi ışığını karartmaz. Yüzeyler kendi üzerine gölge düşürüyorsa `shadowBias` (metre) veya
  `ShadowSettings::normalBias` değerini artırın.
- [Hacimsel sis](#hacimsel-sis) aynı shadow map'leri okur; böylece gölgeli bir ışığın parıltısı
  duvarlarda durur ve gölge düşüren nesneler içinde huzmeler oyar.
- `stats()`, `shadowedPointLights`, `pointShadowCasters` ve `pointShadowDrawCalls` değerlerini raporlar.
  `ShadowSettings::enabled` kapatılınca noktasal ışık gölgeleri de kapanır. Bkz.
  [ADR 0051](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0051-point-light-shadows.md).

### Görüntü tabanlı aydınlatma

Bir `SkyLight`, sahneyi bir ortamla aydınlatır: çevredeki her şeyin equirectangular bir görüntüsü,
genellikle bir HDR fotoğraf. Ücretsiz ve yüksek kaliteli HDRI'ler (CC0)
[Poly Haven](https://polyhaven.com/hdris) adresinden `.hdr` dosyası olarak indirilebilir.

```cpp
// From a file: a RenderAssetResolver loads and bakes it.
illusion::Transform orientation;
orientation.rotation = glm::angleAxis(glm::radians(90.0f), illusion::Vec3(0.0f, 1.0f, 0.0f));
world.create(orientation, illusion::SkyLight{.environment = illusion::AssetRef("game:/skies/meadow_2k.hdr")});

// The built-in procedural sky, shaped by a ProceduralSky on the same Entity.
world.create(illusion::Transform{}, illusion::SkyLight{.environment = illusion::AssetRef("builtin:/sky/procedural")},
             illusion::ProceduralSky{.resolution = 1024});

// Baked by code: add the RenderSky yourself.
const illusion::EnvironmentHandle environment = resources.createEnvironment(illusion::makeSkyImage(1024));
world.create(illusion::Transform{}, illusion::SkyLight{}, illusion::RenderSky{environment});
```

- `createEnvironment()` görüntüyü GPU'da bir kez işler (bake): bir arka plan cube map'i, artan
  roughness için önceden filtrelenmiş bir specular cube map ve spherical harmonics olarak diffuse
  irradiance (CPU'da). Ortamları yükleme sırasında hazırlayın: 2K bir HDRI masaüstü bir makinede
  100 ms'den kısa sürer. `EnvironmentDesc` çözünürlükleri ve örnek sayısını belirler.
- Bir `RenderAssetResolver`, `environment` Asset'i olan bir `SkyLight`'a `RenderSky`'ını verir; dosya
  yüklendiğinde bake edilir. `builtin:/sky/procedural`, Entity'nin `ProceduralSky`'ıyla `makeSkyImage()`
  çıktısını bake eder; alanları her değiştiğinde yeniden.
- **Bake önbelleği.** `RenderAssetCache`, görüntü dosyalarının bake sonuçlarını streamer'ın içe aktarma
  önbelleğinde tutar (`AssetType::Environment`, [`loadCooked()`](assets.md#kendi-urettiginiz-assetler)
  aracılığıyla); anahtar, görüntü ve onun içe aktarma ayarları sidecar'ı ile `EnvironmentDesc`'tir. Sonraki
  bir çalıştırma (ya da aynı `EnvironmentDesc`'e sahip başka bir önbellek) görüntüyü yükleyip bake etmek
  yerine saklanan cube map'leri yükler. Bir bake, yapıldıktan birkaç kare sonra GPU'dan geri okunur ve
  ardından yazılır; `waitIdle()` hâlâ bekleyen geri okumaları bitirir. Değişen bir görüntü yeniden bake
  edilir; `AssetReloadMode::Reimport` ile yapılan bir yeniden yükleme hiçbir şey değişmese de yeniden bake
  eder. `statistics()`, `bakes` ve `bakeCacheHits` değerlerini sayar. Prosedürel gökyüzleri önbelleğe
  alınmaz (dosyaları yoktur).
- `requestEnvironmentReadback()` ve `readEnvironment()` bir ortamın cube map'lerini beklemeden CPU'ya bir
  `BakedEnvironment` olarak kopyalar (araçlarda ve test'lerde `block = true` verin);
  `createEnvironment(const BakedEnvironment&)` ise birini bake etmeden yükler:

```cpp
// Bake once, read the result back (here waiting for it), and upload it later without baking.
const illusion::EnvironmentHandle sky = resources.createEnvironment(image);
illusion::BakedEnvironment baked;
if (resources.readEnvironment(resources.requestEnvironmentReadback(sky), baked, true) ==
    illusion::ReadbackStatus::Ready)
{
    const illusion::EnvironmentHandle copy = resources.createEnvironment(baked, "Meadow");
}
```

- Renderer, ortamı hazır olan ilk `SkyLight`'ı kullanır. Transform'u ortamı döndürür; `intensity` onu
  ölçekler ve `background = false` sahnenin arkasında temizleme rengini bırakır.
- `SkyLight` yoksa onun yerine `SceneRendererDesc::ambient` tekdüze bir ortam ışığı ekler.
- Metaller çevrelerini yansıtır; bu yüzden ancak bir ortamla doğru görünürler.
- Gölgelendirme enerjiyi korur: tekdüze beyaz bir ortamda her beyaz Material, roughness veya
  metalness değeri ne olursa olsun arka planla aynı görünür (GPU test'lerindeki "white furnace"
  testi).

### Gece ve gündüz

Bir `SkyAtmosphere` Component'i günün her saati için bir gökyüzü çizer ve sahneyi onunla aydınlatır. `timeOfDay`
değerini (saat, 0 ile 24 arası) her kare değiştirin, güneş gökyüzünü geçer; hiçbir şey bake edilmez. Her
SkyLight'ın yerini alır.

- **Gökyüzü.** Hava (Rayleigh) ve pus (Mie), güneş ışığını ufka doğru büyüyen bir hava kütlesi boyunca saçar;
  ozon alacakaranlığı mavi tutar: koyu mavi bir öğle, parlak bir ufuk, turuncu bir gün batımı. Daha yüksek
  `turbidity` daha puslu, daha beyaz bir gökyüzü verir. Güneş, bloom oluşturan parlak bir disktir.
- **Gece.** Ay, güneşin karşısındaki noktanın `moonOffset` saat gerisinde durur (0: dolunay, 12: yeni ay) ve
  evresini gösterir; yıldızlar parıldar ve soluk bir Samanyolu gökyüzünü geçer; ikisi de gün batımından sonra
  belirir (`starIntensity`).
- **Işık.** Havanın renklendirdiği güneş sahnenin ilk directional ışığıdır ve gölgeleri düşürür
  (`castShadows`); gün batımından sonra ışık söner ve mavimsi ay ışığı belirir; böylece ışık hiç sıçramaz.
  `sunIntensity` ve `moonIntensity`, `DirectionalLight::intensity` gibi aydınlanma değerleridir.
- **Görüntü tabanlı aydınlatma.** Her renderer gökyüzünün küçük bir cube map'ini (`lightingResolution`,
  varsayılan 64) ve yansımalarını yalnızca gökyüzü değiştiğinde yeniden çizer; dağınık ışık için gökyüzünü
  CPU'da küresel harmoniklere izdüşürür. `stats().atmosphereUpdates` yeniden çizimleri bildirir.
- `sunAzimuth` güneşin yolunu döndürür (varsayılan olarak +x'ten doğar ve -x'te batar), `sunTilt` öğle
  güneşinin ne kadar yüksekte durduğunu belirler; `pathDirection()`, `sunDirection()` ve `moonDirection()`
  yönleri verir.

```cpp
const illusion::Entity sky = world.create(illusion::SkyAtmosphere{.timeOfDay = 17.5f, .turbidity = 3.0f});

// in a game module: a full day in four minutes
world.get<illusion::SkyAtmosphere>(sky).timeOfDay = std::fmod(hours + deltaSeconds / 10.0f, 24.0f);

const illusion::SkyAtmosphereState state = illusion::evaluateSkyAtmosphere(world.get<illusion::SkyAtmosphere>(sky));
const bool lampsOn = state.starVisibility > 0.3f;
```

`evaluateSkyAtmosphere()` (`illusion/render/SkyAtmosphere.hpp`) renderer'ın kullandıklarını döndürür: ışığı,
yıldızların ne kadar görünür olduğunu ve ayın aydınlık kısmını. `atmosphereRadiance()` ve
`atmosphereIrradiance()` (`illusion/assets/Atmosphere.hpp`) gökyüzü modelinin kendisini CPU'da hesaplar
(ADR 0049).

### Pozlama ve renk derecelendirme

Bir `PostEffects` Component'i (sahne başına bir tane) kameranın ışığı görüntüye nasıl çevirdiğini belirler. O
olmadan renderer `Camera::exposure` ve derecelendirmesiz ACES kullanır.

- **Otomatik pozlama** görüntüyü her kare ölçer (log parlaklığın merkez ağırlıklı ortalaması) ve pozlamayı onu
  orta griye getirecek değere doğru, stop cinsinden ve saniyede `adaptationSpeed` hızıyla kaydırır; karanlığa
  alışan bir göz gibi. `minExposure` ve `maxExposure` (stop) onu sınırlar, böylece gece gece kalır;
  `exposureCompensation` onu kaydırır. Uyum süresi `RenderView::deltaSeconds`'tır.
- **Tonemapper**: `AgX` parlak doygun ışığın (neon, ateş, gün batımı) tonunu korur ve onu beyaza soldurur;
  `Aces` daha sert kontrast verir ama parlak renkleri sarıya iter.
- **Derecelendirme**: `temperature` ve `tint` (beyaz dengesi), orta gri etrafında `contrast` ve `saturation`.
- **Lens**: `vignette`, hareketli film `grain`'i ve `chromaticAberration` (köşelerde piksel cinsinden renk
  saçağı).

```cpp
world.create(illusion::PostEffects{.autoExposure = true,
                                   .minExposure = -1.0f, // a sunny day is not darkened below half
                                   .maxExposure = 2.5f,  // a night is lifted by at most 2.5 stops
                                   .tonemapper = illusion::Tonemapper::AgX,
                                   .contrast = 1.1f,
                                   .vignette = 0.25f,
                                   .grain = 0.15f});
view.deltaSeconds = frameSeconds; // adaptation follows real time
```

Tanılama görünümleri (`setDebugView()`) bunların hepsini atlar.

### Hacimsel sis

Bir `VolumetricFog` Component'i (sahne başına bir tane) havayı ışığı saçan bir sisle doldurur. Güneşin ya da
ayın içinden geçtiği yerde sis parlar; ağaçların ya da duvarların gölgelediği yerde karanlık kalır, böylece
ışık huzmeleri belirir. Noktasal ışıklar da onun içinde parlar: bir kamp ateşi ya da neon bir tabela etrafındaki
havayı aydınlatır.

- **Ortam**: `density`, `baseHeight` yüksekliğinde metre başına sönümdür; onun üzerinde sis metre başına
  `heightFalloff` kadar incelir (0 havayı eşit doldurur). `albedo`, saçtığı ışığın rengidir.
- **Aydınlatma**: `anisotropy` (Henyey-Greenstein g) sisi ışıklara doğru (pozitif) ya da onlardan uzağa
  (negatif) parlatır. `lightIntensity` sisteki güneşi, ayı ve noktasal ışıkları, `ambientIntensity` ise
  gökyüzü ışığını ölçekler.
- **Biçim**: sis kameradan `maxDistance` metreye kadar uzanır. `noiseAmount` ve `noiseSize` onu `Wind`'in
  taşıdığı (`windFactor`) parçalara böler.

```cpp
world.create(illusion::VolumetricFog{.density = 0.03f,      // half the light is gone after 23 m
                                     .heightFalloff = 0.15f, // and the fog thins with height
                                     .baseHeight = 0.0f,
                                     .anisotropy = 0.6f, // glows towards the sun and the lamps
                                     .maxDistance = 90.0f,
                                     .noiseAmount = 0.6f, // patchy wisps, carried by the Wind
                                     .noiseSize = 10.0f});
```

Renderer, bir froxel ızgarasını (yaklaşık 8x8 piksellik hücreler, derinlikte üstel aralıklı 64 dilim) sisle ve
ışığıyla doldurur, huzmeler için gölgeli directional ışığın gölge kademelerini okur, huzmeler pürüzsüz
otursun diye her kareyi bir öncekiyle harmanlar ve sonucu önden arkaya toplar. Sis opak yüzeyleri,
gökyüzünü, harmanlanan yüzeyleri ve parçacıkları kaplar. Harmanlama yüzünden ani bir değişiklik (kapatılan bir
ışık) birkaç karede söner; kamera kesmelerinde `SceneRenderer::resetTemporalHistory()` çağırın.
`RenderStats::fogFroxels` karedeki froxel'leri sayar. [Gölgeli](#point-light-shadows) noktasal ışıklar sise de
gölge düşürür; tanılama görünümleri sahneyi sissiz gösterir. Bkz.
[ADR 0050](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0050-volumetric-fog.md).

### HDR ve post-processing

Renderer, sahneyi dahili bir HDR görüntüye çizer ve her kareyi post-processing ile bitirir:

1. Opak ve alpha-mask'li Mesh'ler için bir depth prepass; bu, gölgelendirmedeki overdraw'ı da ortadan
   kaldırır.
2. **Ambient occlusion** (GTAO), depth buffer'dan hesaplanır ve kıvrımlarda ve temas noktalarında
   dolaylı ışığı karartır.
3. Gölgelendirme, gökyüzü, blend Mesh'ler, ardından [parçacıklar](particles.md#cizim).
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
- TAA, depth prepass'in yazdığı piksel başına hareket vektörleriyle nesneleri izler. Her renderer,
  Entity generation değerini kontrol ederek kendi önceki Transform'larını saklar. Render işlemi World'ü
  değiştirmez. Her görünüm için ayrı renderer kullanın; nesneleri ışınladıktan veya World'ü değiştirdikten
  sonra `resetTemporalHistory()` çağırın. TAA varsayılan olarak kapalıdır.
- `motionVectors()`, hata ayıklama görünümleri için hareket vektörü Texture'ını döndürür.
- `stats().drawCalls` her draw'ı sayar: occlusion culling ile opak bir batch, hem depth prepass'te hem de
  gölgelendirmede culling aşaması başına bir indirect draw'a sahiptir.

### Kameralar ve görünüm filtreleri

Camera şeması v2, `projection`, `orthoHeight`, `farPlane` ve `priority` ekler. Eski sahneler perspective
varsayılanlarını korur. Perspective, `fovY` ve sonsuz uzak düzlem kullanır; orthographic, pozitif
`orthoHeight` ve `farPlane > nearPlane` kullanır. `findMainCamera(world)`, Transform'u olan etkin
Camera'lardan en yüksek öncelikliyi döndürür; eşitlikte en küçük Entity index kazanır. Yoksa null Entity döner.

```cpp
illusion::Camera camera;
camera.projection = illusion::CameraProjection::Orthographic;
camera.orthoHeight = 12.0f;
camera.farPlane = 200.0f;
camera.priority = 10;
auto view = illusion::RenderView::fromCamera(illusion::Transform{}, camera, 16.0f / 9.0f);
view.layerMask = 1u << 2;
```

`RenderMesh::kVisible`, `layers & view.layerMask` ve `view.hiddenEntities` bütün geçişleri filtreler.
Gizleme bitset'i `render()` dönene kadar ödünç alınır: `index / 64` sözcüğündeki `index % 64` biti Entity'yi
gizler; eksik sözcükler görünür sayılır. Entity index'leri yeniden kullanıldığında bitset'i yenileyin.
`kCastShadows` gölge oluşturmayı ayrıca kontrol eder. Cascade'ler iki projeksiyonu da destekler ve
orthographic uzak düzleminde sonlanır.

### Entity ID, readback ve overlay

Entity index ve generation içeren RG32_UINT attachment için `SceneRendererDesc::entityIds = true` ayarlayın.
Arka planda iki sözcük de `~0u` olur. Masked pikseller alpha cutoff'a uyar; alpha'sı sıfır olmayan blended
pikseller, sahnenin opak derinliğini değiştirmeden en yakın yüzeyi seçer.

`viewTargets()`, ödünç alınan `depth`, `ids`, `hdr`, `motion`, `renderSize`, `outputSize`,
`viewProjection` (jitter'lı) ve `unjitteredViewProjection` sağlar. Boyut değişimine veya renderer'ın
yıkımına kadar geçerlidir. Overlay'i `render()` sonrasında kaydedin; derinliği SRV olarak örnekleyin ve
çıktı piksellerini render piksellerine eşleyin.

Header: `<illusion/render/ReadbackRing.hpp>`. Ring'i bir kez oluşturun; kopya istemeden önce render
komutlarını gönderin. Sonraki karelerde sorgulayın; `Pending` hedefi değiştirmez. Dolu ring yeni istekleri
reddeder. Bölgeler ve hedef boyutu doğrulanır; kısa hedef buffer ticket'ı tüketmez.

```cpp
auto readback = illusion::ReadbackRing::create(*device, 3);
const auto ticket = readback->request(renderer->viewTargets().ids,
                                     illusion::UVec2(20, 30), illusion::UVec2(1));
if (ticket)
{
    illusion::UVec2 id;
    const auto result = readback->read(*ticket, std::as_writable_bytes(std::span(&id, 1)), true);
    // true waits for this copy only; interactive tools poll with false on later frames.
    if (result == illusion::ReadbackStatus::Ready)
    {
        // Validate index and generation against the current World before using the Entity.
    }
}
```

Her ticket başarıyla okunana kadar bir slot tutar. Boyut ve format eşleştiğinde staging depolaması yeniden
kullanılır. Hem sorgulama hem beklemeli okuma event query kullanır; hiçbiri `waitForIdle()` çağırmaz.

### Debug çizimleri ve tanılama

`DebugDraw` (`<illusion/render/DebugDraw.hpp>`), oluşturulurken çizgi depolamasını ayırır. `line`, `box`
ve `sphere`, geçersiz giriş veya yetersiz alanda hiçbir şey eklemeden false döndürür. Her karede `clear()`
çağırın. Kayıt sırasında bellek ayrılmaz; `render/debugdraw-record` Benchmark'ı 100 bin çizgi için çizgi
başına 20 ns bütçe uygular.

```cpp
illusion::DebugDraw draw(16384);
draw.line(illusion::Vec3(0), illusion::Vec3(1, 0, 0), illusion::Vec4(1, 0, 0, 1),
          3.0f, illusion::DebugDepth::OnTop);
auto overlay = illusion::DebugDrawRenderer::create(*device, rendererDesc.colorFormat);
// While commands is open, after renderer->render(...):
overlay->render(commands, swapchain->currentBackBuffer(), renderer->viewTargets(), draw);
```

Overlay renderer'ını bir kez oluşturun. Genişlik çıktı pikseli cinsindendir; GPU wide-line desteği yerine
quad kullanılır. `DebugDepth::Tested`, reversed-Z sahne derinliğini örnekler; `OnTop` geometri arkasında da
görünür. Ayrı render/çıktı boyutları, çıktı çözünürlüğü farklıyken de doğru örtüşme sağlar.

`setDebugView()`, Lit, Unlit, Albedo, Normals, Roughness, Metallic, AmbientOcclusion, ShadowCascades,
MotionVectors veya LevelOfDetail seçer. Tanılama modları temporal biriktirme, bloom, pozlama ve ACES'i atlar; çıktı renk
kodlaması korunur. `setWireframeOverlay(true)` opak üçgen kenarlarını çizer; non-solid fill olmayan GPU'da
false döndürür. `setClearColor()` arka plan radiance değerini değiştirir.

`SceneRendererDesc::gpuTimers` ile ölçümü açın. `gpuTimings()`, tamamlanan geçiş sürelerini milisaniye olarak
sorgular: `available` timestamp desteğini, `valid` tamamlanmış ölçümü bildirir. Query ring doluysa beklemek
yerine ölçüm atlanır. `stats().lightsDropped`, 4 directional ve 16 point light sınırını aşan etkin ışıkları
sayar. GPU regresyonları DebugDraw, orthographic gölgeler ve normals görüntülerini `tests/data/render/`
PNG'leriyle karşılaştırır; `ILLUSION_UPDATE_GOLDENS=1` bunları günceller. Test çalışma dizinindeki
`render-captures/`, inceleme için görüntüleri saklar.

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
yönünün tersindedir. Perspective görünüm sonsuz uzak düzlemli **reversed-Z** kullanır: yakın düzlem
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
