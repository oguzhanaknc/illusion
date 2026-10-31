# Bellek

illusion, kararlı durumdaki karelerde genel amaçlı heap'i kullanmaktan kaçınır. Her karede yapılan bellek
ayırmaları thread'ler arasında çekişmeye yol açar, belleği parçalar ve kare sürelerini öngörülemez
kılar. Core kütüphanesi bu durumlar için iki allocator sunar.

Header: `#include <illusion/core/Memory.hpp>`

## LinearAllocator

Linear allocator (arena), bir ofseti ilerleterek bellek verir. Tek tek serbest bırakma desteklenmez: bir
işaretçiye (marker) geri sararsınız veya tüm arenayı sıfırlarsınız. Kare başına geçici veriler için her
thread'de bir tane kullanın:

```cpp
illusion::LinearAllocator frameArena(4 * 1024 * 1024);

void update()
{
    frameArena.reset();
    auto* visible = frameArena.allocateArray<illusion::Entity>(1024);
    // ... fill and use `visible` during this frame only ...
}
```

`create<T>()` arenada bir nesne oluşturur; arena destructor çalıştırmadığı için `T` trivially
destructible olmalıdır. `peak()` en yüksek kullanım seviyesini bildirir; bu, arena boyutlandırmaya
yardımcı olur. `reallocate(ptr, oldSize, newSize)` bir bloğu yeniden boyutlandırır: en son yapılan
ayırma yerinde büyür, diğer bloklar kopyalanır. JSON ayrıştırıcısı gibi `realloc` gerektiren
kütüphaneler bu sayede bir arena üzerinde çalışabilir.

## PoolAllocator

Pool allocator tek bir sabit boyuttaki blokları yönetir. Belleği sayfalar halinde ayırır ve serbest
bırakılan blokları yeniden kullanır; böylece ayırma ve serbest bırakma birkaç komut sürer:

```cpp
illusion::PoolAllocator pool(sizeof(Particle), alignof(Particle), 256);
void* memory = pool.allocate();
// ...
pool.deallocate(memory);
```

ECS, her World için 16 KiB'lık Chunk'lardan oluşan bir pool kullanır.

## RangeAllocator

Range allocator, size ait bir buffer'ın (örneğin bir GPU vertex havuzunun) içinde eleman aralıkları dağıtır.
Yeterince büyük ilk serbest aralığı alır, yoksa buffer'ın kullanılan kısmını (`end()`) uzatır; çağıran da
buffer'ını bu boyuta büyütür. Serbest bırakılan aralıklar komşularıyla birleşir ve sondaki serbest bir
aralık `end()` değerini küçültür:

```cpp
illusion::RangeAllocator ranges;
const illusion::u32 a = ranges.allocate(1000); // offset 0
const illusion::u32 b = ranges.allocate(500);  // offset 1000
ranges.free(a, 1000);
const illusion::u32 c = ranges.allocate(800);  // offset 0: reuses the freed range
// grow the buffer to ranges.end() elements before writing
```

`RenderResources`, vertex ve index havuzlarını bunlardan ikisiyle yönetir; böylece serbest bırakılıp
yeniden oluşturulan Mesh'ler belleklerini yeniden kullanır.

## Hizalanmış bellek ayırma

`alignedAlloc(size, alignment)` ve `alignedFree(ptr)`, tüm platformlarda ikinin kuvveti olan herhangi
bir alignment ile bellek ayırır.

## Kurallar

- Kare başına çalışan kod yollarında `new`, `malloc` çağırmayın veya container büyütmeyin. Kapasiteyi
  önceden ayırın veya bir arena kullanın.
- Allocator'lar thread-safe değildir. Her thread'e veya Job'a kendi arenasını verin.
- Heap belleğinin tükenmesi ölümcüldür: `ILL_CHECK` tetiklenir ve bir mesajla süreç sonlanır. Dolu bir
  arena ise ölümcül değildir: `allocate()` `nullptr` döndürür ve arena alan API'ler (örneğin
  [JSON](json.md#bellek)) hatayı bir dönüş değeri olarak bildirir.
