# Project Modules

A project's game code is a set of [game modules](../modules.md) whose sources live in `modules/`. The editor
creates them, compiles them with CMake and the same compiler it was built with, shows the compiler's errors,
and loads the result into [Play mode](play-mode.md) without a restart (ADR 0043). An AI agent does the same
through the [Editor commands](../../reference/editor-commands.md).

## Creating a module

`module.create` writes a module from a template:

```json
{"method": "module.create", "params": {"name": "tetris"}}
```

```text
modules/
  CMakeLists.txt          finds the engine's module SDK and adds every module directory
  tetris/
    CMakeLists.txt        illusion_add_module(tetris SHARED SOURCES tetris.cpp USES ...)
    tetris.cpp            ILL_MODULE(tetris, ...) with empty load, start and update functions
```

A module name is 1 to 48 lower case letters, digits and underscores, starting with a letter; it is the
directory, the CMake target and the library name. Add sources to the module's `CMakeLists.txt`. The files are
ordinary project files: `module.create` is undoable, and the source is edited in any code editor or with the
file commands.

## Building

**Modules → Build Modules** (**Ctrl+B**) runs `module.build`:

1. The first build configures CMake in `.illusion/build/modules` (git-ignored) with the editor's module SDK:
   its compiler, flags and configuration, so the module gets the editor's build key. Later builds skip this
   step; `"reconfigure": true` runs it again.
2. `cmake --build` compiles the modules (or only those in `"modules"`), reporting progress.
3. On success every built module that is not loaded yet is loaded, and the loaded ones are hot-reloaded:
   their Components keep their values and a running play session keeps running. `"load": false` only
   builds.

`module.build` is asynchronous: agents receive `rpc.progress` notifications and can cancel it with
`rpc.cancel`. A compile that fails is not a command error; the result says `"succeeded": false` and lists the
diagnostics:

```json
{
  "succeeded": false,
  "configured": false,
  "errors": 1,
  "warnings": 0,
  "diagnostics": [
    {"file": "modules:/tetris/tetris.cpp", "line": 42, "column": 9, "severity": "error",
     "code": "C2065", "message": "'boardd': undeclared identifier"}
  ],
  "libraries": [],
  "loaded": [],
  "reloaded": [],
  "modules": [],
  "output": "..."
}
```

Diagnostics of GCC, Clang, MSVC, clang-cl, the MSVC linker and CMake are recognised. Files inside the project
are given as virtual paths (`modules:/...`); the other files as the tool printed them. Every diagnostic is
also logged to the Console in the `Build` category with its file and line, and `output` holds the last lines of
CMake's output for anything the parser did not recognise.

`module.reload` hot-reloads one module by name, or every module whose library file changed, for libraries built
outside the editor (**Ctrl+R**).

## The module SDK

The engine's build writes a CMake package to `<build>/sdk`: `illusionConfig.cmake` imports `illusion::core` and
the API targets (`illusion::physics_api`, `audio_api`, `input_api`, `render_api`, `runtime_api`) and defines
`illusion_add_module()` and `illusion_project_modules()`. `<build>/sdk/<Config>/illusion-sdk.json` describes the
toolchain, and the editor uses the one of its own build. Profiling builds write no SDK, since they link modules
statically.

The editor finds the compiler through its environment. On Windows, start it from a developer command prompt
(`tools\windows\vsdev.cmd`); otherwise the configure step fails and reports CMake's error as a diagnostic.

!!! warning "Agents can run code"
    `module.build`, `module.reload` and `module.load` compile and load C++ inside the editor process. They are
    `build` class commands: the `ask` permission mode asks a person first.
