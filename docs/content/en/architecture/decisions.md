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
| [0025](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0025-physics-extensions.md) | Mesh and hull geometry, characters, joints and shape queries |
| [0026](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0026-audio-effects-and-background-loading.md) | Audio effects, sound cones and clips loaded through the asset streamer |
| [0027](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0027-module-system-and-hot-reload.md) | Game modules: a C entry point, service injection and hot reload that hands Component types over |
| [0028](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0028-json-api-and-expected.md) | A non-throwing public JSON API over private yyjson, and Expected for recoverable errors |
| [0029](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0029-localization.md) | Localization with stable keys, compiled English, data-file locales and a MessageFormat subset |
| [0030](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0030-editor-tree-and-export-boundary.md) | A separate editor tree, a command-first editor and an enforced export boundary |
| [0031](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0031-editor-ui-stack.md) | The editor UI stack: Dear ImGui docking with our backends, a gamma-space UI target, theme tokens, fonts and icons |
| [0032](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0032-reflection-v2.md) | Reflection v2: metadata, struct reflection, a JSON value API and migrations |
| [0033](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0033-entity-ids-names-scene-format-v2.md) | Persistent entity ids, names and scene format v2 |
| [0034](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0034-transform-hierarchy.md) | Transform hierarchy with world-space truth, Disabled and Inactive |
| [0035](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0035-asset-references.md) | Asset references by virtual path, and authoring/runtime render Components |
| [0036](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0036-editor-command-core.md) | Typed editor command core, transactions, operation-log undo, crash journal and project layout |
| [0037](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0037-editor-rpc-protocol.md) | Editor RPC protocol: JSON-RPC 2.0 over NDJSON on stdio and AF_UNIX, tokens and permission modes |
| [0038](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0038-agent-acceptance-transcripts.md) | Agent acceptance tests from replayable transcripts |
| [0039](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0039-mcp-bridge.md) | A dual-era MCP bridge with tools generated one to one from editor commands |
| [0040](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0040-renderer-extension-points.md) | Expose renderer extension points for editors and tools |
| [0041](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0041-module-api-v3.md) | Module API v3: start and stop per World, a multi-world host and host service interfaces |
| [0042](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0042-play-mode-and-game-session.md) | Play mode through scene snapshots and a shared GameSession runtime |
| [0043](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0043-project-modules-built-by-the-editor.md) | Project modules built by the editor through a module SDK package |
| [0044](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0044-component-layout-migration-on-hot-reload.md) | In-place Component layout migration on hot reload |

To propose a new decision, copy `docs/adr/0000-template.md` and follow the
[contributing guide](../contributing.md#architecture-decision-records).
