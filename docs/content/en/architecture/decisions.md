# Decisions

Significant design decisions are recorded as Architecture Decision Records (ADRs) in
[`docs/adr`](https://github.com/oguzhanaknc/illusion/tree/main/docs/adr). Each record describes the
context, the decision and its consequences. ADRs are written in English only; they are engineering
records rather than user documentation.

| ADR | Title |
| --- | --- |
| [0001](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0001-record-architecture-decisions.md) | Record architecture decisions |
| [0002](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0002-dependency-management.md) | Pinned dependencies with FetchContent |
| [0003](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0003-type-identity-across-modules.md) | Type identity and services across modules |
| [0004](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0004-archetype-ecs.md) | Archetype-based ECS with chunked SoA storage |
| [0005](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0005-job-system.md) | Job System with a bounded lock-free queue |
| [0006](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0006-documentation-languages.md) | Bilingual documentation |
| [0007](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0007-event-bus.md) | Single-threaded event bus with immediate and queued delivery |
| [0008](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0008-virtual-file-system.md) | Virtual file system with mount points |
| [0009](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0009-renderer-foundation.md) | Renderer foundation: NVRHI on Vulkan 1.3, run-time loader, y-up clip space |
| [0010](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0010-shader-pipeline.md) | Slang shaders compiled to SPIR-V at build time |
| [0011](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0011-gamepads-sdl3.md) | Gamepads through SDL3, with virtual devices for tests |
| [0012](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0012-input-actions.md) | Action mapping with a text binding format and stable glyph identifiers |
| [0013](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0013-forward-pbr-renderer.md) | Forward PBR renderer with reversed-Z, CPU culling and instancing |
| [0014](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0014-image-based-lighting.md) | Image-based lighting: GPU-baked cube maps, SH irradiance, multiple-scattering split sum |
| [0015](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0015-cascaded-soft-shadows.md) | Cascaded shadow maps with PCSS for one directional light |
| [0016](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0016-hdr-post-processing.md) | HDR pipeline with depth prepass, GTAO, TAA, bloom and tonemapping |

To propose a new decision, copy `docs/adr/0000-template.md` and follow the
[contributing guide](../contributing.md#architecture-decision-records).
