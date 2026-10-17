# Third-party notices

illusion uses the following third-party software. Versions are pinned in `cmake/Dependencies.cmake`.
"Shipped" means the code is linked into games built with illusion; licence texts for shipped components
must be included with distributed games (the export pipeline will collect them automatically).

| Component | Version | Licence | Use | Shipped |
| --- | --- | --- | --- | --- |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | MIT | Unit tests | No |
| [nanobench](https://github.com/martinus/nanobench) | 4.6.0 | MIT | Benchmarks | No |
| [Tracy](https://github.com/wolfpld/tracy) | 0.14.1 | BSD-3-Clause | Profiling (only in `ILLUSION_PROFILING` builds) | Profiling builds only |
| [GLFW](https://github.com/glfw/glfw) | 3.5.1 | Zlib | Window, keyboard, mouse | Yes |
| [SDL](https://github.com/libsdl-org/SDL) | 3.4.18 | Zlib | Gamepads (joystick, haptic, HIDAPI, sensors only) | Yes |
| [glm](https://github.com/g-truc/glm) | 1.0.3 | MIT | Math | Yes |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 5.6.0 | MIT | Physics (planned) | Yes |
| [miniaudio](https://github.com/mackron/miniaudio) | 0.11.25 | Public domain or MIT-0 | Audio (planned) | Yes |
| [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) | commit `6b96fb0` | MIT | Graphics abstraction | Yes |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | 1.4.352 | Apache-2.0 OR MIT | Vulkan API headers | Yes (headers only) |
| [cgltf](https://github.com/jkuhlmann/cgltf) | 1.15 | MIT | glTF 2.0 import | Yes |
| [stb_image](https://github.com/nothings/stb) | commit `2c980bb` (v2.30) | Public domain or MIT | PNG, JPEG and HDR decoding | Yes |
| [Slang](https://github.com/shader-slang/slang) | 2026.19 | Apache-2.0 WITH LLVM-exception | Shader compiler, run at build time only | No |

Development tools that are not linked: CMake, Ninja, clang-format, Doxygen, MkDocs, Material for MkDocs,
mkdocs-static-i18n.

The Steamworks SDK is not distributed with illusion. Users supply it under Valve's Steamworks SDK
Access Agreement.
