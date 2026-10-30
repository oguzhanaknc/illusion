# Oyun Modülleri ve Hot Reload

Oyun kodu modüllerde yaşar: engine'in yüklediği, her karede çalıştırdığı ve geliştirme sırasında oyun
çalışırken yeniden yüklediği, tek giriş noktalı kütüphaneler. Aynı modül, dışa aktarılan bir oyuna statik
olarak linklenir; böylece dağıtılan build'lerde hiç yükleyici bulunmaz.

Header'lar: `<illusion/core/Module.hpp>` (modül tarafı), `<illusion/modules/ModuleHost.hpp>` (host)

Kütüphane: `illusion::modules` (host); shared modüller yalnızca `illusion::core`'u ve `USES` ile
belirttikleri API hedeflerini linkler

## Modül yazma

```cpp
// game/Tetris.cpp
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>

namespace tetris
{
struct Piece
{
    illusion::i32 column = 0;
    illusion::f32 fall = 0.0f;
};
} // namespace tetris

namespace
{

bool load(illusion::ModuleContext&)
{
    // Register every Component type of the module here, so that a reload can take them over.
    illusion::reflect<tetris::Piece>("tetris.Piece").field("column", &tetris::Piece::column);
    return true;
}

void start(illusion::ModuleContext& context)
{
    // A game session begins on this World: spawn what the game needs.
    context.world().create(tetris::Piece{});
}

void update(illusion::ModuleContext& context, illusion::f64 deltaSeconds)
{
    context.world().query<tetris::Piece>().each(
        [&](tetris::Piece& piece) { piece.fall += static_cast<illusion::f32>(deltaSeconds); });
}

constexpr illusion::ModuleApi kTetris =
    illusion::makeModuleApi("tetris", "1.0.0", {.load = &load, .start = &start, .update = &update});

} // namespace

ILL_MODULE(tetris, kTetris)
```

Bir modül bir `ModuleApi`'dir: bir ad, semantik bir sürüm ve `makeModuleApi()`'ye `ModuleCallbacks`
olarak verilen fonksiyonları. `makeModuleApi()`, derlendiği engine sürümünü ve build ayarlarını doldurur;
`ILL_MODULE()` onu dışa aktarır. `makeModuleApi(name, version, &load, &unload, &update)` biçimi, `start`
ve `stop` kullanmayan modüller için bir kısaltmadır.

| Fonksiyon | Ne zaman çağrılır | Tipik iş |
| --- | --- | --- |
| `load` (zorunlu) | Modül yüklendikten sonra ve her yeniden yüklemeden sonra | Component türlerini kaydetmek ve yansıtmak, handler'lara abone olmak; reddetmek için `false` döndürmek |
| `unload` (isteğe bağlı) | Modül kaldırılmadan veya yeniden yüklenmeden önce | Event handler'larının aboneliğini bitirmek, modülün içini gösteren her şeyi bırakmak |
| `start` (isteğe bağlı) | Her Simulation World için bir kez: World eklendiğinde ya da World ekliyken modül yüklendiğinde | Oyunun Entity'lerini oluşturmak |
| `stop` (isteğe bağlı) | Başlamış her Simulation World için bir kez: World ayrılmadan ya da modül kaldırılmadan önce | Sonuçları kaydetmek, oturum kaynaklarını bırakmak |
| `update` (isteğe bağlı) | Her `ModuleHost::update()`'te, Simulation World başına bir kez | Oyun mantığı |

`load` ve `unload` hiçbir World görmez: orada `context.world()` assert eder ("use start()") ve
`context.hasWorld()` `false` döner. Hot reload `unload` ve `load`'u çağırır ama `stop` ya da `start`'ı
çağırmaz; böylece çalışan bir oyun oturumunu korur.

`ModuleContext`, çağrının World'üne, host'un `JobSystem`'ine ve servislerine erişim verir:
`context.service<T>()`, World'ün eklenirken aldığı `T` türündeki servisi, yoksa host'un `provide()`'a
verdiğini, o da yoksa null döndürür.

## Build

