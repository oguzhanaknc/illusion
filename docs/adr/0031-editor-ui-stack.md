# 0031. The editor UI stack: Dear ImGui docking with our backends, a gamma-space UI target, theme tokens, fonts and icons

- Status: Accepted
- Date: 2026-10-20

## Context

The editor needs a UI toolkit that is fast enough to redraw every frame next to the viewports, docks
panels, works with the engine's NVRHI renderer and platform layer, and can be tested and driven without a
human. It must look calm and professional, scale with per-monitor DPI and a user scale, be readable in a
high-contrast theme, show any language the localization system supports, and never allocate in idle
frames.

Options considered:

- **Qt.** Complete and accessible, but LGPL/commercial licensing, a second event loop and windowing
  system next to GLFW, and heavy integration with the Vulkan renderer.
- **A retained-mode toolkit of our own.** Full control, but years of work for text editing, docking,
  tables and input methods.
- **Dear ImGui (docking branch) with ImPlot.** Immediate mode fits the command-first editor (panels read
  state every frame), it is MIT licensed, widely used in engine editors, has docking, tables, multi-select
  and dynamic fonts (1.92+), and exposes test-engine hooks. It has no screen-reader support.

## Decision

- **Dear ImGui 1.93.0 (docking) and ImPlot**, built as editor-only static targets (`illusion::imgui`,
  `illusion::implot`) from pinned archives, with our configuration header
  (`illusion/editor/ui/ImGuiConfig.hpp`): 32-bit characters (icons live above U+FFFF), obsolete functions
  disabled, no file or shell functions, no default font, `IM_ASSERT` mapped to `ILL_ASSERT`, and the test
  engine hooks enabled. No stock backends.
- **Our backends.** `UiPlatformBackend` feeds window events from the EventBus to Dear ImGui and applies
  its requests (cursor shape and visibility, clipboard, IME position). `UiRenderer` draws with NVRHI:
  dynamic font atlas through `ImGuiBackendFlags_RendererHasTextures`, 16-bit indices with vertex offsets,
  grow-only vertex and index buffers per frame in flight, push constants, a scissor per draw command, a
  binding-set cache and tagged 64-bit texture ids for Dear ImGui's textures and textures the editor
  registers (viewports, thumbnails).
- **Colour pipeline.** The UI is drawn in gamma space, as Dear ImGui's colours and blending expect, into
  an `RGBA8_UNORM` UI target. A present pass copies it to the swapchain and decodes sRGB for `_SRGB`
  swapchains, so both kinds of swapchain show identical colours; captures read the UI target directly.
  Viewport images shown in the UI are `RGBA8_UNORM`, encoded by the tonemapper.
- **Theme tokens.** Colours come from 28 named tokens in `editor-data/themes/*.json` (dark, light, high
  contrast); every theme defines every token. `Theme::apply()` rebuilds the whole Dear ImGui style from
  the tokens and the design metrics (4 px grid, rounding, spacing) times the scale, never scaling
  cumulatively. Tests enforce WCAG contrast: text 4.5:1 (7:1 in high contrast), graphics 3:1. Accent
  presets replace only the accent tokens.
- **Fonts.** Roboto (Apache-2.0, v2.138) for text, Roboto Mono for code and numbers, Material Symbols
  Rounded merged into every text font. Fonts are dynamic: glyphs are rasterised at the size in use, so a
  scale or DPI change only sets `FontScaleDpi`. Fonts load from `editor:` through the VFS into memory the
  `FontManager` owns.
- **Icons.** `cmake/IllusionIcons.cmake` generates `Icons.hpp` from the pinned `.codepoints` file: one
  constant per icon (`icons::kAccountTree`), so an unknown icon is a compile error, plus a sorted name
  table for names known only at run time (`findIcon()`).
- **Text.** Labels come from the editor string table and are built per frame in an arena (`FrameText`) as
  `"<text>###<stable id>"`, so ids, saved layouts and tests do not depend on the language.
- **Widget kit.** Shared widgets draw with theme tokens and take stable ids (`Widgets.hpp`). Widgets and
  panels must not allocate in idle frames; a test runs the gallery and counts both global and Dear ImGui
  allocations.
- **Testing.** `UiItemRegistry` records items through the test-engine hooks; `UiDriver` clicks, drags and
  types by stable id. A headless renderer stand-in runs frames without a GPU. GPU tests compare gallery
  captures with golden images (tolerance for driver differences) and CI publishes the captures.

## Consequences

- The editor UI costs about 40 µs of CPU per frame for the densest gallery page (budget 400 µs).
- Screen readers cannot read the UI; keyboard and gamepad navigation, a high-contrast theme, a
  colour-blind axis preset, UI scale and reduced motion are provided instead. This is documented.
- Upgrading Dear ImGui means re-checking our backends against its changelog (texture protocol, draw
  callbacks, renamed flags) and re-running the golden tests.
- Roboto and Roboto Mono must stay on their Apache-2.0 releases; later releases are OFL.
- Golden images change with every intended visual change; they are rewritten with
  `ILLUSION_UPDATE_GOLDENS=1` and reviewed as images in the pull request.
