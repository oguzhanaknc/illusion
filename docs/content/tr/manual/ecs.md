# ECS

illusion oyun durumunu bir Entity Component System (ECS) içinde saklar. **Entity** hafif bir
tanıtıcıdır (handle), **Component** bir Entity'ye eklenmiş düz veridir; System'ler ise belirli
Component'lere sahip tüm Entity'ler üzerinde **Query** çalıştıran fonksiyonlardır.

Header: `#include <illusion/ecs/World.hpp>`

## Component'ler

Taşınabilir (movable) her C++ tipi Component olabilir. Component'leri küçük ve davranıştan arındırılmış
tutun:

```cpp
namespace game
{
struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Velocity
{
    float x = 0, y = 0, z = 0;
};

struct Player // empty type: a tag
{
};
} // namespace game
```

Kurallar:

- Move constructor ve destructor exception fırlatmamalıdır.
- Her Component tipinin benzersiz, tam nitelikli (fully qualified) bir adı olmalıdır. Component'leri
  anonim namespace'lerde veya fonksiyonların içinde tanımlamayın.
- Boş tipler **tag**'dir: bellek kaplamazlar ve filtreleme için kullanılırlar.
- 64 byte'a kadar alignment desteklenir.

Component tipleri ilk kullanıldıklarında otomatik olarak kaydedilir. Component'leri sahne
dosyalarına kaydetmek için onları yansıtın ([Reflection](reflection.md) ve [Sahneler](scenes.md) sayfalarına bakın).

## Entity'ler

```cpp
illusion::World world;

illusion::Entity player = world.create(game::Position{}, game::Velocity{1, 0, 0}, game::Player{});
illusion::Entity empty = world.create();

world.emplace<game::Velocity>(empty, 0.0f, 1.0f, 0.0f); // add, constructing in place
world.set(empty, game::Position{5, 0, 0});              // add or assign
game::Position& position = world.get<game::Position>(empty);
bool moving = world.has<game::Velocity>(empty);
world.remove<game::Velocity>(empty);

world.destroy(empty);
bool alive = world.isAlive(empty); // false: the handle is stale
```

Bir Entity handle'ı bir indeks ve bir generation içerir. Bir Entity yok edildiğinde yuvası yeni bir
generation ile yeniden kullanılır; böylece eski handle'lar `isAlive()` ile güvenle tespit edilir.

