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
| `illusion/core/Log.hpp` | `Logger`, `LogSink`, `ConsoleSink` ve `ConsoleStreams`, `MemorySink`, `LogRingSink` ve `LogRecord`, `ILL_LOG_*` makroları |
| `illusion/core/Memory.hpp` | `LinearAllocator`, `PoolAllocator`, hizalanmış bellek ayırma |
| `illusion/core/Expected.hpp` | `Expected<T, E>`, `Unexpected`, `unexpected()`: kurtarılabilir hatalar için değer veya hata |
| `illusion/core/Json.hpp` | `JsonDocument`, `JsonValue`, `JsonMutDocument`, `JsonMutValue`, `JsonError`: katı, exception fırlatmayan JSON okuma ve kanonik biçimde yazma |
| `illusion/core/Random.hpp` | `Random` (deterministik xoshiro256**), `secureRandomBytes` |
| `illusion/core/JobSystem.hpp` | `JobSystem`, `JobCounter`, `Job` |
| `illusion/core/InplaceFunction.hpp` | Bellek ayırmayan, tipi silinmiş (type-erased) çağrılabilir nesne |
| `illusion/core/Thread.hpp` | Thread isimleri, thread indeksleri, spin-wait ipucu |
| `illusion/core/AssetId.hpp` | `AssetId`, parçalı Asset yolları, `builtinAssetPaths` |
| `illusion/core/AssetRef.hpp` | `AssetRef`, `AssetPathTable`, `AssetKind` |
| `illusion/core/VirtualPath.hpp` | Sanal yol denetimleri ve `normalizeVirtualPath` |
| `illusion/core/Hash.hpp` | Kararlı FNV-1a ve xxHash64 hash'leri |
| `illusion/core/Serialization.hpp` | `BinaryWriter`, `BinaryReader` (sınır denetimli, little-endian) |
| `illusion/core/TypeName.hpp` | Derleme zamanı tip isimleri |
| `illusion/core/Profiler.hpp` | Tracy profiling makroları |
| `illusion/core/EventBus.hpp` | `EventBus`, `Subscription` |
| `illusion/core/Time.hpp` | `monotonicSeconds`, `Stopwatch`, `FrameClock`, `FixedTimestep` |
| `illusion/core/Version.hpp` | `SemanticVersion`, `engineVersion` |
| `illusion/core/Module.hpp` | `ModuleApi`, `ModuleContext`, `ModuleDependency`, `makeModuleApi`, `ILL_MODULE`: oyun modüllerinin modül tarafı |
| `illusion/modules/ModuleHost.hpp` | `ModuleHost`: modülleri yükler, denetler, çalıştırır ve hot-reload eder; `ModuleInfo` |
| `illusion/core/Math.hpp` | glm tabanlı vektör, matris ve quaternion tipleri, `Aabb`, `Sphere`, `Obb`, `Plane`, `Frustum`, `Ray` ve kesişimler, `screenToRay`, `perspectiveReversedZ`, `orthographicReversedZ`, Euler dönüşümleri |
| `illusion/ecs/World.hpp` | `World`, `Query`, `ChunkView`, `RawChunk` (`forEachMatching`) |
| `illusion/ecs/CommandBuffer.hpp` | Ertelenmiş yapısal değişiklikler |
| `illusion/ecs/Component.hpp` | Component kaydı ve `ComponentRegistry` |
| `illusion/ecs/Entity.hpp` | `Entity` handle'ı |
| `illusion/ecs/Reflection.hpp` | `reflect`, `ComponentReflection`, `FieldInfo`, `EnumReflection` |
| `illusion/ecs/SceneSerializer.hpp` | `saveScene`, `loadScene` (JSON sahne dosyaları), `UnknownComponents`, sahne ayarları |
| `illusion/ecs/EntityGuid.hpp` | `EntityGuid` Component'i, `GuidGenerator`, `assignMissingGuids`, `GuidEntityResolver` |
| `illusion/ecs/Name.hpp` | `Name` Component'i, `isValidEntityName` |
| `illusion/ecs/Hierarchy.hpp` | `Hierarchy`, `LocalTransform`, `Disabled`, `Inactive`, `WorldSpaceAuthority`; `setParent`, `reorder`, `destroyRecursive`, `setEnabled`, `HierarchyIndex`, `updateTransformHierarchy` |
| `illusion/ecs/Transform.hpp` | `Transform` Component'i (konum, dönüş, ölçek), `combine`, `relativeTo` |
| `illusion/ecs/Archetype.hpp` | Archetype depolaması (motor içi) |
| `illusion/platform/Platform.hpp` | `Platform`: pencere sisteminin yaşam döngüsü, event pompalama, `wake()`, `monitors()`, `keyDisplayName()` |
| `illusion/platform/Window.hpp` | `Window`, `WindowDesc`, `CursorMode`, `CursorShape`, `WindowIcon`: pencere durumu, pano, bırakılan dosyalar |
| `illusion/platform/Input.hpp` | `InputState`, `Key`, `MouseButton` |
| `illusion/platform/Events.hpp` | Pencere ve input event'leri |
| `illusion/platform/Archive.hpp` | `ArchiveReader`, `writeArchive`, `packDirectory` |
| `illusion/platform/FileSystem.hpp` | `VirtualFileSystem`, platform dizinleri |
| `illusion/input/Gamepad.hpp` | `Gamepad`, `GamepadState`, `TriggerEffect`, gamepad event'leri |
| `illusion/input/GamepadSystem.hpp` | `GamepadSystem`, `VirtualGamepad` |
| `illusion/input/InputActions.hpp` | `InputActions`, `InputBinding`, `BindingCapture`, glyph'ler, `glyphLabelKey`, `keyLabelKey` |
| `illusion/input/InputStrings.hpp` | Üretilir: "input" string tablosunun `TextKey` sabitleri |
| `illusion/text/Localization.hpp` | `Localization`: string tabloları, çeviriler, geri dönüş zinciri, `pseudoLocale()` |
| `illusion/text/StringTable.hpp` | `StringTableData`, `TextKey`: `illusion_textc` ile üretilen derlenmiş string tabloları |
| `illusion/text/LocaleFile.hpp` | `LocaleFile`, `LocaleEntry`, `LocaleError`: locale dosya biçimi |
| `illusion/text/MessageFormat.hpp` | `MessageFormat`, `MessageArgument`, `MessageContext`: ICU MessageFormat alt kümesi |
| `illusion/text/PluralRules.hpp` | `PluralRule`, `PluralCategory`, `PluralOperands`: CLDR kardinal çoğul kuralları |
| `illusion/text/NumberFormat.hpp` | `NumberFormat`, `formatNumber`: yerel ayara göre sayı ayırıcıları |
| `illusion/text/LocaleId.hpp` | `LocaleId`: BCP 47 dil etiketleri |
| `illusion/text/TextOutput.hpp` | `TextOutput`: biçimlendirilmiş metni bir string'e veya arenaya yazar |
| `illusion/text/SearchFold.hpp` | `searchFold`: yerel ayardan bağımsız büyük/küçük harf ve aksan katlama |
| `illusion/text/FuzzyMatcher.hpp` | `FuzzyMatcher`: puanlı alt dizi eşleştirme |
| `illusion/text/Utf8.hpp` | UTF-8 doğrulama, çözme, kodlama ve `Utf8View` |
| `illusion/assets/Image.hpp` | `Image`, çözme, PNG kodlama, mip üretimi |
| `illusion/assets/Mesh.hpp` | `MeshData`, `Vertex`, prosedürel Mesh'ler |
| `illusion/assets/Model.hpp` | `ModelData`, `MaterialData`, `loadGltf` |
| `illusion/assets/AssetFormat.hpp` | `AssetId`, `AssetFile`, hazırlanmış görüntü ve model formatları |
| `illusion/assets/AssetDatabase.hpp` | `AssetDatabase`: görüntüler ve modeller için içe aktarma önbelleği |
| `illusion/assets/AssetStreamer.hpp` | `AssetStreamer`, `AssetHandle`: görüntülerin, modellerin ve diğer dosyaların (`loadFile`) asenkron yüklenmesi |
| `illusion/assets/AssetCooker.hpp` | `cookAssets`: bir dizini önceden içe aktarma |
| `illusion/assets/MaterialAsset.hpp` | `MaterialAsset`: `.material` dosyaları, `parseMaterialAsset`, `writeMaterialAsset` |
| `illusion/assets/Environment.hpp` | Equirectangular eşleme, `ShIrradiance`, `makeSkyImage` |
| `illusion/physics/PhysicsWorld.hpp` | `PhysicsWorld`: simülasyon, cisim ve karakter kontrolü, Mesh ve hull geometrisi, joint'ler, raycast'ler, shape cast'ler, örtüşmeler, çarpma hızlarıyla temas olayları |
| `illusion/physics/PhysicsComponents.hpp` | `RigidBody`, `Collider`, `CharacterController`, `Joint` Component'leri ve çalışma zamanı durumları (`PhysicsBody`, `CharacterBody`, `PhysicsJoint`); `PhysicsMeshHandle` |
| `illusion/physics/PhysicsAssetResolver.hpp` | `PhysicsAssetResolver`: Collider'ların Mesh Asset'lerini hazırlar |
| `illusion/audio/AudioWorld.hpp` | `AudioWorld`: clip'ler (arka planda yükleme dahil), çalma, tek seferlik sesler, mikser bus'ları, bus filtreleri, reverb, çıkış; `AudioWorldDesc`, `AudioStats` |
| `illusion/audio/AudioComponents.hpp` | `AudioSource` (filtreler, reverb gönderimi ve konilerle), `AudioListener`, `AudioPlayback` Component'leri; `AudioBus`, `AudioAttenuation` |
| `illusion/render/RenderDevice.hpp` | `RenderDevice`, `GpuInfo` (bindless ve multi-draw desteği dahil): NVRHI üzerinden Vulkan cihazı |
| `illusion/render/Swapchain.hpp` | `Swapchain`: bir pencereye present etme |
| `illusion/render/DebugTriangle.hpp` | Tanılama amaçlı üçgen renderer'ı |
| `illusion/render/RenderResources.hpp` | `RenderResources`: GPU Mesh'leri, Texture'ları, Material'leri, ortamlar ve Handle'ları |
| `illusion/render/SceneRenderer.hpp` | `SceneRenderer`, `RenderView`, `RenderStats` |
| `illusion/render/Shadows.hpp` | `ShadowSettings`, `ShadowCascades`, `computeShadowCascades` |
| `illusion/render/UploadQueue.hpp` | `UploadQueue`: streaming ile gelen modellerin byte bütçeli GPU yüklemesi |
| `illusion/render/PostProcess.hpp` | `PostProcessSettings` (ambient occlusion, bloom, TAA), `temporalJitter` |
| `illusion/render/SceneComponents.hpp` | Kaydedilen render Component'leri: `MeshRenderer`, `Camera`, `DirectionalLight`, `PointLight`, `SkyLight`, `ProceduralSky` |
| `illusion/render/Components.hpp` | Çalışma zamanı `RenderMesh`, `RenderSky`, `MotionHistory`; `spawnModel` |
| `illusion/render/RenderAssets.hpp` | `RenderAssetCache`, `RenderAssetResolver`: Asset referanslarından GPU kaynaklarına |