```cmake
# Shared in development builds, static when ILLUSION_SHARED_MODULES is OFF (exported games).
illusion_add_module(tetris SOURCES game/Tetris.cpp USES physics render)
```

`illusion_add_module()`, `ILLUSION_SHARED_MODULES` `ON` iken (varsayılan) bir shared library, aksi halde
statik bir kütüphane üretir. `SHARED` veya `STATIC` birini zorlar. Shared bir modül, linklediği her şeyin
kendi kopyasını taşır ve giriş noktası dışında hiçbir şeyi dışa aktarmaz.

`USES`, modülün header'larına ve düz veri Component'lerine ihtiyaç duyduğu engine kütüphanelerini
adlandırır: `physics`, `audio`, `input`, `render` (`SceneComponents.hpp`'nin kaydedilen, GPU'suz
Component'leri) ve M7q-2'den itibaren `runtime`. Her biri, kütüphanenin kodunu değil, yalnızca header
içeren `illusion::<name>_api` hedefini linkler.

!!! warning "Shared modüller engine kütüphanelerini linklemez"
    Shared bir modülün içindeki bir engine kütüphanesi kopyası, host'un nesneleri üzerinde kendi
    Allocator'ları, factory'leri ve yeniden yüklemeden sonra boşta kalan vtable'larıyla çalışırdı (ADR
    0041). Bu yüzden shared bir modülde `illusion::core` dışındaki bir engine kütüphanesini `LINK` etmek
    bir configure hatasıdır. Header'lar için `USES` kullanın, koda host'un servisleri üzerinden ulaşın.
    `LINK`, üçüncü taraf kütüphaneler ve statik modüllerdeki engine kütüphaneleri için kalır.

## Modülleri çalıştırma

```cpp
illusion::ModuleHost modules(jobs);
illusion::ServiceSet services;
services.provide(settings); // modules call context.service<GameSettings>()
modules.attachWorld(world, illusion::WorldRole::Simulation, std::move(services));
if (const std::optional<std::string> error = modules.load("build/game/libtetris.so"))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
while (running)
{
    modules.reloadChanged(); // picks up a rebuilt library
    modules.update(frameSeconds);
}
modules.detachWorld(world);
```

`load()` kütüphaneyi bir gölge dizine kopyalar ve kopyayı yükler; böylece build orijinalin üzerine
yazabilir. Ardından modülü denetler ve bir sorun varsa bir hata mesajı döndürür:

