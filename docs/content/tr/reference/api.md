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
| `illusion/core/Hash.hpp` | Kararlı FNV-1a ve xxHash64 hash'leri |
| `illusion/core/Serialization.hpp` | `BinaryWriter`, `BinaryReader` (sınır denetimli, little-endian) |
| `illusion/core/TypeName.hpp` | Derleme zamanı tip isimleri |
| `illusion/core/Profiler.hpp` | Tracy profiling makroları |
| `illusion/core/EventBus.hpp` | `EventBus`, `Subscription` |
| `illusion/core/Time.hpp` | `monotonicSeconds`, `Stopwatch`, `FrameClock`, `FixedTimestep` |
| `illusion/core/Version.hpp` | `SemanticVersion`, `engineVersion` |
| `illusion/core/Math.hpp` | glm tabanlı vektör, matris ve quaternion tipleri, `Aabb`, `Sphere`, `Frustum`, `perspectiveReversedZ` |
| `illusion/ecs/World.hpp` | `World`, `Query`, `ChunkView` |
| `illusion/ecs/CommandBuffer.hpp` | Ertelenmiş yapısal değişiklikler |
| `illusion/ecs/Component.hpp` | Component kaydı ve `ComponentRegistry` |
| `illusion/ecs/Entity.hpp` | `Entity` handle'ı |
| `illusion/ecs/Reflection.hpp` | `reflect`, `ComponentReflection`, `FieldInfo`, `EnumReflection` |
| `illusion/ecs/SceneSerializer.hpp` | `saveScene`, `loadScene` (JSON sahne dosyaları) |
| `illusion/ecs/Transform.hpp` | `Transform` Component'i (konum, dönüş, ölçek) |
| `illusion/ecs/Archetype.hpp` | Archetype depolaması (motor içi) |
| `illusion/platform/Platform.hpp` | `Platform`: pencere sisteminin yaşam döngüsü ve event pompalama |
| `illusion/platform/Window.hpp` | `Window`, `WindowDesc`, `CursorMode` |
| `illusion/platform/Input.hpp` | `InputState`, `Key`, `MouseButton` |
| `illusion/platform/Events.hpp` | Pencere ve input event'leri |
| `illusion/platform/Archive.hpp` | `ArchiveReader`, `writeArchive`, `packDirectory` |
| `illusion/platform/FileSystem.hpp` | `VirtualFileSystem`, platform dizinleri |
| `illusion/input/Gamepad.hpp` | `Gamepad`, `GamepadState`, `TriggerEffect`, gamepad event'leri |
| `illusion/input/GamepadSystem.hpp` | `GamepadSystem`, `VirtualGamepad` |
| `illusion/input/InputActions.hpp` | `InputActions`, `InputBinding`, `BindingCapture`, glyph'ler |
| `illusion/assets/Image.hpp` | `Image`, çözme, PNG kodlama, mip üretimi |
| `illusion/assets/Mesh.hpp` | `MeshData`, `Vertex`, prosedürel Mesh'ler |
| `illusion/assets/Model.hpp` | `ModelData`, `MaterialData`, `loadGltf` |
| `illusion/assets/AssetFormat.hpp` | `AssetId`, `AssetFile`, hazırlanmış görüntü ve model formatları |
| `illusion/assets/AssetDatabase.hpp` | `AssetDatabase`: görüntüler ve modeller için içe aktarma önbelleği |
| `illusion/assets/AssetStreamer.hpp` | `AssetStreamer`, `AssetHandle`: asenkron yükleme |
| `illusion/assets/AssetCooker.hpp` | `cookAssets`: bir dizini önceden içe aktarma |
| `illusion/assets/Environment.hpp` | Equirectangular eşleme, `ShIrradiance`, `makeSkyImage` |
| `illusion/render/RenderDevice.hpp` | `RenderDevice`, `GpuInfo` (bindless ve multi-draw desteği dahil): NVRHI üzerinden Vulkan cihazı |
| `illusion/render/Swapchain.hpp` | `Swapchain`: bir pencereye present etme |
| `illusion/render/DebugTriangle.hpp` | Tanılama amaçlı üçgen renderer'ı |
| `illusion/render/RenderResources.hpp` | `RenderResources`: GPU Mesh'leri, Texture'ları, Material'leri, ortamlar ve Handle'ları |
| `illusion/render/SceneRenderer.hpp` | `SceneRenderer`, `RenderView`, `RenderStats` |
| `illusion/render/Shadows.hpp` | `ShadowSettings`, `ShadowCascades`, `computeShadowCascades` |
| `illusion/render/UploadQueue.hpp` | `UploadQueue`: streaming ile gelen modellerin byte bütçeli GPU yüklemesi |
| `illusion/render/PostProcess.hpp` | `PostProcessSettings` (ambient occlusion, bloom, TAA), `temporalJitter` |
| `illusion/render/Components.hpp` | `MeshRenderer`, `Camera`, `DirectionalLight`, `PointLight`, `SkyLight`, `MotionHistory`, `spawnModel` |
