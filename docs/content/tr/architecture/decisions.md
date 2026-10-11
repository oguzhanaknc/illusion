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

Yeni bir karar önermek için `docs/adr/0000-template.md` dosyasını kopyalayın ve
[katkıda bulunma rehberini](../contributing.md#architecture-decision-recordlar) izleyin.
