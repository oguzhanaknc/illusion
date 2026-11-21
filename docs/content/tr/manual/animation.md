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

Showcase projesinin `Column`'u `assets/animations/column.animgraph`'ı oynatır.

### Dosya, anahtar anahtar

Bir graf dosyasının anahtarları aşağıdadır; bilinmeyen bir anahtar hatadır, böylece bir yazım yanlışı fark edilmeden geçmez.

| Anahtar | Anlamı |
| --- | --- |
| `format`, `version` | `"illusion.animgraph"` ve `1` |
| `parameters` | Grafın girdileri (aşağıda); isteğe bağlı |
| `rootMotion` | Yatay hareketi ve yaw'ı Entity'yi süren eklemin adı; isteğe bağlı |
| `layers` | Sırayla değerlendirilen bir ya da daha fazla katman |

**Parametreler** `{"name", "type", "default"}` biçimindedir. Adlar benzersizdir ve boş olamaz.

| `type` | Tuttuğu | `default` |
| --- | --- | --- |
| `Float` | Bir sayı (hız, yön) | Bir sayı, yoksa 0 |
| `Int` | -2^24 ile 2^24 arasında bir tam sayı (silah türü) | Bir tam sayı, yoksa 0 |
| `Bool` | `true` ya da `false` (çömelme) | `true` ya da `false`, yoksa `false` |
| `Trigger` | Bir kez kurulur; bir geçiş onu tüketene ya da update bitene kadar kurulu kalır | Yok |

**Katmanlar** `{"name", "weight", "blend", "mask", "entry", "states", "transitions"}` biçimindedir:

- [0, 1] aralığındaki `weight` (varsayılan 1) katmanı ölçekler; `blend` ya `Override` (varsayılan) olur ve
  altındaki katmanların pozladığını değiştirir, ya da `Additive` olur ve her klibin ilk karesinden pozunun
  farkını ekler; böylece nefes alma ya da eğilme klibi her temel hareketin üstüne biner.
- `mask`, `{"joint", "weight"}` listeler: her giriş o eklemin **ve alt eklemlerinin** ağırlığını ayarlar
  (sonraki giriş kendi alt ağacında kazanır). Hiçbir girişin kapsamadığı eklemleri katman değiştirmez; boş ya da
  eksik bir mask her eklemi kapsar. `"mask": [{"joint": "spine", "weight": 1}]` üst gövdeyi katmana verir,
  bacakları alttaki katmanlara bırakır.
- `entry` katmanın başladığı durumu adlandırır (varsayılan: ilk durum). `states` en az bir durum içerir.

**Durumlar** `{"name", "clip" ya da "blend1d" ya da "blend2d", "speed", "speedParameter", "loop", "events"}`
biçimindedir:

