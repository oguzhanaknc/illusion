# Animasyon

Karakterler, yaratıklar ve makineler kemiklerle hareket eder: skinned bir Mesh bir iskeletin eklemlerini
izler, bir klip de eklemleri zaman içinde hareket ettirir. illusion skin'leri ve klipleri glTF'ten içe
aktarır, Job System üzerinde pozlar ve Mesh'leri GPU'da skin'ler ([ADR 0059](../architecture/decisions.md)).

## Rig'li bir glTF modelini içe aktarma

`loadGltf()` bir glTF dosyasının skin'lerini ve animasyonlarını Mesh'lerin yanında `ModelData`'sına okur:

- `ModelData::skins`, glTF skin'i başına en çok `kMaxJoints` (1.024) eklemli bir `Skeleton` tutar.
  Eklemler, ebeveynler çocuklarından önce gelecek şekilde sıralanır; her biri adını, ebeveynini, dinlenme
  pozunu ve ters bind matrisini korur.
- `ModelData::animations`, glTF animasyonu başına bir `AnimationClip` tutar. Bir kanal bir eklemin
  konumunu, dönüşünü ya da ölçeğini **adıyla** `Step`, `Linear` ya da `CubicSpline` anahtarlarla hareket
  ettirir; böylece bir klip, eklemleri bu adları taşıyan her iskelette oynar. Adsız klipler
  `animation<N>` diye adlandırılır; tekrar eden adlar `.2`, `.3`, ... alır.
- Skinned bir Mesh vertex'lerini korur ve paralel bir `SkinVertex` akışı olan `MeshData::skin`'i alır
  (vertex başına dört eklem ve dört ağırlık, 16 bayt), ayrıca her eklemin hareket ettirdiği sınırları
  (`jointBounds`). Dörtten fazla etkisi olan vertex'ler en büyük dördünü yeniden normalize ederek tutar.

Morph target'lar, eklem olmayan node'lardaki kanallar ve geçersiz skin verisi bir log satırıyla atlanır;
Mesh bu durumda rijit çizilir.

## Asset yolları

Kaydedilen Component'ler bir modelin parçalarına asset yolu parçalarıyla başvurur:

| Parça | Başvurduğu |
| --- | --- |
| `model.glb#mesh/N` | `N` numaralı Mesh (bir `MeshRenderer`) |
| `model.glb#skin/N` | `N` numaralı iskelet (`AssetKind::Skeleton`) |
| `model.glb#animation/<name>` | `<name>` adlı klip (`AssetKind::AnimationClip`) |

## Animator

Skinned bir Mesh, `MeshRenderer`'ının Entity'sindeki kaydedilen `Animator` Component'iyle hareket eder.
World başına bir `AnimationWorld` pozları ECS'in dışında tutar ve çalışma zamanı Component'i
`AnimationPose`'u ekler (pozun slotu, eklem sayısı ve dünya sınırları):

Header'lar: `<illusion/animation/AnimationComponents.hpp>`, `<illusion/animation/AnimationWorld.hpp>`

```cpp
illusion::AnimationWorld animation(world, streamer);
const illusion::Entity hero = world.create(
    illusion::Transform{}, illusion::MeshRenderer{.mesh = illusion::AssetRef("game:/hero.glb#mesh/0")},
    illusion::Animator{.skeleton = illusion::AssetRef("game:/hero.glb#skin/0"),
                       .clip = illusion::AssetRef("game:/hero.glb#animation/Walk")});

// Every frame, after game code and before physics:
animation.update(deltaSeconds, &jobs, true);
const illusion::AnimationPose& pose = world.get<illusion::AnimationPose>(hero);
const std::span<const illusion::SkinMatrix> skin = animation.skinMatrices(pose.slot);
```

| Alan | Anlamı |
| --- | --- |
| `skeleton` | İskelet, `model.glb#skin/N`; Mesh'in skin'i olmalıdır |
| `clip` | Klip, `model.glb#animation/<name>`; boşsa dinlenme pozu gösterilir |
| `graph` | Animator'ı `clip` yerine yöneten bir [animasyon grafı](#animasyon-graflar) (`.animgraph`) |
| `speed` | Oynatma hızı; negatifse geriye oynatır |
| `time` | Klibin içinde saniye |
| `loop` | Klibin sonunda başa sar; değilse son pozda kal |
| `playing` | Oyun çalışırken `time`'ı ilerlet |

