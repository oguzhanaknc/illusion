# 0035. Asset references by virtual path, and the authoring/runtime split of render Components

- Status: Accepted
- Date: 2026-10-22

## Context

ADR 0022 left `MeshRenderer` and `SkyLight` out of scene files because they held GPU handles: a scene could
place cameras and lights but not what they look at. The editor (M7i onward) must save, inspect and edit
every Component, including the meshes, materials, environments, collision meshes and audio clips they use,
and an AI agent must be able to name an asset in a command. Assets are identified by the hash of their
virtual path (ADR 0019); that decision anticipated GUID sidecar files "when the editor needs rename
tracking". Shared game modules have their own copy of the engine's statics (ADR 0027), so anything
process-wide must be handed over through `ModuleServices`.

## Decision

- **`AssetRef{AssetId id}`** is the reflected field type `FieldType::Asset` (with `FieldMeta::assetKind` as
  an editor hint). `AssetId` moves to core (`illusion/core/AssetId.hpp`) together with the virtual-path
  syntax (`VirtualPath.hpp`); the assets library includes it from `AssetFormat.hpp` for existing users.
- **Paths, not GUIDs.** The id stays the FNV-1a hash of the normalized path (ADR 0019 is kept). A
  process-wide, thread-safe, append-only `AssetPathTable` maps ids back to paths; shared modules use the
  host's table (`ModuleServices::assetPaths`, `kModuleApiVersion` 2). JSON writes an AssetRef as its path
  (`""` for none) and also reads `null` and `{"path": "..."}`, the form that can later gain a `uid` member.
- **Sub-assets and built-ins.** A fragment selects part of a file: `#mesh/N`, `#material/N`,
  `#node/<name>`. The reserved mount `builtin:` (VirtualFileSystem refuses to mount it) provides
  `mesh/{cube,sphere,plane,capsule,cylinder}`, `material/default` and `sky/procedural`, so scenes and tests
  need no files for simple content.
- **Authoring and runtime Components.** The GPU-free header `SceneComponents.hpp` holds the saved
  Components: `MeshRenderer{mesh, submesh, material, layers, castShadows, visible}`, `SkyLight{environment,
  intensity, background}`, `ProceduralSky`, `Camera` and the lights. The renderer draws only the runtime
  Components `RenderMesh` (GPU handles, flags and the source key) and `RenderSky`; code that builds geometry
  writes `RenderMesh` directly with a source key of 0.
- **Resolvers.** One `RenderAssetResolver` per World compares every `MeshRenderer` with the key its
  `RenderMesh` was resolved from (in parallel; no allocation when nothing changed) and re-resolves only the
  differences through a `RenderAssetCache` shared by all Worlds of a `RenderResources`. The cache loads
  through `AssetStreamer`, uploads streamed models through `UploadQueue` and counts the resolvers using each
  asset. Assets still loading keep the previous result; a failed mesh draws nothing and a failed material
  falls back to the default material. `PhysicsAssetResolver` cooks `Collider::mesh` into PhysicsWorld
  geometry (cached by asset, mesh index and shape kind); `AudioSource::clip` is an AssetRef with unchanged
  JSON.
- **Materials as files.** `.material` (JSON `illusion.material` version 1) holds `MaterialData` by field
  name plus texture AssetRefs; it streams through `AssetStreamer::loadFile`, and `cookAssets()` cooks its
  textures in the colour space the renderer uses.
- **Missing assets** never fail a scene load: the reference is kept (and saved again), and
  `SceneLoadResult::missingAssets` lists the ones `SceneLoadOptions::assetExists` (or the built-in list)
  does not know.

## Why not GUID sidecars

Sidecar files with GUIDs keep references alive when files move, but they double the number of files,
conflict in merges, get lost when files are copied outside the editor, and need a project-wide index before
any asset can be found. With every command going through the editor (M7i), moving an asset can rewrite the
paths that refer to it (`asset.move` with a reference index), which gives the same result with readable
files. Revisit when projects routinely move assets outside the editor, or when the asset catalog (M7u)
shows that path rewrites are too slow; the `{"path", "uid"}` JSON form leaves room for that.

## Consequences

- Closes ADR 0022's consequence: `MeshRenderer`, `SkyLight` and collision meshes are saved in scenes.
- Renaming or moving a file outside the editor breaks its references (reported as missing assets).
- `RenderResources` cannot free GPU memory yet, so cache entries that no resolver uses stay resident; eviction
  comes with the asset catalog and live reload (M7u).
- Systems that need GPU or physics data from a saved Component go through a resolver; the resolver costs
  under a nanosecond per renderer in steady state (`render/asset-resolve-steady`).