- Bir durumun tam olarak bir hareketi vardır: `clip` (`"game:/hero.glb#animation/walk"`, [Asset
  yollarındaki](#asset-yollar) klip yolu), `blend1d` ya da `blend2d`. `*` adı ayrılmıştır.
- `speed` (varsayılan 1, negatif olamaz) oynatma hızıdır; `speedParameter` onunla çarpılan bir Float
  parametresini adlandırır, böylece tek bir parametre bütün bir durumu yavaşlatabilir. Negatif hız durumu tutar.
- `loop` (varsayılan `true`) sonda başa sarar; aksi halde durum son pozunu tutar ve normalize zamanı 1'in
  üzerinde devam eder.
- `events`, `{"name", "time"}` biçimindedir; `time` [0, 1] aralığında normalizedir ve zamana göre sıralıdır.

**Blend space'ler** birden çok klibi aynı anda, Float parametrelerine göre ağırlıklandırarak oynatır:

- `"blend1d": {"parameter", "points": [{"value", "clip"}]}` en az iki nokta içerir; noktalar `value`'ya göre
  sıralıdır ve tekrar etmez. İki komşu arasında ağırlıklar doğrusaldır; ilkinin altında ve sonuncunun üstünde
  uçtaki klip tek başına oynar.
- `"blend2d": {"x", "y", "points": [{"x", "y", "clip"}]}` iki farklı Float parametresini adlandırır ve ayrı,
  hepsi tek bir doğru üzerinde olmayan en az üç nokta içerir. Noktalar graf yüklenirken üçgenlenir (Delaunay);
  bir üçgenin içinde üç klip parametre değerlerinin barycentric ağırlıklarını alır, kabuğun dışında kabuğun
  kenarlarının en yakın noktası kullanılır.
- Bir blend space'in klipleri senkron oynar: tek bir normalize zamanı paylaşırlar ve durumun süresi kliplerin
  sürelerinin ağırlıklı ortalamasıdır. Farklı uzunluktaki bir yürüme ve bir koşu bu yüzden ağırlıklar
  değişirken adımlarını birlikte tutar ve durumun olayları her klibin aynı anında tetiklenir.

**Geçişler** `{"from", "to", "duration", "exitTime", "conditions"}` biçimindedir:

- `from` bir durum adı ya da herhangi bir durum için `"*"` olur (hedef durumun kendisinde asla tetiklenmez);
  `to` katmanın başka bir durumudur.
- `conditions`'ın **hepsi** sağlanmalıdır. Bir koşul `{"parameter", "compare", "value"}` biçimindedir; bir
  Trigger `compare` ve `value` almaz ve kurulu olduğu sürece sağlanır.

  | Parametre | `compare` | `value` |
  | --- | --- | --- |
  | `Float` | `Greater`, `Less` | Bir sayı |
  | `Int` | `Greater`, `Less`, `Equal`, `NotEqual` | Bir tam sayı |
  | `Bool` | `Equal`, `NotEqual` | `true` ya da `false` |
  | `Trigger` | yok | yok |

- `exitTime` (normalize, isteğe bağlı) geçişi, kaynak durumun normalize zamanı ona ulaşana ya da döngüdeki
  bir kaynak başa sarana kadar bekletir. Ne koşulu ne de exit time'ı olan bir geçiş hemen tetiklenir.
- `duration` (saniye, varsayılan 0) cross-fade'dir: ağırlıklar kaynaktan hedefe kayarken iki durum da oynar.
  0 ile değişim anidir.
- **Öncelik dosyadaki sıradır**: her update'te katmanın tetiklenen ilk geçişi kazanır. Acil olanları (`*`'dan
  bir zıplama) başa koyun.

### Geçişler nasıl çalışır

- Bir cross-fade sürerken katmanın başka bir geçişi tetiklenmez: **süren bir geçiş kesilmez**. Süresi boyunca
  kurulan bir trigger, o update'in sonunda etkisiz temizlenir; bu yüzden girdi bir fade sırasında gelebiliyorsa
  oyun onu yeniden kurar (ya da bir Bool tutar).
- Tetiklenen bir geçiş, koşullarındaki trigger'ları tüketir. Hiçbir geçişin tüketmediği trigger'lar update'in
  sonunda temizlenir; yani trigger bir "şimdi" sinyalidir, Bool ise bir durumdur.
- Hedef kendi 0 zamanından başlar ve bir sonraki adımla hareket eder. Yeni durum, geçiş tetiklenir tetiklenmez
  katmanın `currentState()`'idir, fade bittiğinde değil.
- Oyun kodunun parametre yazımları bir sonraki Animation adımında kullanılır.

### Olaylar

Bir olay, bir durumun adlandırılmış bir anıdır: ayak sesleri, efektler ve oyun kancaları için. Her update'ten sonra
Animator'lar zamanın geçtiği olayları yayımlar: normalize `t` zamanındaki bir olay, durumun zamanı [before, after)
yarı açık aralığında onu geçtiğinde tetiklenir. Ayrıntılar:

- Döngüdeki bir durum, başa sarmasının iki yanındaki olayları aynı update'te tetikler.
- Döngüsüz bir durum her olayı zamanı onu geçtiğinde bir kez tetikler; son pozunu tuttuktan sonra artık bir şey
  tetiklemez.
- Yalnızca ağırlığı 0'ın üzerinde olan durumlar tetikler: bir cross-fade sırasında iki durum da tetikler, fade'i
  bitmiş bir durum tetiklemez.
- Bir döngüden uzun bir adım her olayı bir kez tetikler.

Bir modül onları `AnimationService::pollEvents()` ile okur: son update'in olayları, katman sırasıyla ve update
sırasıyla; her biri Entity'yi, olay adını, katmanı ve durumu taşır. Bir update boyunca yayımlanırlar; bir update
içindeki her çağrı aynı olayları döndürür.

### Root motion

`"rootMotion": "hips"` ile adlandırılan eklemin yatay ötelemesi (x ve z) ve yaw'ı pozundan çıkarılır: eklem
dinlenme pozunun yatay konumunda ve yaw'ında kalır, böylece Mesh Entity'sinden uzaklaşıp yürümez; hareket oyuna dünya
uzayında `AnimationPose::rootTranslation` (metre) ve `rootYaw` (radyan, +y etrafında) olarak, poz gibi
ağırlıklandırılıp katmanlanarak (blend space'ler ve cross-fade'ler dahil) verilir. Bir update'in hareketini
[oyun oturumu](runtime.md#adm-sras) yalnızca oyun çalışırken uygular (editörün kaydırmasında değil):

- `CharacterController`'ı olan bir Entity, ötelemeyi adıma bölünmüş yatay hızı olarak alır; dikey hız
  controller'ındır, yani yerçekimi ve zıplamalar çalışmaya devam eder ve zıplama klipten değil fizikten gelir.
- Diğer her Entity ötelemeyle hareket eder (bir parent altındaysa `LocalTransform`'u).
- İkisi de yaw kadar döner.
- Karakteri kendisi yönlendiren bir oyun `rootMotion`'ı grafından çıkarır.

### Eksiksiz bir örnek

Aşağıdaki graf `speed` parametresine göre yürür ve koşar, 2D bir blend space'te yana adım atar, herhangi bir
durumdan bir trigger ile zıplar (zıplama döngüye girmez ve sonuna yakın hareket durumuna döner), üst gövdesiyle
maskeli bir katman üzerinden nişan alır, additive bir katmanla nefes alır, döngü başına iki kez `footstep` olayı
yayımlar ve `hips` ekleminin root motion'ını alır. Klipler, rig'li bir `hero.glb`'nin taşıyacağı adlardır.
`tests/animation/TestAnimationGraphExample.cpp` testi bu metnin aynısını ayrıştırır.

```json
{
  "format": "illusion.animgraph",
  "version": 1,
  "parameters": [
    {"name": "speed", "type": "Float"},
    {"name": "playRate", "type": "Float", "default": 1},
    {"name": "moveX", "type": "Float"},
    {"name": "moveY", "type": "Float"},
    {"name": "strafing", "type": "Bool"},
    {"name": "aiming", "type": "Bool"},
    {"name": "jump", "type": "Trigger"}
  ],
  "rootMotion": "hips",
  "layers": [
    {
      "name": "base",
      "entry": "locomotion",
      "states": [
        {
          "name": "locomotion",
          "speedParameter": "playRate",
          "blend1d": {
            "parameter": "speed",
            "points": [
              {"value": 0, "clip": "game:/hero.glb#animation/idle"},
              {"value": 1.5, "clip": "game:/hero.glb#animation/walk"},
              {"value": 4, "clip": "game:/hero.glb#animation/run"}
            ]
          },
          "events": [
            {"name": "footstep", "time": 0.25},
            {"name": "footstep", "time": 0.75}
          ]
        },
        {
          "name": "strafe",
          "blend2d": {
            "x": "moveX",
            "y": "moveY",
            "points": [
              {"x": 0, "y": 0, "clip": "game:/hero.glb#animation/idle"},
              {"x": -1, "y": 0, "clip": "game:/hero.glb#animation/strafe_left"},
              {"x": 1, "y": 0, "clip": "game:/hero.glb#animation/strafe_right"},
              {"x": 0, "y": 1, "clip": "game:/hero.glb#animation/walk"}
            ]
          },
          "events": [
            {"name": "footstep", "time": 0.25},
            {"name": "footstep", "time": 0.75}
          ]
        },
        {
          "name": "jump",
          "clip": "game:/hero.glb#animation/jump",
          "loop": false,
          "events": [
            {"name": "land", "time": 0.8}
          ]
        }
      ],
      "transitions": [
        {"from": "*", "to": "jump", "duration": 0.1, "conditions": [{"parameter": "jump"}]},
        {
          "from": "jump",
          "to": "locomotion",
          "duration": 0.2,
          "exitTime": 0.9
        },
        {
          "from": "locomotion",
          "to": "strafe",
          "duration": 0.25,
          "conditions": [{"parameter": "strafing", "compare": "Equal", "value": true}]
        },
        {
          "from": "strafe",
          "to": "locomotion",
          "duration": 0.25,
          "conditions": [{"parameter": "strafing", "compare": "Equal", "value": false}]
        }
      ]
    },
    {
      "name": "upper",
      "mask": [{"joint": "spine", "weight": 1}],
      "states": [
        {"name": "relaxed", "clip": "game:/hero.glb#animation/idle"},
        {"name": "aim", "clip": "game:/hero.glb#animation/aim"}
      ],
      "transitions": [
        {
          "from": "relaxed",
          "to": "aim",
          "duration": 0.15,
          "conditions": [{"parameter": "aiming", "compare": "Equal", "value": true}]
        },
        {
          "from": "aim",
          "to": "relaxed",
          "duration": 0.15,
          "conditions": [{"parameter": "aiming", "compare": "Equal", "value": false}]
        }
      ]
    },
    {
      "name": "breath",
      "blend": "Additive",
      "weight": 0.5,
      "states": [
        {"name": "breathe", "clip": "game:/hero.glb#animation/breathe"}
      ]
    }
  ]
}
```

Bir modül onu `setFloat(hero, "speed", ...)`, `setBool(hero, "strafing", ...)`, `setTrigger(hero, "jump")` ile
yönetir ve `footstep` ile `land` olaylarını okur; [Bir karakteri canlandırın](../tutorials/animated-character.md)
eğitimi bunu adım adım yapar.

### Neler kaydedilir, neler yeniden başlar

- Animator'ın alanları (`skeleton`, `graph`, `speed`, `time`, `loop`, `playing`; `clip` kullanılmaz) sahneyle ve
  oyunla kaydedilir. **Grafın canlı durumu** (geçerli durumlar, zamanları, süren bir cross-fade, parametre
  değerleri) kaydedilmez: bir sahne yüklemesi, kayıtlı bir oyun ya da graf dosyasının yeniden yüklenmesi grafı
  giriş durumlarında, varsayılan parametrelerle ve Animator'ın `time`'ı ile yeniden başlatır.
- Bir parametreyi korumak isteyen bir oyun (çömelen bir Bool) onu yüklemeden sonra yeniden yazar.
- Oyun çalışırken bir `.animgraph`'ı düzenlemek onu yeniden yükler: onu kullanan Animator'lar yeni grafla yeniden
  başlar. Ayrıştırılamayan ya da bağlanamayan yeni bir dosya yüklü grafı korur ve bildirilir.

### Sınırlar ve hatalar

- Bir Animator bir update'te en fazla `kMaxActiveClips` (16) klip örnekler: ağırlığı olan durumların bütün klipleri
  (blend space'ler ve cross-fade'ler her klibi sayar). Bunun ötesinde ağırlığı çarpı katman ağırlığı en hafif
  klipler atılır ve geri kalanlar yeniden normalize edilir.
- Klipler iskeletin modelinde, mask'lerin ve `rootMotion`'ın adlandırdığı eklemler iskeletinde bulunmalıdır;
  bağlanamayan bir graf eksik adla bildirilir ve dinlenme pozunu gösterir.
- `parseAnimationGraph()` dosyayı bütün olarak doğrular ve sorunun JSON yolunu adlandırır; böylece bir ajan ya da
  bir test onu hemen düzeltebilir:

  ```text
  layers[0].transitions[1].conditions[0].parameter: unknown parameter 'sped'
  layers[0].states[2]: needs exactly one of clip, blend1d or blend2d
  parameters[3].default: a Trigger has no default
  ```

- Kesilemeyen geçişler, alt durum makineleri ve canlı durumu kaydetme
  [Henüz desteklenmeyenler](#henuz-desteklenmeyenler) altında listelenir.

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
