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
| [0017](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0017-gpu-occlusion-culling-and-motion-vectors.md) | Two-phase GPU occlusion culling, indirect draws and per-object motion vectors |
| [0018](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0018-bindless-multi-draw.md) | Bindless materials, geometry pools and multi-draw indirect |
| [0019](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0019-asset-ids-cooked-formats-import-cache.md) | Asset ids, cooked asset files and a content-hashed import cache |
| [0020](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0020-asset-streaming.md) | Asset streaming: an I/O thread, the Job System and main-thread publication |
| [0021](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0021-packed-archives.md) | Packed archives, archive mounts and cooked shipping |
| [0022](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0022-reflection-and-scene-files.md) | Component reflection with stable names and JSON scene files |
| [0023](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0023-jolt-physics-integration.md) | Jolt Physics on the Job System, ECS-driven bodies and a fixed step |
| [0024](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0024-audio-with-miniaudio.md) | Audio with miniaudio, mixed on an audio thread that owns its state |

To propose a new decision, copy `docs/adr/0000-template.md` and follow the
[contributing guide](../contributing.md#architecture-decision-records).
