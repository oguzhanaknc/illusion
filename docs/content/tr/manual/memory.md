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
yardımcı olur.

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

## Hizalanmış bellek ayırma

`alignedAlloc(size, alignment)` ve `alignedFree(ptr)`, tüm platformlarda ikinin kuvveti olan herhangi
bir alignment ile bellek ayırır.

## Kurallar

- Kare başına çalışan kod yollarında `new`, `malloc` çağırmayın veya container büyütmeyin. Kapasiteyi
  önceden ayırın veya bir arena kullanın.
- Allocator'lar thread-safe değildir. Her thread'e veya Job'a kendi arenasını verin.
- Bellek yetersizliği ölümcüldür: `ILL_CHECK` tetiklenir ve bir mesajla süreç sonlanır.
