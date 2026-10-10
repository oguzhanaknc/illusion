# Profiling

illusion, [Tracy](https://github.com/wolfpld/tracy) profiler için enstrümante edilmiştir. Profiling
açılmadıkça enstrümantasyon hiçbir koda dönüşmez; bu yüzden makrolar yayımlanan kodda kalabilir.

## Profiling'i açma

`*-profile` preset'lerinden biriyle configure edin veya `ILLUSION_PROFILING=ON` ayarlayın:

```bash
cmake --preset linux-clang-profile
cmake --build --preset linux-clang-profile-release
```

Programı çalıştırın ve **0.14.1** sürümlü Tracy profiler arayüzüyle bağlanın; arayüz ve istemci aynı
sürüm olmalıdır. Profiling "isteğe bağlıdır" (on demand): veriler yalnızca bir profiler bağlıyken
toplanır.

## Kodu enstrümante etme

Header: `#include <illusion/core/Profiler.hpp>`

```cpp
void updatePhysics()
{
    ILL_PROFILE_SCOPE(); // zone named after the function
    {
        ILL_PROFILE_SCOPE_N("Broadphase");
        // ...
    }
}

void runFrame()
{
    // ...
    ILL_PROFILE_FRAME(); // marks the end of a frame
}
```

`ILL_PROFILE_THREAD("name")` çağıran thread'e isim verir. Job System worker'ları otomatik olarak
isimlendirilir ve çalıştırılan her Job bir "Job" zone'u olarak görünür.

## Performans bütçeleri

Profiling zamanın nereye gittiğini açıklar; benchmark'lar ise gerilemesini önler. Kritik yolların CI'da
çalışan bütçeli benchmark'ları vardır (bkz. [Başlarken](../getting-started.md#benchmarklar)). Bir
System'i optimize ettiğinizde `benchmarks/` altında bir benchmark ekleyin veya mevcut bütçeyi sıkılaştırın.
