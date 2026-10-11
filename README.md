# illusion

[![CI](https://github.com/oguzhanaknc/illusion/actions/workflows/ci.yml/badge.svg)](https://github.com/oguzhanaknc/illusion/actions/workflows/ci.yml)

illusion is a performance-focused 3D game engine for Windows, macOS and Linux, written in C++20 and
designed to ship commercial games on Steam and similar stores.

> **Status: foundation phase.** The core library (logging, memory, Job System, archetype ECS) is
> implemented, tested on all platforms and benchmarked. Rendering, input, audio, physics, the editor and
> the export pipeline are next; see [ROADMAP.md](ROADMAP.md).

## Highlights

- Data-oriented, archetype-based ECS with 16 KiB structure-of-arrays chunks, cached queries, chunk
  iteration, parallel iteration and command buffers.
- Job System with an allocation-free, lock-free bounded queue; waiting threads help execute jobs.
- Platform layer: windows with keyboard/mouse input (GLFW, headless mode for tests), event bus, frame
  timing, virtual file system with atomic writes.
- No per-frame heap allocations: arenas, pools and an inline type-erased callable.
- Tracy profiler integration and benchmark tests with performance budgets in CI.
- CI on Windows (MSVC, clang-cl), Linux (GCC, Clang, ASan/UBSan, TSan) and macOS (Apple Clang).
- Documentation in English and Turkish, kept in sync by CI.

## Quick start

```bash
git clone https://github.com/oguzhanaknc/illusion.git
cd illusion
cmake --preset linux-clang          # or windows-msvc, macos-clang, linux-gcc, ...
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

On Windows, run the commands through `tools\windows\vsdev.cmd` (or from a Visual Studio developer
shell). See [Getting Started](docs/content/en/getting-started.md) for prerequisites and options.

```cpp
#include <illusion/ecs/World.hpp>

struct Position { float x, y, z; };
struct Velocity { float x, y, z; };

illusion::World world;
world.create(Position{0, 0, 0}, Velocity{1, 0, 0});
world.query<Position, const Velocity>().each([](Position& p, const Velocity& v) { p.x += v.x; });
```

## Documentation

- English: [docs/content/en](docs/content/en/index.md) · Türkçe: [docs/content/tr](docs/content/tr/index.md)
- Build the site locally: `pip install -r docs/requirements.txt && mkdocs serve`
- API reference: `doxygen docs/Doxyfile`
- Design decisions: [docs/adr](docs/adr)
- Contributor and agent rules: [AGENTS.md](AGENTS.md)

## Repository layout

| Path | Contents |
| --- | --- |
| `engine/core` | Core library (`illusion::core`) |
| `tests` | Unit tests (doctest) |
| `benchmarks` | Benchmarks with performance budgets (nanobench) |
| `cmake` | Build options, compiler settings, pinned dependencies |
| `docs` | MkDocs site (en/tr), ADRs, Doxygen configuration |
| `tools` | Format, docs parity and platform helper scripts |
| `.claude/skills` | Development guides for AI agents |

## Third-party software

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
