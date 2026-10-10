# API Referansı

API referansı, public header'lardaki yorumlardan Doxygen ile üretilir. Her public bildirim belgelenmiştir;
belgelenmemiş bir bildirim olursa CI başarısız olur.

## Referansı yerelde üretme

[Doxygen](https://www.doxygen.nl/) 1.9 veya daha yeni bir sürümü kurun, ardından depo kök dizininden
çalıştırın:

```bash
doxygen docs/Doxyfile
```

`build/api/index.html` dosyasını tarayıcıda açın.

## Public header'lar

| Header | İçerik |
| --- | --- |
| `illusion/core/Base.hpp` | Sabit genişlikli tipler, platform makroları, alignment yardımcıları |
| `illusion/core/Assert.hpp` | `ILL_ASSERT`, `ILL_VERIFY`, `ILL_CHECK`, assert handler'ları |
| `illusion/core/Log.hpp` | `Logger`, `LogSink`, `ILL_LOG_*` makroları |
| `illusion/core/Memory.hpp` | `LinearAllocator`, `PoolAllocator`, hizalanmış bellek ayırma |
| `illusion/core/JobSystem.hpp` | `JobSystem`, `JobCounter`, `Job` |
| `illusion/core/InplaceFunction.hpp` | Bellek ayırmayan, tipi silinmiş (type-erased) çağrılabilir nesne |
| `illusion/core/Thread.hpp` | Thread isimleri, thread indeksleri, spin-wait ipucu |
| `illusion/core/Hash.hpp` | Kararlı FNV-1a hash |
| `illusion/core/TypeName.hpp` | Derleme zamanı tip isimleri |
| `illusion/core/Profiler.hpp` | Tracy profiling makroları |
| `illusion/ecs/World.hpp` | `World`, `Query`, `ChunkView` |
| `illusion/ecs/CommandBuffer.hpp` | Ertelenmiş yapısal değişiklikler |
| `illusion/ecs/Component.hpp` | Component kaydı ve `ComponentRegistry` |
| `illusion/ecs/Entity.hpp` | `Entity` handle'ı |
| `illusion/ecs/Archetype.hpp` | Archetype depolaması (motor içi) |
