# Parçacıklar

Parçacık efektleri, `ParticleEmitter` taşıyan sıradan Entity'lerdir: kıvılcım, duman, toz, büyü.
`ParticleWorld` parçacıklarını CPU'da yayar, hareket ettirir ve emekliye ayırır; bunu deterministik olarak ve
bir efekt başladıktan sonra bellek ayırmadan yapar. Böylece efektler headless oturumlarda, testlerde ve
tekrarlanabilir sabit adımlı play oturumlarında çalışır (ADR 0047). Canlı parçacıkları `SceneRenderer`
çizer (bkz. [Çizim](#cizim)).

Header'lar: `<illusion/particles/ParticleWorld.hpp>`, `<illusion/particles/ParticleComponents.hpp>`

Kütüphane: `illusion::particles`

## Başlarken

```cpp
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>

illusion::reflectParticleComponents(); // once, before scenes are loaded

illusion::World world;
illusion::ParticleWorld particles(world);

illusion::ParticleEmitter sparks;
sparks.rate = 60.0f;                                // particles per second
sparks.lifetimeMin = 0.4f;
sparks.lifetimeMax = 0.9f;
sparks.speedMin = 1.5f;
sparks.speedMax = 3.5f;
sparks.spread = 0.6f;                               // radians around local +Y
sparks.gravityFactor = 0.6f;
sparks.colorStart = illusion::Vec4(6.0f, 3.0f, 1.0f, 1.0f); // HDR: glows through bloom
sparks.colorEnd = illusion::Vec4(2.0f, 0.3f, 0.05f, 0.0f);
world.create(illusion::Transform{}, sparks);

particles.update(1.0f / 60.0f); // every frame; pass a JobSystem* to run the emitters in parallel
```

Bir oyun oturumunda `GameSession::particles()`, fizik ve transform hiyerarşisinden sonra fizik yerçekimiyle
ilerletilir.

## Emitter'lar

| Alan | Anlamı |
| --- | --- |
| `maxParticles` | En fazla canlı parçacık (1 ile 65536 arası); değiştirmek emitter'ın parçacıklarını sıfırdan başlatır. |
| `rate`, `burst` | Yayarken saniyedeki parçacık sayısı ve başlarken bir anda yayılan parçacıklar. |
| `loop`, `duration` | Sonsuza kadar ya da `duration` saniye boyunca yay. |
| `playOnStart` | İlk güncellemede başla; değilse `ParticleWorld::play()` bekle. |
| `lifetimeMin`, `lifetimeMax` | Bir parçacığın yaşadığı saniye; aralıkta rastgele seçilir. |
| `speedMin`, `speedMax` | m/s cinsinden ilk hız; aralıkta rastgele seçilir. |
| `shape`, `radius`, `boxExtents` | Parçacıkların doğduğu yer: `Point`, `Sphere` (yarıçap) ya da `Box` (yarı boyut). |
| `spread` | Yerel +Y etrafındaki ilk yönlerin radyan cinsinden yarı açısı (π: her yön). |
| `gravityFactor`, `drag` | Dünyanın yerçekimini ölçekler (negatif yükselir); saniyede kaybedilen hız oranı. |
| `sizeStart`, `sizeEnd` | Doğumda ve ölümde m cinsinden genişlik. |
| `colorStart`, `colorEnd` | Doğumda ve ölümde doğrusal renk ve alfa; 1'in üzerindeki değerler parlar. |
| `blend` | `Additive` (ışık: ateş, kıvılcım) ya da `Alpha` (örter: duman, toz). |
| `space` | `World`: parçacıklar doğdukları yerde kalır; `Local`: emitter'la birlikte hareket eder. |
| `texture` | Sprite Texture'ı; yoksa yumuşak yuvarlak bir nokta çizilir. |
| `seed` | Eşit seed'ler eşit parçacıklar verir. |
| `simulation` | `Cpu` (varsayılan) ya da `Gpu`: bkz. [GPU simülasyonu](#gpu-simulasyonu). |

Renk ve boyut parçacığın yaşına göre ara değerlenir; `particleColor()` ve `particleSize()` ömrün bir kesri için
değerleri verir.

## Oynatmak ve durdurmak

```cpp
particles.play(entity);   // starts again from the beginning: the burst fires, a timed effect runs again
particles.stop(entity);   // stops emitting; the live particles finish their lives
particles.clear(entity);  // removes the live particles at once
const illusion::u32 alive = particles.aliveCount(entity);
```

`play()` bir efekti birebir yeniden oynatır: her başlangıç `seed`'den aynı rastgele sayıları çeker. Inactive
Entity'lerdeki emitter'lar duraklar.

## Çizim

`SceneRenderer` her canlı parçacığı kameraya bakan bir dörtgen olarak, blend Mesh'lerden sonra ve HDR görüntüye
çizer; böylece 1'in üzerindeki renkler bloom ile parlar:

- `Additive` parçacıklar ışıklarını ekler ve sıra gerektirmez. `Alpha` parçacıklar arkalarındakini örter:
  emitter'lar en uzaktan en yakına çizilir, her birinin parçacıkları da öyle.
- Renderer sahne derinliğini okur: bir yüzeyin arkasındaki parçacık çizilmez, ona yakın olan son 30 cm içinde
  solar (yumuşak parçacıklar); böylece duman zemine keskin çizgiler kesmez.
- Bir `texture`, bir `MeshRenderer`'ın Mesh'leri gibi `RenderAssetResolver` tarafından çalışma zamanı
  Component'i `RenderParticles`'a çözülür; yoksa parçacık yumuşak yuvarlak bir noktadır.
- Renk ve boyut, parçacıklar kare için toplanırken parçacığın yaşından hesaplanır.

`RenderStats::particles` çizilen parçacıkları sayar. `render/100k-particles` GPU Benchmark'ı 100.000 parçacıklı
tam bir kareyi ölçer.

## Editörde

Scene görünümü edit World'deki her emitter'ı önizler: `EditorContext` onları her karede ilerletir; böylece bir
efekti Inspector'da ayarlarken oynadığını görürsünüz. Bir play oturumu çalışırken play World'ün emitter'ları
oyunundur ve onları oturum ilerletir. Parçacıklar çalışma durumudur: önizleme sahne dosyasını ya da hash'ini
asla değiştirmez.

`particles.*` [Editor commands](../reference/editor-commands.md), kişiler ve ajanlar için etkin World'ün
(oynarken play World'ün) emitter'larını yönetir; geri alınamaz:

| Komut | Ne yapar |
| --- | --- |
| `particles.play`, `particles.stop`, `particles.clear` | Bir emitter'ı yeniden başlatır, yaymayı durdurur, parçacıklarını siler |
| `particles.emit` | Hemen `count` parçacıklık bir patlama; `emitted` kaçının sığdığını söyler |
| `particles.stats` | Emitter'lar, yayanlar ve canlı parçacıkları (`entity` ile tek bir emitter) |

```json
{"method": "particles.emit", "params": {"entity": "/Sparks", "count": 40}}
{"method": "particles.stats", "params": {}}
```

## Oyun modülleri

Oyun kodu kendi World'ünün parçacıklarına `ParticleService` üzerinden ulaşır (`USES particles`, bkz.
[scripting API](../reference/scripting-api.md#particleservice)):

```cpp
#include <illusion/particles/ParticleService.hpp>

void onLanding(illusion::ModuleContext& context, illusion::Entity dust)
{
    if (illusion::ParticleService* particles = context.service<illusion::ParticleService>())
    {
        particles->play(dust);          // the dust cloud from the beginning
        if (particles->aliveCount(dust) == 0)
        {
            particles->emit(dust, 20);  // and a puff right now
        }
    }
}
```

## GPU simülasyonu

`simulation` `Gpu` olduğunda bir emitter'ın parçacıkları GPU'da yaşar: renderer onları compute Shader'larda
yaşlandırır, hareket ettirir, emekliye ayırır ve doğurur; durum buffer'ından dolaylı bir çizimle çizer. Böylece
bir milyon parçacıklı bir kare, bir CPU geçişi ve bir yükleme yerine bir iki milisaniyeye mal olur
(`render/1m-gpu-particles` GPU Benchmark'ı). Her emitter en fazla 65.536 parçacık tutar.

Emitter'ın zamanını yine `ParticleWorld` tutar: her güncelleme bir adım yayınlar (doğumları ve saniyeleri,
numaralı) ve emitter'ı çizen her görünüm her yeni adımı simüle eder. Adım numarası doğumların seed'idir; böylece
Scene görünümü ile Game görünümü aynı parçacıkları gösterir. Karşılığında:

- Parçacıklar `ParticlePool`'da değildir: kod ve testler konumlarını okuyamaz. `aliveCount()` ve
  `particles.stats`, bir görünümün birkaç kare önce GPU'dan geri okuduğu sayıyı bildirir.
- Yalnızca bir görünüm onları çizdiğinde hareket ederler; bir emitter'ı bir süredir çizmeyen görünüm her adımı
  değil, birkaç adımı yetiştirir.
- Alpha parçacıklar sıralanmadan, yaşadıkları sırayla çizilir.

Kodun okuduğu ya da testlerde birebir tekrarlanması gereken oyun efektleri için CPU simülasyonunu; ateş
böcekleri, kar, yağmur, közler gibi büyük ve süs amaçlı olanlar için GPU'yu kullanın.

## Parçacık havuzu

`ParticleWorld` her emitter'a bir `ParticlePool` verir (çalışma durumu, kaydedilmez). `[0, count)` aralığındaki
parçacıklar canlıdır ve yapı dizisi (SoA) biçimindedir: `positions(axis)`, `velocities(axis)`, `ages()` ve
`lifetimes()`. `World` uzayında dünya konumlarıdır; `Local` uzayında emitter'ın uzayındadır. Havuz her kapasite
için bir kez ayrılır; ölen bir parçacığın yerini son canlı parçacık alır.

## Performans

`ParticleWorld::update()` emitter'ları Job System üzerinde paralel Job'lar olarak çalıştırır ve havuzlar
oluştuktan sonra bellek ayırmaz. `particles/update` Benchmark'ı canlı parçacık başına maliyeti ölçer.
