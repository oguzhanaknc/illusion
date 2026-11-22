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

## Socket'ler

Bir socket, bir Entity'yi bir ekleme asar: elde bir fener, sırtta bir kılıç, başta bir şapka. Entity'yi
Animator'ın Entity'sinin çocuğu yapın ve ona bir `BoneAttachment` verin (`illusion.BoneAttachment`, kaydedilir,
Inspector'ın Animation kategorisinde):

| Alan | Anlamı |
| --- | --- |
| `joint` | Ebeveynin iskeletindeki eklemin adı (glTF node adı) |
| `translation` | Eklemin uzayında, metre cinsinden kaydırma |
| `rotation` | Eklemin uzayında kaydırma dönüşü |
| `scale` | Kaydırma ölçeği |

Her Animation update'inden sonra, editör zamanı kaydırırken de, çocuğun `LocalTransform`'u eklemin model
uzayındaki dönüşümü ile kaydırmanın çarpımı olur ve `Transform`'u ebeveyninkinden çıkar. Kaydırma eklemle
birlikte hareket eder: (0, 0, 0,1)'lik bir öteleme, eklemin kendi z ekseni boyunca 10 cm'dir. Bağlı Entity'nin
kendi çocukları onu her zamanki gibi izler. `AnimationService::jointTransform()` bir modüle aynı yeri, bir
Entity olmadan, dünya uzayında verir.

```cpp
illusion::BoneAttachment socket;
socket.joint = "hand_r";
socket.translation = illusion::Vec3(0.0f, 0.05f, 0.1f);
const illusion::Entity lantern = world.create(illusion::Transform{}, socket);
static_cast<void>(illusion::setParent(world, lantern, hero, illusion::ParentMode::KeepLocal));

// Where the hand is, in world space, after the last Animation update.
const illusion::Expected<illusion::Mat4, illusion::AnimationError> hand =
    animation->jointTransform(hero, "hand_r");
```

- Eklem adı iskelet başına bir kez aranır; `joint` değişince ya da model yeniden yüklenince yeniden aranır.
- Eksik bir eklem ya da `Animator`'ı olmayan bir ebeveyn (veya hiç ebeveyn olmaması) Entity başına bir kez
  loglanır ve Entity olduğu yerde kalır. Ebeveynin modeli yüklenirken Entity mesajsız olarak kendi hâline
  bırakılır.
- Inactive Entity'ler atlanır: devre dışı bir ebeveyn pozunu korur ve bağlı çocukları yerinde kalır.
- [Oyun oturumunda](runtime.md#adm-sras) socket'ler root motion'dan sonra ve fizikten önce yerleştirilir;
  böylece bağlı bir Entity'deki `Kinematic` bir `RigidBody` aynı adımda socket'e sürülür ve dinamik
  gövdeleri iter. Bağlı bir Entity'de `Dynamic` bir gövde desteklenmez: her update'te socket'e ışınlanır ve
  hızını kaybeder. Onu `Kinematic` yapın ya da düşmesi için `BoneAttachment`'ı kaldırın.
- Kendisi de bağlı olan bir Entity'deki socket (bağlı bir koldaki el) aynı update'te yerleştirilir, ama
  fizik onun dünya konumunu bir update geç görebilir.

## Look-at

Animator'ın Entity'sindeki bir `LookAt` (`illusion.LookAt`, kaydedilir, Inspector'ın Animation
kategorisinde), animasyon pozu yazdıktan sonra bir başı bir hedefe çevirir:

| Alan | Anlamı |
| --- | --- |
| `joint` | Baş eklemi |
| `chainLength` | Dönüşü paylaşan eklemler: baş ve en çok üç atası (1 ile 4 arası) |
| `target` | Bakılacak Entity; yoksa `point` kullanılır |
| `point` | `target` yokken bakılacak dünya uzayındaki nokta |
| `forward` | Başın kendi uzayındaki ileri ekseni (varsayılan +z) |
| `maxAngle` | Animasyondaki yönden en büyük dönüş, radyan cinsinden (varsayılan 70°) |
| `weight` | Dönüşün [0, 1] aralığındaki payı; 0 look-at'i kapatır |
| `speed` | Oyun çalışırken başın hedefi izleme hızı, saniyede radyan; 0 hemen izler |

- Dönüş zincir boyunca eşit paylaşılır; boyun ve omurga her biri biraz döner. Her eklem animasyondaki yönden
  en kısa yay boyunca döner, böylece baş animasyondaki eğimini korur.
- Animasyondaki yönden `maxAngle`'dan uzak bir hedef `maxAngle`'a kadar izlenir. Daha arkada dönüş
  azalarak söner ve tam başın arkasındaki bir hedef hiç dönüş almaz: karakterin arkasından geçen bir hedef
  başı bir omuzdan ötekine hiçbir zaman çevirip atmaz.
- Oyun çalışırken baş hareket eden bir hedefi animasyondaki yönden başlayarak saniyede en çok `speed` radyan
  izler. Play modu dışındaki editör dönüşün tamamını hemen gösterir.

```cpp
illusion::LookAt look;
look.joint = "head";
look.chainLength = 2; // the head and the neck
look.target = player;
const illusion::Entity guard = world.create(illusion::Transform{}, animator, look);

// From a module: look at a point instead (this clears the target Entity).
static_cast<void>(animation->setLookAtTarget(guard, illusion::Vec3(0.0f, 1.6f, 4.0f)));
```

## Two-bone IK

Bir `TwoBoneIk` (`illusion.TwoBoneIk`, kaydedilir) bir uç eklemi, bir ayağı ya da bir eli, ebeveyni ve
büyük ebeveyniyle, örneğin bir dizle ve bir kalçayla, bir hedefe taşır:

| Alan | Anlamı |
| --- | --- |
| `joint` | Uç eklem; ebeveyni ve büyük ebeveyni zinciri oluşturur |
| `target` | Konumu hedef olan Entity; yoksa `point` kullanılır |
| `point` | `target` yokken dünya uzayındaki hedef |
| `pole` | Orta eklemin doğru büküldüğü Entity; yoksa `polePoint` kullanılır |
| `polePoint` | `pole` yokken Animator'ın model uzayındaki pole (+z: dizler öne bükülür) |
| `weight` | IK'nin [0, 1] aralığındaki payı; 0 animasyondaki uzvu gösterir |
| `alignToTarget` | Uç ekleme hedef Entity'nin dönüşünü de ver (foot placement ile: zeminin) |

Çözüm analitiktir (kosinüs teoremi): kemikler boylarını korur, orta eklem kök, hedef ve pole'ün düzleminde
bükülür ve erişilemeyen bir hedef uzvu ona doğru dümdüz uzatır. Kökten hedefe giden doğru üzerindeki bir pole,
animasyonun verdiği bükülmeyi korur. Sıfır uzunluklu kemikler ve kök üzerindeki hedefler hatasız bırakılır.

Bir Entity bir türden tek bir Component taşır; bu yüzden bir `TwoBoneIk` kendi Entity'sinin Animator'ına, o
yoksa ebeveyninin Animator'ına etki eder: bir uzvu Animator'ın Entity'sine, ötekileri çocuklarına koyun.

```cpp
illusion::TwoBoneIk hand;
hand.joint = "hand_r";
hand.target = doorHandle;
hand.alignToTarget = true;
const illusion::Entity reach = world.create(illusion::Transform{}, hand);
static_cast<void>(illusion::setParent(world, reach, hero, illusion::ParentMode::KeepLocal));

// From a module: blend the hand in, or aim it at a point.
static_cast<void>(animation->setIkWeight(hero, "hand_r", 0.5f));
static_cast<void>(animation->setIkTarget(hero, "hand_r", illusion::Vec3(1.0f, 1.2f, 0.5f)));
```

Aşamalar her Animator'ın Job System üzerindeki değerlendirmesinde, graf ya da klip ve root motion'dan sonra ve
model uzayı matrislerinden önce çalışır: önce look-at, sonra Entity'lerinin sırasıyla uzuvlar. Hedefler
update başlarken World'den alınır ve Animator'ın `Transform`'uyla, ölçek dahil, onun model uzayına çevrilir.
Eksik bir eklem bir kez loglanır; ekleminin büyük ebeveyni olmayan bir uzuv yok sayılır. Socket'ler ve
`jointTransform()` çözülmüş pozu görür.

## Foot placement

Animator'ın Entity'sindeki bir `FootPlacement` (`illusion.FootPlacement`, kaydedilir), oyun çalışırken (Play
modu ve oynatıcı) ayakları engebeli zeminde tutar. Ayakları, Animator'ın zinciri pelvis ekleminden sarkan
(bir uyluğun ebeveyni pelvistir) `TwoBoneIk` uzuvlarıdır; en çok dört tane:

| Alan | Anlamı |
| --- | --- |
| `pelvis` | Alçaktaki ayağın zemine erişebilmesi için indirilen pelvis eklemi |
| `rayLength` | Işınların kalça yüksekliğinin ne kadar altına uzandığı, metre cinsinden |
| `footHeight` | Ayak bileğinin zeminden yüksekliği, metre cinsinden |
| `maxStep` | Düzeltilen en büyük yükseklik farkı, metre cinsinden |
| `speed` | Düzeltmelerin izleme hızı, saniye başına; 0 hemen izler |
| `layerMask` | Işınların çarptığı fizik katmanları |

[Oyun oturumu](runtime.md#adm-sras) her Animation adımından önce, son update'in animasyon pozundaki kalça
yüksekliğinden her ayağın altına dümdüz aşağı bir ışın atar. Işın çarparsa ayak, zemine ve Entity'nin
orijininden animasyondaki yüksekliğine (en az `footHeight`) kadar, en çok `maxStep` kadar kaldırılır ya da
indirilir; `alignToTarget` ile zeminin eğimine yatar. Pelvis en büyük indirme kadar düşer, böylece alçak
zeminin üstündeki bacak ona erişebilir. Işını hiçbir şey bulamayan bir ayağın IK'si yavaşça söner. Her
değişiklik `speed` ile izlenir; basamaklar ve kenarlar sıçrama yapmaz.

```cpp
illusion::FootPlacement feet;
feet.pelvis = "hips";
feet.rayLength = 1.6f;
feet.layerMask = 1u << 0; // the ground's layer only
world.emplace<illusion::FootPlacement>(hero, feet);
// The legs: TwoBoneIk limbs on "foot_l" (on hero) and "foot_r" (on a child).
```

- Animator'ın Entity'si animasyonunun zemin seviyesidir: klipte basan bir ayak zemine basmaya devam eder,
  klipte kalkmış bir ayak da o kadar kalkık kalır.
- Işınlar karakteri taşıyan gövdeyi (Animator'ın Entity'sinde ya da bir atasında bir `CharacterController`
  veya `RigidBody`) atlar ve karakterin socket'lerdekiler gibi öteki gövdelerinin altından geçer. Kendi
  katmanında, `layerMask` dışında duran karakterlerin buna hiç ihtiyacı yoktur.
- Işınlar son update'in pozundan başlar; ayaklar zemini bir update geriden izler ve bu oyun kare hızlarında
  görünmez. `engine/animation` fizik kullanmaz: oturum sonuçları, sonraki Animation adımının uyguladığı
  runtime `FootPlacementPose`'a yazar.
- Çalışan bir oturum yokken (Play modu dışındaki editör) ayaklar animasyondaki pozu korur.

## Spring bones

Animator'ın Entity'sindeki bir `SpringBone` (`illusion.SpringBone`, kaydedilir, Inspector'ın Animation
kategorisinde), animasyon pozladıktan sonra bir eklem zincirinin (saç, kuyruk, pelerin kenarı) eylemsizlik ve
yerçekimiyle sallanmasını sağlar:

| Alan | Anlamı |
| --- | --- |
| `root` | Zincirin ilk eklemi; animasyondaki yerinde kalır, altındaki her eklem sallanır |
| `stiffness` | Adım başına animasyon pozuna doğru çekme, [0, 1] aralığında (varsayılan 0,6) |
| `damping` | Adım başına kaybedilen hız payı, [0, 1] aralığında (varsayılan 0,2) |
| `gravity` | Dünya uzayındaki ivme, m/s² cinsinden (varsayılan 0, -9,81, 0) |
| `radius` | Çarpışmalar için her zincir ekleminin yarıçapı, metre cinsinden |
| `colliders` | Zincirin dışında kaldığı en çok dört küre: `joint`, eklemin uzayında `offset` (metre), `radius` (metre); `joint`'i boş bir girdi kapalıdır |

Kökün altındaki her eklem, animasyon pozunun onu simüle edilen ebeveynine göre koyduğu yere doğru çekilir,
kemik uzunluğunu korur ve collider'ların dışına itilir. Simülasyon dünya uzayında Verlet entegrasyonu
kullanır; bu yüzden hareket eden ya da dönen bir karakter zinciri de sürükler.

```cpp
illusion::SpringBone tail;
tail.root = "tail_01";
tail.stiffness = 0.35f;
tail.damping = 0.3f;
tail.colliders[0] = {"hips", illusion::Vec3(0.0f, 0.0f, -0.1f), 0.15f}; // keeps the tail off the body
world.emplace<illusion::SpringBone>(hero, tail);
```

- Adımlar 1/60 sn'de sabittir: bir update süresinin içine sığdığı kadar, en çok dört adım çalıştırır ve
  kalanı sonraki update'e taşır. Sonuç yalnızca pozlara ve update sürelerine bağlıdır; bu yüzden
  `GameSessionDesc::fixedStep` ile çalışan bir oyun her çalıştırmada aynı şekilde sallanır.
- Bir zincir en çok 32 eklemlidir: kökün altındaki eklemler, iskeletin sırasıyla. Daha uzun bir zincirde
  yalnızca ilk 32 eklem sallanır.
- Collider'lar aynı iskeletin eklemlerindeki kürelerdir. Zincir fizik dünyasıyla çarpışmaz: gövdeler ve zemin
  onu durdurmaz.
- Eksik bir `root` ya da collider eklemi bir kez loglanır; kökü olmayan bir zincir hiçbir şey yapmaz.
- Bir update zamanı ilerletmediğinde (Play modu dışındaki editör, scrubbing) zincir animasyon pozunu
  gösterir ve durgun başlar.
- Zincir look-at ve IK'dan önce çalışır; bu yüzden onlar sallanmış eklemleri görür.

## Ragdolls

Animator'ın Entity'sindeki bir `Ragdoll` (`illusion.Ragdoll`, kaydedilir, Inspector'ın Animation
kategorisinde), `active` olduğu sürece animasyonlu karakteri bir fizik ragdoll'una çevirir: kapsül gövdeler
eklemleri sürer, düşer, dünyayla çarpışır ve durulur.

| Alan | Anlamı |
| --- | --- |
| `profile` | Şekilleri ve sınırları içeren bir `.ragdoll` dosyası; yoksa iskeletten bir profil üretilir |
| `active` | Karakter animasyonla değil, fizikle simüle edilir |
| `blendSeconds` | Animasyon pozundan simüle edilen poza ve geri harmanlama süresi, saniye cinsinden (varsayılan 0,25) |
| `mass` | Toplam kütle, kg cinsinden; parçalar arasında hacme göre paylaşılır (varsayılan 70) |
| `layer` | Parçaların fizik katmanı, [0, 15] aralığında |

`active` true olduğunda [oyun oturumu](runtime.md#adm-sras) ragdoll'u son Animation adımının pozundan ve
karakterin sahip olduğu hızdan (bir `CharacterController`'ın ya da bir gövdenin) başlatır; böylece koşarken
vurulan bir karakter öne doğru düşer. Her fizik adımı sonra parçaların dünya Transform'larını yayımlar ve
Animation adımı onları `blendSeconds` boyunca poza harmanlar. `active` false olduğunda ragdoll kaldırılır ve
poz aynı sürede animasyona geri harmanlanır. Ragdoll'lar oyun oturumunda (Play modu ve oynatıcı) çalışır;
Play modu dışındaki editör animasyonu gösterir.

```cpp
illusion::Ragdoll ragdoll;
ragdoll.profile = illusion::AssetRef("assets:/animations/strider.ragdoll");
ragdoll.mass = 55.0f;
ragdoll.layer = 1;
world.emplace<illusion::Ragdoll>(hero, ragdoll);

// From a module: fall, and stand up again later.
static_cast<void>(animation->setRagdoll(hero, true));
static_cast<void>(animation->setRagdoll(hero, false));
```

[`AnimationService::setRagdoll()`](../reference/scripting-api.md#animationservice) bir modül için `active`
değerini yazar. Showcase projesindeki Fallen Strider ragdoll olarak başlar, modülünün 90. karesinde ayağa
kalkar ve 200. karede yeniden düşer.

### Profiller

Bir `.ragdoll` dosyası (`"format": "illusion.ragdoll"`, sürüm 1) gövdeleri, ebeveynler çocuklardan önce
gelecek şekilde listeler. Her parça, eklemin kemiği boyunca uzanan bir kapsüldür ve eklemindeki bir
kısıtlamayla en yakın ata ekleminin parçasına bağlanır. Parçası olmayan bir eklem, üstündeki parçayla
birlikte hareket eder. Bu, showcase projesinin Strider'ıdır
(`samples/showcase/assets/animations/strider.ragdoll`), kısaltılmış:

```json
{
  "format": "illusion.ragdoll",
  "version": 1,
  "parts": [
    { "joint": "hips", "radius": 0.12, "halfHeight": 0.1, "rotation": [0, 0, 0.7071068, 0.7071068], "massShare": 0.3 },
    { "joint": "thigh_l", "radius": 0.06, "halfHeight": 0.145, "offset": [0, -0.205, 0] },
    { "joint": "shin_l", "radius": 0.06, "halfHeight": 0.145, "offset": [0, -0.205, 0],
      "swingLimit": 0.5, "twistMin": -0.1, "twistMax": 0.1 }
  ]
}
```

| Bir parçanın anahtarı | Anlamı |
| --- | --- |
| `joint` | Eklemin adı; zorunludur ve her eklem bir kez geçer. İskelette olmayan bir eklem atlanır |
| `radius` | Kapsülün yarıçapı, metre cinsinden; pozitif olmalıdır (varsayılan 0,05) |
| `halfHeight` | Kapsülün silindir uzunluğunun yarısı, metre cinsinden (varsayılan 0,1; 0 bir küre yapar) |
| `offset` | Kapsülün merkezi, eklemin uzayında, `[x, y, z]` metre cinsinden |
| `rotation` | Kapsülün eklemin uzayındaki yönelimi, bir quaternion `[x, y, z, w]`; kapsülün ekseni +y'dir |
| `massShare` | `Ragdoll::mass`'in payı, [0, 1] aralığında; 0 olan parçalar kalanı hacme göre paylaşır; paylar en çok 1 eder |
| `swingLimit` | Ebeveyn parçaya koni sınırı, radyan cinsinden [0, π] aralığında (varsayılan 45°) |
| `twistMin`, `twistMax` | Twist sınırları, radyan cinsinden [-π, 0] ve [0, π] aralığında (varsayılan -30° ve 30°) |

Hatalar, `.animgraph` hataları gibi JSON yolunu adlandırır (`parts[2].radius: must be positive`). Okunamayan
bir dosya bir kez loglanır ve `active` kapatılıp yeniden açılana kadar ragdoll başlatılmaz. Bir profil bir
kez yüklenir ve dosyası değişince yeniden yüklenir.

`profile` olmadan oturum modelden bir tane üretir: vertex'leri 4 cm'den fazla yayılan her eklem için kemik
boyunca çocuğuna doğru yerleştirilmiş bir kapsül, yukarıdaki varsayılan sınırlarla. Bu, her rig'li modele
çalışan bir ragdoll verir; elle yazılmış bir profil daha iyi şekiller ve sınırlar verir.

### Ragdoll çalışırken askıya alınanlar

- Animator'ın **root motion**'ı uygulanmaz ve Entity'nin `CharacterController`'ı ragdoll kaldırılana kadar
  simülasyondan çıkarılır.
- Entity'nin kendi `Transform`'u ragdoll başladığında bulunduğu yerde kalır: simüle edilen parçalar Entity'yi
  değil, eklemleri hareket ettirir. `active`'i kapatmadan önce Entity'yi karakterin yattığı yere taşıyın;
  böylece karakter eski yerinde değil orada ayağa kalkar. `AnimationService::jointTransform()` simüle edilen
  eklemi dünya uzayında verir:

```cpp
// The simulated hips, from the last update: where the character lies.
const illusion::Expected<illusion::Mat4, illusion::AnimationError> hips =
    animation->jointTransform(hero, "hips");
if (hips)
{
    illusion::Transform& transform = world.get<illusion::Transform>(hero);
    transform.position = illusion::Vec3((*hips)[3].x, transform.position.y, (*hips)[3].z);
    static_cast<void>(animation->setRagdoll(hero, false));
}
```

- Ragdoll harmanlaması pozun son aşamasıdır, look-at ve IK'dan sonra gelir: simüle edilen bir karakter
  ragdoll'un pozunu onlar olmadan gösterir ve geri harmanlama ragdoll'un pozundan başlar.
- Socket'ler harmanlanmış pozu izler: bir ragdoll'un elindeki fener onunla birlikte düşer.

### Kaldırma

Ragdoll, `active` false olduğunda, Entity yok edildiğinde ya da `Inactive` olduğunda veya `Ragdoll`
kaldırıldığında kaldırılır ve karakteri simülasyona geri döner. Başlayamayan bir ragdoll (hiçbir parça
iskelete uymuyor ya da fizik dünyasının gövdeleri bitti) reddedilir ve bir kez loglanır; yeniden denemek için
`active`'i kapatıp açın. Model ya da `.ragdoll` dosyası hâlâ yüklenirken ragdoll, hazır olur olmaz başlar.

!!! note "Pasif ragdoll'lar"
    Bir ragdoll yalnızca düşer. Parçaların animasyonu izlemesini sağlayan motorlar (powered ragdoll'lar) ve
    kapsül dışındaki şekiller henüz yoktur.

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
- İkiden fazla kemikli IK zincirleri ve pelvisten sarkan
  bacaklardan fazlası için foot placement (dört bacaklı bir rig'in ön bacakları gibi).
- Klip önizlemeleri, bir zaman çizelgesi, iskelet debug çizimi ve `animation.*` editör komutları (ANIM-4).
- Morph target'lar, vertex başına dörtten fazla etki ve anahtar kare sıkıştırma.

## Performans

- Eklemler Entity değil, bitişik dizilerde satırlardır: 60 eklemli 300 karakteri pozlamak tek bir
  masaüstü çekirdeğinde yaklaşık 1,5 ms sürer (Benchmark `animation/evaluate-300x60`) ve Job System'e
  yayılır.
- 300 Animator üzerindeki 600 socket'i yerleştirmek bir update'e yaklaşık 0,05 ms ekler (Benchmark
  `animation/sockets-300`).
- 300 Animator'ın her birinde üç eklemlik bir look-at ve iki IK uzvu bir update'e yaklaşık 0,1 ms ekler
  (Benchmark `animation/ik-300`).
- 300 Animator'ın her birinde 8 eklemlik bir spring zinciri, bir look-at ve IK uzuvları
  `animation/world-update`'e yaklaşık 0,17 ms ekler (0,26 ms yerine 0,43 ms).
- Yirmi ragdoll yaklaşık 0,17 ms'de simüle edilir (Benchmark `physics/ragdolls-20`, bütçe
  1 ms).
- Skinning, Mesh'i kaç pass çizerse çizsin karede bir kez çalışır: 10.240 vertex'lik 300 instance bir
  NVIDIA GeForce RTX 4070 SUPER'da 0,66 ms sürer (GPU testi 3 ms'lik bir bütçe koyar).
- Görünür her skinned instance vertex başına 64 bayt GPU belleğine (pozlanmış vertex ve önceki konumu) ve
  pass başına bir draw'a mal olur. Binlerce özdeş karakter instanced skinning gerektirir; bu henüz yoktur.
