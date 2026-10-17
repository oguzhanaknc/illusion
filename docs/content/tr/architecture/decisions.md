# Kararlar

Önemli tasarım kararları,
[`docs/adr`](https://github.com/oguzhanaknc/illusion/tree/main/docs/adr) altında Architecture Decision
Record (ADR) olarak kaydedilir. Her kayıt bağlamı, kararı ve sonuçlarını anlatır. ADR'ler yalnızca
İngilizce yazılır; kullanıcı dokümantasyonu değil, mühendislik kayıtlarıdır.

| ADR | Başlık |
| --- | --- |
| [0001](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0001-record-architecture-decisions.md) | Mimari kararların kaydedilmesi |
| [0002](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0002-dependency-management.md) | FetchContent ile sabitlenmiş bağımlılıklar |
| [0003](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0003-type-identity-across-modules.md) | Modüller arası tip kimliği ve servisler |
| [0004](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0004-archetype-ecs.md) | Chunk'lı SoA depolamalı, Archetype tabanlı ECS |
| [0005](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0005-job-system.md) | Sınırlı, lock-free kuyruklu Job System |
| [0006](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0006-documentation-languages.md) | İki dilli dokümantasyon |
| [0007](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0007-event-bus.md) | Anında ve kuyruklu iletimli, tek thread'li event bus |
| [0008](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0008-virtual-file-system.md) | Mount noktalı sanal dosya sistemi |
| [0009](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0009-renderer-foundation.md) | Renderer temeli: Vulkan 1.3 üzerinde NVRHI, çalışma zamanı loader'ı, y-yukarı clip space |
| [0010](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0010-shader-pipeline.md) | Build sırasında SPIR-V'ye derlenen Slang shader'ları |
| [0011](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0011-gamepads-sdl3.md) | Test'ler için sanal cihazlarla, SDL3 üzerinden gamepad'ler |
| [0012](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0012-input-actions.md) | Metin tabanlı binding formatı ve kararlı glyph kimlikleriyle action eşleme |
| [0013](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0013-forward-pbr-renderer.md) | Reversed-Z, CPU culling ve instancing ile forward PBR renderer |
| [0014](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0014-image-based-lighting.md) | Görüntü tabanlı aydınlatma: GPU'da hazırlanan cube map'ler, SH irradiance, çoklu saçılımlı split sum |
| [0015](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0015-cascaded-soft-shadows.md) | Tek bir yönlü ışık için PCSS'li cascaded shadow map'ler |
| [0016](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0016-hdr-post-processing.md) | Depth prepass, GTAO, TAA, bloom ve tonemapping içeren HDR hattı |
| [0017](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0017-gpu-occlusion-culling-and-motion-vectors.md) | İki aşamalı GPU occlusion culling, indirect draw'lar ve nesne başına hareket vektörleri |
| [0018](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0018-bindless-multi-draw.md) | Bindless Material'ler, geometri havuzları ve multi-draw indirect |
| [0019](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0019-asset-ids-cooked-formats-import-cache.md) | Asset kimlikleri, hazırlanmış Asset dosyaları ve içerik hash'li içe aktarma önbelleği |
| [0020](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0020-asset-streaming.md) | Asset streaming: bir I/O thread'i, Job System ve ana thread'de yayımlama |
| [0021](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0021-packed-archives.md) | Paketlenmiş arşivler, arşiv mount'ları ve hazırlanmış Asset dağıtımı |
| [0022](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0022-reflection-and-scene-files.md) | Kararlı adlarla Component reflection ve JSON sahne dosyaları |
| [0023](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0023-jolt-physics-integration.md) | Job System üzerinde Jolt Physics, ECS ile yönetilen cisimler ve sabit adım |
| [0024](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0024-audio-with-miniaudio.md) | miniaudio ile ses; durumunun sahibi olan bir ses thread'inde miksaj |
| [0025](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0025-physics-extensions.md) | Mesh ve hull geometrisi, karakterler, joint'ler ve şekil sorguları |

Yeni bir karar önermek için `docs/adr/0000-template.md` dosyasını kopyalayın ve
[katkıda bulunma rehberini](../contributing.md#architecture-decision-recordlar) izleyin.
