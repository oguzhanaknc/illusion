# Mimariye Genel Bakış

Bu sayfa illusion'ın nasıl yapılandırıldığını ve her modülün izlediği ilkeleri anlatır.

## Katmanlar

illusion katmanlar halinde inşa edilir. Bir katman yalnızca altındaki katmanlara bağımlı olabilir.

| Katman | İçerik | Durum |
| --- | --- | --- |
| Core | Platform tespiti, assertion'lar, logging, bellek, Job System, ECS, event bus, zaman, matematik | Tamamlandı |
| Platform | Pencere, klavye ve fare (GLFW), sanal dosya sistemi | Tamamlandı |
| Input | SDL3 üzerinden gamepad'ler (titreşim, LED, hareket, touchpad, adaptive trigger'lar), action eşleme, yeniden atama, glyph'ler | Tamamlandı |
| Runtime servisleri | Asset'ler ve streaming, ses (miniaudio), fizik (Jolt) | Planlandı |
| Renderer: temel | NVRHI üzerinden Vulkan 1.3 cihazı, swapchain'ler, Slang shader hattı | Tamamlandı |
| Renderer: özellikler | Mesh'ler, PBR, IBL, gölgeler, GTAO, post-processing, culling | Planlandı |
| Editör | Sahne düzenleme, gizmo'lar, AI komut API'si | Planlandı |
| Oyun modülü | Oyunun kendi kodu; editörde hot-reload edilebilen paylaşımlı kütüphane | Planlandı |

Core bir statik kütüphanedir. Editör ve export edilen oyunlar onu link eder; bir oyun modülü motor
durumunun ikinci bir kopyasını asla barındırmaz (aşağıdaki "Süreç genelindeki servisler" bölümüne bakın).

## İlkeler

**Veri odaklı tasarım.** Veri, işlenme biçimine göre yerleştirilir. ECS, Component'leri
structure-of-arrays Chunk'larda saklar; böylece System'ler belleği sıralı okur.

**Kare başına heap ayırma yok.** Kare başına veriler arena ve pool'larda yaşar. Job'lar, log mesajları,
ECS Query'leri ve command buffer'lar kararlı durumda bellek ayırmaz. Kritik yolları benchmark'lar korur.

**Açık thread modeli.** İşler Job System üzerinde Job olarak çalışır. Yapısal ECS değişiklikleri tanımlı
noktalarda tek bir thread üzerinde gerçekleşir; paralel kod yalnızca kendisine verilen Component'lere
dokunur.

**Programcı hatalarında yüksek sesle başarısız ol.** Değişmezler `ILL_ASSERT` (Debug ve CI) ve
`ILL_CHECK` (her zaman) ile kontrol edilir. Motor kodu akış kontrolü için exception kullanmaz; bir Job'dan
veya bir Component constructor'ından kaçan exception süreci sonlandırır.

**Her karar kayıt altına alınır.** Önemli tasarım kararları Architecture Decision Record (ADR) olarak
yazılır; bkz. [Kararlar](decisions.md).

## Süreç genelindeki servisler

Birkaç servis süreç geneline aittir: Logger, Component registry ve assert handler. Her birine
değiştirilebilir bir instance pointer'ı üzerinden erişilir (`Logger::setInstance()`,
`ComponentRegistry::setInstance()`, `setAssertHandler()`).

Bu, hot reload için önemlidir. Editörde oyun modülü, statik core kütüphanesinin kendi kopyasını link eden
bir paylaşımlı kütüphanedir. Modül yüklendiğinde editör kendi servis örneklerini modüle enjekte eder;
böylece iki taraf da tek bir Logger ve tek bir Component registry paylaşır. Component tipleri tam
nitelikli adlarının hash'i ile tanımlanır; bu yüzden bir Component her iki tarafta da aynı id'yi alır.

## Depo yapısı

| Yol | İçerik |
| --- | --- |
| `engine/core` | Core kütüphanesi: `include/illusion/{core,ecs}` ve `src/` |
| `engine/platform` | Platform kütüphanesi: `include/illusion/platform` ve `src/` |
| `engine/input` | Input kütüphanesi: `include/illusion/input` ve `src/` |
| `engine/render` | Render kütüphanesi: `include/illusion/render`, `src/` ve `shaders/` |
| `samples` | Motor özelliklerini çalıştıran küçük uygulamalar |
| `tests` | Unit test'ler (doctest), her özellik için bir dosya |
| `benchmarks` | Performans bütçeli benchmark'lar (nanobench) |
| `cmake` | Build seçenekleri, derleyici ayarları, sabitlenmiş bağımlılıklar |
| `docs/content/{en,tr}` | Bu dokümantasyon, İngilizce ve Türkçe |
| `docs/adr` | Architecture Decision Record'lar |
| `tools` | Format, dokümantasyon ve platform yardımcı betikleri |
| `.claude/skills` | AI agent'lar ve insanlar için adım adım rehberler |
