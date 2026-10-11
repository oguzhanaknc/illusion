# 0010. Shaders in Slang, compiled to SPIR-V at build time and embedded

- Status: Accepted
- Date: 2026-10-11

## Context

Shaders need one source language for Vulkan today and possibly Metal or Direct3D 12 later, reproducible
compilation on all three host platforms, errors at build time rather than at run time, and nothing to
ship or load separately.

Options considered: GLSL with glslang/shaderc (Vulkan-centric, no other targets), HLSL with DXC (no
official macOS binaries; ties to DirectX conventions), Slang (HLSL-compatible, actively developed under
Khronos, targets SPIR-V, DXIL and Metal, prebuilt for Windows, Linux and macOS).

## Decision

- Shaders are written in Slang.
- The pinned Slang release (2026.19) for the build host is downloaded with SHA-256 verification;
  `ILLUSION_SLANGC_EXECUTABLE` can point at another compiler.
- `illusion_add_shaders()` compiles each entry point to SPIR-V with warnings as errors and dependency
  tracking, then embeds the bytecode in the target. A generated header exposes one function per shader
  returning `std::span<const std::byte>`.
- Compiled SPIR-V entry points are named `main`, NVRHI's default.

## Consequences

- No shader files at run time, so no missing or mismatched shaders in shipped builds.
- Each CI job downloads the Slang archive (60–80 MB); add caching if it becomes a bottleneck.
- Hot reload of shaders (editor milestone) will need run-time compilation through the Slang library;
  the build-time path stays the default for exported games.