- modül API sürümü (3; sürüm 2 için derlenmiş modüller World işlerini `start`'a taşımalı ve yeniden
  derlenmelidir), engine sürümü (shared modüller tam olarak bu engine'e karşı derlenmiş olmalıdır) ve
  build ayarları (derleyici ve sürümü, Debug veya Release, assert'ler);
- geçerli ve benzersiz bir ad ile semantik bir sürüm;
- bağımlılıkları.

Host daha sonra modüle süreç genelindeki servislerini (logger, Component registry'si, Asset yol tablosu,
assert handler'ı, thread ve Worker indeksleri) verir, `load`'u ve ardından her Simulation World'de
`start`'ı çağırır. `unload()` ve destructor modülleri, en son yükleneni önce olmak üzere durdurur ve
kaldırır.

## World'ler

Bir host, modülleri her biri bir rolle eklenmiş istenen sayıda World üzerinde çalıştırır:

| `WorldRole` | Kullanım | `start` / `update` / `stop` |
| --- | --- | --- |
| `Edit` | Bir editörün düzenlediği World | Hiçbir zaman |
| `Simulation` | Oyunun çalıştığı World: bir play oturumu, dağıtılan oyun | Evet |
| `Preview` | Bir Asset'i (bir prefab, bir sahne) çalıştırmadan gösteren World | Hiçbir zaman |

`attachWorld(world, role, services)` bir World ekler; bir Simulation World'de yüklü her modülü yükleme
sırasıyla başlatır. `detachWorld(world)` onları en son yükleneni önce olmak üzere durdurur ve World'ün
Entity'lerine dokunmaz. `update(deltaSeconds)` her Simulation World'ü ekleme sırasıyla, her birinde
modülleri yükleme sırasıyla ilerletir; `update(world, deltaSeconds)` yalnızca birini ilerletir.

Bir modül kaldırıldığında, yüklenemediğinde ya da bir yeniden yüklemede bir türü bıraktığında host, o
türlerin Component'lerini, kodları gitmeden önce eklenmiş her World'den kaldırır. Bir World'ü yok etmeden
önce ayırın ve bir modül kaldırıldıktan sonra ayrılmış bir World'de o modülün Component'lerini tutmayın.

## Hot reload

`reloadChanged()`, dosyası değişen her shared modülü yeniden yükler; `reload(name)` birini hemen yeniden
yükler:

1. Eski sürümün `unload`'u çalışır.
2. Yeni kütüphane yüklenir ve `load`'u modülün Component türlerini yeniden kaydeder: registry onları yeni
   koda devreder; böylece var olan Component'ler verilerini korur.
3. Yeni sürümün artık kaydetmediği türlerin Component'leri, eski kod onları yok etmek için hâlâ yüklüyken
   eklenmiş her World'den kaldırılır.
4. Eski kütüphane kapatılır.

Yeni kütüphane yüklenemezse ya da `load`'u başarısız olursa eski sürüm türlerini geri alır ve çalışmaya
devam eder; hata log'a yazılır ve aynı dosya değişene kadar yeniden denenmez.

!!! warning "Yeniden yüklemede ne korunur"
    Yalnızca Component'lerdeki veriler korunur: oyun durumunu modülün global değişkenlerinde değil,
    World'de tutun. Bir modülün Component türünün boyutunu veya hizalamasını değiştirmek reddedilir
    (yeniden yükleme geri alınır); uygulamak için yeniden başlatın.

Yeniden yüklenebilir modüller için kurallar:

- Host, kendi kodunun kullandığı her Component türünün sahibidir ve `ModuleHost` constructor'ı core
  Component'lerini yansıtır. Hiçbir engine türü bir modülün koduna bağlı kalmasın diye oyunun kullandığı
  diğer engine kütüphanelerinin Component'lerini (`reflectPhysicsComponents()`, `reflectAudioComponents()`,
  ...) modülleri yüklemeden önce yansıtın. Araçlar bir modülün kendi türlerini C++ türü olarak değil,
  registry üzerinden (stable name ile) görür.
- Modülün her Component türünü `load`'da kaydedin (veya yansıtın); Entity'leri `start`'ta oluşturun.
- Modülün içini gösteren şeyleri `unload`'da geri alın: event handler'ları, host'a verilen callback'ler.
- Modülün gönderdiği Job'lar, onları gönderen çağrı dönmeden önce biter.
- Profiling build'leri (`ILLUSION_PROFILING`) yalnızca statik modül kullanır.

## Sürümler ve bağımlılıklar

```cpp
constexpr std::array kNeeds{illusion::ModuleDependency{"tetris", "1.2.0"}};
constexpr illusion::ModuleApi kScores =
    illusion::makeModuleApi("tetris.scores", "0.3.0", &load, &unload, &update, kNeeds);
```

Bir modül yalnızca bağımlılıklarından sonra ve yüklü her bağımlılık istenen sürümle uyumluysa (aynı ana
sürüm, daha eski değil; bkz. `SemanticVersion`) yüklenir. Başka bir modül ona bağımlıyken bir modül
kaldırılamaz.

## Dağıtım

Dışa aktarılan bir oyunda modül statiktir: bu durumda `ILL_MODULE(tetris, ...)`
`illusionModuleEntry_tetris()`'i tanımlar ve oyun onu ekler:

```cpp
ILL_DECLARE_STATIC_MODULE(tetris);

illusion::ModuleHost modules(jobs);
modules.attachWorld(world, illusion::WorldRole::Simulation);
if (const auto error = modules.add(*illusionModuleEntry_tetris()))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
```

Statik modüller oyunun engine'ini paylaşır; bu yüzden servise ihtiyaç duymaz ve yeniden yüklenemez.
