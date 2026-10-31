# 0043. Project modules built by the editor through a module SDK package

- Status: Accepted
- Date: 2026-10-31

## Context

A project's game code is a set of modules (ADR 0027, ADR 0041) whose sources live in the project's
`modules/` directory. Until now nothing built them: `module.load` took the path of a library that someone had
compiled by hand, inside the engine's own CMake tree. The brief asks for an editor that an agent drives
without a human, up to a Tetris game, so the editor must create a module, compile it, report compiler errors
in a form an agent can act on, and load the result into the play session; a person wants the same with one
shortcut and a click on an error that opens the line in their code editor.

A module must be built against the same engine headers, the same `illusion::core` and the same compiler and
configuration as the editor that loads it: the host rejects any other build key (ADR 0027).

Options considered for giving a project build access to the engine:

- **`add_subdirectory()` of the engine sources** in the project's build. Always consistent, but every project
  configures and compiles the engine and fetches its dependencies again: minutes before the first build.
- **A generated build inside the engine's own tree.** Fast, but projects then live inside the engine's build
  and depend on its layout.
- **A CMake package of the engine that a project finds with `find_package(illusion)`.** The standard way for
  a CMake library to be used by another build. The editor's build tree exports the targets a module needs and
  a description of its toolchain; M8 installs the same package with the editor.

Options considered for running the build:

- **Compile in the editor process** (a library compiler). Not available for C++ at the quality of the
  platform compilers, and a compiler crash would take the editor down.
- **Run CMake as a child process** with the editor's toolchain, read its output and parse the diagnostics.

## Decision

- **Module SDK.** Every editor build tree writes a CMake package to `<build>/sdk`: `illusionConfig.cmake`
  imports `illusion::core` (with `illusion::glm` and `illusion::yyjson`) and the header-only API targets
  (`illusion::physics_api`, `audio_api`, `input_api`, `render_api`, `runtime_api`), and defines
  `illusion_add_module()` and `illusion_configure_target()`, the functions engine modules use. Profiling
  builds write no SDK, since they build modules statically (ADR 0041).
- **Toolchain description.** Next to it, `<build>/sdk/<Config>/illusion-sdk.json` names the CMake program,
  the generator, the configuration and the cache arguments of the editor's build (compiler, flags, make
  program, runtime library), so a project's module is compiled exactly like the editor. The editor reads it
  from `EditorContextOptions::moduleSdk`; the development editor passes the file of its own build tree, and
  M8's installed editor will pass the one installed beside it. Without one, module builds are unsupported.
- **Project layout.** `modules/CMakeLists.txt` finds the package and calls `illusion_project_modules()`,
  which adds every `modules/<name>/` directory with a `CMakeLists.txt`; each module's own file calls
  `illusion_add_module(<name> SHARED ...)`. `module.create` writes these files from a template (undoable
  file edits). The libraries are written to `.illusion/build/modules/bin`,
  one per module, named after the module's target.
- **Builds.** `module.build` is an async command (ADR 0036 addendum). It configures
  `.illusion/build/modules` when it has no cache yet (or when asked), then runs `cmake --build`, reading
  both output streams line by line with `Process`. GCC, Clang, MSVC and CMake diagnostics are parsed into
  structured records (file relative to the project when inside it, line, column, severity, code, message),
  logged to the Console with their source, and returned. A successful build loads each built module that is
  not loaded yet and hot-reloads the others through the `ModuleHost` (shadow copies keep the loaded library
  replaceable); `module.reload` reloads modules whose library changed, built elsewhere.
- **External tools.** `editor.open_source` opens a project file at a line in the code editor of the
  preferences (`codeEditor`, a command line with `{file}`, `{line}` and `{column}` placeholders) or, when it
  is empty, with the system's default application. The Console's source links and the diagnostics of a
  build call it.
- **Permissions.** Building, loading and opening sources are `Build` and `System` class commands, so the
  `ask` permission mode asks a person before an agent compiles code.

## Consequences

- An agent can go from a module's sources to a running game with `module.create`, file edits,
  `module.build` and `play.start`, and gets errors with file and line instead of raw compiler output.
- The first build of a project costs a CMake configure (a few seconds); later builds are incremental.
- The editor must find the compiler: on Windows it is started from a developer environment
  (`tools\windows\vsdev.cmd`) until M8 installs an SDK that locates Visual Studio itself. A configure that
  cannot find the compiler fails with CMake's message as a diagnostic.
- A build-tree SDK contains absolute paths of that build tree; projects are not tied to it, because the path
  is passed at configure time and the build directory lives in the project's git-ignored `.illusion/`.
- The package is the first public CMake interface of the engine: renaming an exported target or
  `illusion_add_module()`'s signature is an incompatible change for projects.