- Her alan kaydedilir; böylece sahneler ve [kayıtlı oyunlar](saved-games.md) klibin nerede kaldığını
  korur.
- `advance` true iken (Play modu ve oynatıcı; `GameSession` başlatılmışken bunu geçirir) `update()`,
  `speed` çarpı saniyeyi `time`'a ekler; `loop` açıksa klibin içine sarar, değilse `time` klibin sonunda
  (geriye oynarken başında) durur ve `playing` false olur.
- Her model dosyası `AssetStreamer` üzerinden bir kez yüklenir; iskeletleri, klipleri ve Mesh'lerinin
  eklem sınırları tüm Animator'lar arasında paylaşılır ve bir klip bir iskelete bir kez bağlanır. Pozlar
  Job System üzerinde paralel örneklenip birleştirilir ve skin matrisleri tek bir bitişik tampona
  yazılır; `previousSkinMatrices()` hareket vektörleri için bir önceki güncellemeyi tutar (bir pozun ilk
  güncellemesinde güncel olanlara eşittir). `AnimationPose::bounds`, skinned Mesh'in dünya uzayındaki
  sınırlarıdır.
- Modeli yüklenirken poz bekler (`jointCount` 0'dır). Mesh'e uymayan bir iskelet (başka bir skin, başka
  bir eklem sayısı ya da rijit bir Mesh) bir kez bildirilir ve poz almaz; Mesh bind pozunda çizilir.
  `Inactive` etiketli Entity'ler pozlarını ve `time`'larını korur.
- Bir model dosyasını yeniden yüklemek (`AssetStreamer::reload()`) Animator'larını yeniden bağlar;
  başarısız bir yeniden yükleme önceki veriyi korur. Kararlı bir güncelleme bellek ayırmaz.

## Editörde oynatma ve kaydırma

Inspector'da skinned bir `MeshRenderer`'ın Entity'sine bir `Animator` ekleyin ve `skeleton` ile `clip`'ini
ayarlayın. Play modu dışında editör, edit World'ünü kaydedilen `time`'da ilerletmeden pozlar; bu yüzden
Inspector'da `time`'ı sürüklemek klibi Scene görünümünde kaydırır. Play modunda oturumun `AnimationWorld`'ü
oynayan her Animator'ın `time`'ını ilerletir; Stop kaydedilen değerlere döner. Showcase projesinin
`Column`'u `sway` klibini bu şekilde oynatır ([Showcase projesi](editor/showcase.md)).

## Animasyon grafları

`graph` alanı bir `.animgraph` dosyasını adlandıran bir Animator, klibi yerine o grafı oynatır (ADR 0060):
durumları klip ya da 1D/2D blend space oynatan durum makinesi katmanları, parametrelerle tetiklenen ve
cross-fade yapan geçişler, maskeli ve additive katmanlar, durum olayları ve root motion. Dosya JSON'dur
(`"format": "illusion.animgraph"`, `parseAnimationGraph()` okur); editörün Asset tarayıcısı onu bir
`AnimationGraph` olarak listeler ve export'lar onu olduğu gibi taşır.

- Graf `AssetStreamer` üzerinden bir kez yüklenir ve iskelet başına bir kez bağlanır; her Animator kendi
  durumunu tutar. Bu durum giriş durumlarında, zaman olarak `time` (saniye) ve varsayılan parametrelerle
  başlar. Bir sahne ya da kayıtlı oyun grafı aynı şekilde yeniden başlatır.
- İlerletilirken ve `playing` açıkken graf, `speed` çarpı saniye kadar adım atar; Play modu dışında editör
  giriş durumlarını `time`'da gösterir. `clip` ve `loop` kullanılmaz.
- Oyun kodu onu [`AnimationService`](../reference/scripting-api.md#animationservice) üzerinden yönetir:
  ada göre parametreler ve trigger'lar, geçerli durum ve zamanı, son update'in olayları. Grafı olmayan
  Animator'lar klibi `play()` ile değiştirir; bu, oynattıkları klipten cross-fade yapar.
- `rootMotion` içeren bir graf o eklemin yatay hareketini ve yaw'ını pozdan çıkarır;
  [oyun oturumu](runtime.md#adm-sras) Animation adımından sonra Entity'yi onunla hareket ettirir.
- Dosyayı değiştirmek (`AssetStreamer::reload()`; editör bunu değişen dosyalar için yapar) Animator'larını
  yeni grafla yeniden başlatır; yüklenemeyen ya da bağlanamayan bir dosya bildirilir ve dinlenme pozunu
  gösterir.

```cpp
// In a game module: drive the graph of the Entity's Animator.
if (illusion::AnimationService* animation = context.service<illusion::AnimationService>())
{
    static_cast<void>(animation->setFloat(hero, "speed", 3.0f));
    static_cast<void>(animation->setTrigger(hero, "jump"));
    std::array<illusion::AnimationEvent, 16> events{};
    const illusion::u32 count = animation->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        playFootstep(events[i].entity);
    }
}
```

Biçimin tam kılavuzu, eksiksiz bir örnekle ANIM-2c ile gelecek; showcase projesinin `Column`'u
`assets/animations/column.animgraph`'ı oynatır.

## Skinned Mesh'leri çizme

`SceneRenderer` pozlanmış her Mesh'i GPU'da skin'ler. Pozları ona `RenderView::animation` içinde verin;
oynatıcı, Play modu ve editörün viewport'ları bunu sizin için yapar:

```cpp
streamer.update();
cache.update();
resolver.update(world, &jobs);
animation.update(deltaSeconds, &jobs, true);

illusion::RenderView view = illusion::RenderView::fromCamera(cameraTransform, camera, aspect);
view.animation = &animation; // null draws skinned meshes in their bind pose
renderer->render(commands, target, world, view, &jobs);
```

- Karenin başında bir compute pass, pozu taze olan (`AnimationWorld`'ün son güncellemesinde yazılmış) ve
  `AnimationPose::bounds`'u kameraya ya da bir gölge kademesine değen her instance'ı skin'ler. Pozlanmış
  vertex'leri skinned bir vertex tamponunun bir aralığına, önceki pozdaki konumları da yanlarına yazar.
- Depth prepass, gölge haritaları, gölgelendirme, hareket vektörleri, Entity ID'leri ve wireframe katmanı
  instance'ı bu aralıktan çizer; böylece gölgeler pozlanmış şekli gösterir, seçim ona isabet eder ve
  temporal anti-aliasing eklemlerin hareketini görür.
- Culling pozun sınırlarını kullanır. Skinned bir instance tek başına çizilir (pass başına bir draw),
  GPU güdümlü instancing yolunun dışında ve occlusion culling olmadan. Ayrıntı seviyeleri yine uygulanır.
- Bir instance, pozu sürdükçe aralığını korur; aralığı ilk görünür olduğunda alır.
  `RenderStats::skinnedInstances` ve `skinnedVertices` son karenin işini, `RenderGpuTimings::skinning`
  GPU süresini sayar.

## Henüz desteklenmeyenler

- Süren bir geçişi kesmek, alt durum makineleri ve bir grafın canlı durumunu kaydetmek.
- Socket'ler (bir Entity'yi bir ekleme bağlamak), look-at ve ters kinematik, ragdoll'lar (ANIM-3).
- Klip önizlemeleri, bir zaman çizelgesi, iskelet debug çizimi ve `animation.*` editör komutları (ANIM-4).
- Morph target'lar, vertex başına dörtten fazla etki ve anahtar kare sıkıştırma.

## Performans

- Eklemler Entity değil, bitişik dizilerde satırlardır: 60 eklemli 300 karakteri pozlamak tek bir
  masaüstü çekirdeğinde yaklaşık 1,5 ms sürer (Benchmark `animation/evaluate-300x60`) ve Job System'e
  yayılır.
- Skinning, Mesh'i kaç pass çizerse çizsin karede bir kez çalışır: 10.240 vertex'lik 300 instance bir
  NVIDIA GeForce RTX 4070 SUPER'da 0,66 ms sürer (GPU testi 3 ms'lik bir bütçe koyar).
- Görünür her skinned instance vertex başına 64 bayt GPU belleğine (pozlanmış vertex ve önceki konumu) ve
  pass başına bir draw'a mal olur. Binlerce özdeş karakter instanced skinning gerektirir; bu henüz yoktur.
