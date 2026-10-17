# Oyun Modülleri ve Hot Reload

Oyun kodu modüllerde yaşar: engine'in yüklediği, her karede çalıştırdığı ve geliştirme sırasında oyun
çalışırken yeniden yüklediği, tek giriş noktalı kütüphaneler. Aynı modül, dışa aktarılan bir oyuna statik
olarak linklenir; böylece dağıtılan build'lerde hiç yükleyici bulunmaz.

Header'lar: `<illusion/core/Module.hpp>` (modül tarafı), `<illusion/modules/ModuleHost.hpp>` (host)

Kütüphane: `illusion::modules` (host); modüller yalnızca `illusion::core`'a ihtiyaç duyar

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

bool load(illusion::ModuleContext& context)
{
    // Register every Component type of the module here, so that a reload can take them over.
    illusion::reflect<tetris::Piece>("tetris.Piece").field("column", &tetris::Piece::column);
    return true;
}

void update(illusion::ModuleContext& context, illusion::f64 deltaSeconds)
{
    context.world().query<tetris::Piece>().each(
        [&](tetris::Piece& piece) { piece.fall += static_cast<illusion::f32>(deltaSeconds); });
}

constexpr illusion::ModuleApi kTetris = illusion::makeModuleApi("tetris", "1.0.0", &load, nullptr, &update);

} // namespace

ILL_MODULE(tetris, kTetris)
```

Bir modül bir `ModuleApi`'dir: bir ad, bir semantik sürüm ve `load`, `unload` ve `update` fonksiyonları.
`makeModuleApi()`, derlendiği engine sürümünü ve build ayarlarını doldurur; `ILL_MODULE()` onu dışa
aktarır.

| Fonksiyon | Ne zaman çağrılır | Tipik iş |
| --- | --- | --- |
| `load` (zorunlu) | Modül yüklendikten sonra ve her yeniden yüklemeden sonra | Component türlerini kaydetmek ve yansıtmak; reddetmek için `false` döndürmek |
| `unload` (isteğe bağlı) | Modül kaldırılmadan veya yeniden yüklenmeden önce | Event handler'larının aboneliğini bitirmek, modülün içini gösteren her şeyi bırakmak |
| `update` (isteğe bağlı) | Her `ModuleHost::update()`'te | Oyun mantığı |

`ModuleContext`, host'un `World`'üne, `JobSystem`'ine ve servislerine erişim verir: host'un `provide()`'a
verdiği her şey `context.service<T>()` olarak kullanılabilir (host'ta yoksa null).

## Build

```cmake
# Shared in development builds, static when ILLUSION_SHARED_MODULES is OFF (exported games).
illusion_add_module(tetris SOURCES game/Tetris.cpp LINK illusion::physics)
```

`illusion_add_module()`, `ILLUSION_SHARED_MODULES` `ON` iken (varsayılan) bir shared library, aksi halde
statik bir kütüphane üretir. `SHARED` veya `STATIC` birini zorlar. Shared bir modül, linklediği engine
kütüphanelerinin kendi kopyasını taşır ve giriş noktası dışında hiçbir şeyi dışa aktarmaz.

## Modülleri çalıştırma

```cpp
illusion::ModuleHost modules(world, jobs);
modules.provide(physics); // modules call context.service<illusion::PhysicsWorld>()
if (const std::optional<std::string> error = modules.load("build/game/libtetris.so"))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
while (running)
{
    modules.reloadChanged(); // picks up a rebuilt library
    modules.update(frameSeconds);
}
```

`load()` kütüphaneyi bir gölge dizine kopyalar ve kopyayı yükler; böylece build orijinalin üzerine
yazabilir. Ardından modülü denetler ve bir sorun varsa bir hata mesajı döndürür:

- modül API sürümü, engine sürümü (shared modüller tam olarak bu engine'e karşı derlenmiş olmalıdır) ve
  build ayarları (derleyici ve sürümü, Debug veya Release, assert'ler);
- geçerli ve benzersiz bir ad ile semantik bir sürüm;
- bağımlılıkları.

Host daha sonra modüle süreç genelindeki servislerini (logger, Component registry'si, assert handler'ı,
thread ve Worker indeksleri) verir ve `load`'u çağırır. `update()` modülleri yükleme sırasıyla çalıştırır;
`unload()` ve destructor onları, en son yükleneni önce olmak üzere kaldırır.

## Hot reload

`reloadChanged()`, dosyası değişen her shared modülü yeniden yükler; `reload(name)` birini hemen yeniden
yükler:

1. Eski sürümün `unload`'u çalışır.
2. Yeni kütüphane yüklenir ve `load`'u modülün Component türlerini yeniden kaydeder: registry onları yeni
   koda devreder; böylece var olan Component'ler verilerini korur.
3. Yeni sürümün artık kaydetmediği türlerin Component'leri, eski kod onları yok etmek için hâlâ yüklüyken
   World'den kaldırılır.
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
- Modülün her Component türünü `load`'da kaydedin (veya yansıtın).
- `load`'da modülün kendi türlerinin var olan Component'lerine dokunmayın; bunu `update`'te yapın.
- Modülün içini gösteren şeyleri `unload`'da geri alın: event handler'ları, host'a verilen callback'ler.
- Modülün gönderdiği Job'lar, `update`'i dönmeden önce biter.
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

illusion::ModuleHost modules(world, jobs);
if (const auto error = modules.add(*illusionModuleEntry_tetris()))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
```

Statik modüller oyunun engine'ini paylaşır; bu yüzden servise ihtiyaç duymaz ve yeniden yüklenemez.