Handle'lar yalnızca onları oluşturan World ve süreç içinde geçerlidir. Kaydedilen veya sahne dosyaları ya
da editör gibi dışarıdan başvurulan Entity'ler ayrıca kalıcı bir id, yani `EntityGuid` Component'ini, ve
isteğe bağlı olarak bir `Name` taşır; bkz. [Entity id'leri](scenes.md#entity-idleri).

!!! warning "Referanslar kısa ömürlüdür"
    `get()`, `tryGet()` ve `emplace()` tarafından döndürülen referans ve pointer'lar bir sonraki yapısal
    değişiklikte (Entity oluşturma veya yok etme, Component ekleme veya çıkarma) geçersiz olur.

## Query'ler

Bir Query, listelenen Component'lerin tümüne sahip her Entity'yi ziyaret eder. Salt okunur erişimi
`const` ile belirtin:

```cpp
world.query<game::Position, const game::Velocity>().each(
    [](game::Position& position, const game::Velocity& velocity) {
        position.x += velocity.x;
        position.y += velocity.y;
        position.z += velocity.z;
    });
```

Çağrılabilir nesne ilk parametre olarak Entity'yi de alabilir: `[](illusion::Entity e, game::Position& p)`.

Filtreler, erişilmeyen Component'lere göre Entity seçer:

```cpp
auto players = world.query<game::Position>().with<game::Player>();
auto others = world.query<game::Position>().without<game::Player>();
illusion::u32 playerCount = players.count();
```

Eşleşen Archetype'lar World başına önbelleğe alınır; bu yüzden her karede Query oluşturmak bellek
ayırmaz.

### Chunk iterasyonu

`eachChunk()` size her seferinde bir Chunk'ın bitişik dizilerini verir; bu, SIMD ve derleyicinin otomatik
vektörleştirmesi için en uygun yapıdır:

```cpp
world.query<game::Position, const game::Velocity>().eachChunk(
    [](illusion::ChunkView<game::Position, const game::Velocity>& chunk) {
        std::span<game::Position> positions = chunk.get<game::Position>();
        std::span<const game::Velocity> velocities = chunk.get<const game::Velocity>();
        for (illusion::u32 i = 0; i < chunk.size(); ++i)
        {
            positions[i].x += velocities[i].x;
        }
    });
```

### Paralel iterasyon

`parallelEach()` Chunk'ları Job System üzerinde işler ve tüm Chunk'lar bittiğinde döner:

```cpp
illusion::JobSystem jobs;
world.query<game::Position, const game::Velocity>().parallelEach(
    jobs, [](game::Position& position, const game::Velocity& velocity) { position.x += velocity.x; });
```

Çağrılabilir nesne eşzamanlı çalışır; yalnızca kendisine verilen Component'lere ve thread'ler arasında
paylaşılması güvenli olan verilere dokunmalıdır.

C++ tipleri yerine `ComponentId`'lerle çalışan serializer'lar ve araçlar aynı Chunk'ları
`World::forEachMatching()` ile dolaşır; bkz. [Tipi silinmiş erişim](scenes.md#tipi-silinmis-erisim).

## İterasyon sırasında yapısal değişiklikler

Bir Query dolaşırken Entity oluşturmak veya yok etmek, Component eklemek veya çıkarmak yasaktır (Debug'da
bir assert tetiklenir). Değişiklikleri bir `CommandBuffer`'a kaydedin ve sonrasında uygulayın:

```cpp
illusion::CommandBuffer commands;
world.query<const game::Position>().each([&](illusion::Entity e, const game::Position& p) {
    if (p.y < -100.0f)
    {
        commands.destroy(e);
    }
});
commands.apply(world);
```

`CommandBuffer`; `destroy()`, `set()`, `remove<T>()` ve `create(components...)` destekler. Komutlar
kaydedildikleri sırayla çalışır. Buffer belleğini kullanımlar arasında korur; bu yüzden System başına
bir buffer'ı yeniden kullanmak kararlı durumda bellek ayırmaz.

## Yapısal sürüm

`World::structuralVersion()`, Entity kümesi veya bir Entity'nin Component kümesi her değiştiğinde bir
artar: bir Entity oluşturmak veya yok etmek, Entity'de olmayan bir Component eklemek, olan birini
çıkarmak ve boş olmayan bir World'ü temizlemek; doğrudan ya da bir `CommandBuffer` üzerinden. Component
değerlerini yazmak onu değiştirmez. Araçlar, önbelleklerini yalnızca World'ün düzeni değiştiğinde yeniden
oluşturmak için bunu kullanır:

```cpp
if (world.structuralVersion() != m_outlinerVersion)
{
    rebuildOutliner(world);
    m_outlinerVersion = world.structuralVersion();
}
```

## Nasıl çalışır

Tam olarak aynı Component tipi kümesine sahip Entity'ler bir **Archetype**'ı paylaşır. Bir Archetype,
Entity'lerini 16 KiB'lık **Chunk**'larda saklar; bir Chunk içinde her Component tipinin kendi sıkı
paketlenmiş dizisi vardır (structure of arrays). Bir Component eklemek veya çıkarmak Entity'yi başka bir
Archetype'a taşır. Bir Entity silindiğinde Archetype'ın son Entity'si boşluğa taşınır; böylece Chunk'lar
yoğun kalır.

Bu tasarımın gerekçesi için bkz. [ADR 0004](../architecture/decisions.md).

## Performans notları

- Çok sayıda `get()` çağrısı yerine Query'leri tercih edin: Query'ler bitişik bellek üzerinden akar.
- Her karede çok sayıda Entity'ye sık sık Component ekleyip çıkarmaktan kaçının; durum sık değişiyorsa
  bir tag veya bayrak alanı kullanın.
- Salt okunur Component'leri Query'lerde `const` olarak işaretleyin. Bu, niyeti belgeler ve ileride
  scheduler'ın System'leri paralel çalıştırmasına olanak tanıyacaktır.
