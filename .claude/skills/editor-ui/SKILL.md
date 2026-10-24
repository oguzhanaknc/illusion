---
name: editor-ui
description: Write or change editor UI in illusion (editor/): widgets, panels, the gallery, theme tokens, fonts, icons, stable ids, UiDriver tests and golden images. Use whenever code under editor/ draws UI or a theme, locale or font file changes.
---

# Editor UI

Read `docs/content/en/architecture/editor.md` first. Decisions: ADR 0030 (tree, boundary, command-first)
and ADR 0031 (UI stack).

## 1. Where code goes

- Reusable widgets: `editor/ui/include/illusion/editor/ui/Widgets.hpp` + `src/Widgets.cpp`. Every new
  widget also appears in the gallery (`editor/tools/ui_gallery/Gallery.cpp`).
- Text: add keys to `editor/data/locales/en.json` (sorted, `comment` when the context is unclear); they
  become `illusion::editor::strings::k...` constants (`illusion/editor/EditorStrings.hpp`). See the
  `localization` skill.
- State changes: panels never change editor state themselves; they run commands (`editor-commands` skill).
- The editor window: `editor/shell` (`illusion::editor_shell`). Panels are classes in `src/` drawn by
  `EditorShell::draw()` with the shell's state (`EditorShell::Impl` in `ShellImpl.hpp`); they run commands with
  `Impl::run()` (failures become toasts) or `Impl::runAction()`. A new panel gets an entry in `Panel` and
  `kPanels` (`Layouts.cpp`), a dock slot in each built-in layout that shows it, a `window.*` UI action, and a
  title key. Menu items and toolbar buttons are `UiAction`s in `editor/core/src/UiActions.cpp`: give each an
  id, its command and params, a menu and group, and a default chord only when the command has none; actions of
  commands that do not exist yet name their milestone. Never put state changes in a panel that bypass
  commands, except the agents' approvals, pause and disconnect (deliberately not commands, ADR 0037).
- New targets: `illusion_add_editor_library()` / `illusion_add_editor_executable()` only. Never include
  `illusion/editor/...`, `imgui.h` or `implot.h` outside `editor/` (`python tools/check_layering.py`).

## 2. Rules for UI code

1. **Colours** come from theme tokens: `ui::tokenColor(kit, ThemeToken::...)` for draw lists,
   `kit.theme.color(...)` for `ImVec4`. No literal colours except sample content (a viewport mock-up).
2. **Sizes** come from the style (`GetFrameHeight()`, `style.FramePadding`, ...) or are design values
   multiplied by `kit.scale`. Never use pixel sizes that ignore the scale.
3. **Fonts**: `FontScope scope(fonts, FontRole::Strong)` or `fonts.push(role)` + `ImGui::PopFont()`. Never
   `PushFont(nullptr, GetFontSize())` (scales twice).
4. **Ids**: every interactive item gets a stable id through `FrameText`:
   `text.label(strings::kX, "panel.item")`, `text.iconOnly(icon, "panel.item")`, `text.verbatim(name, id)`
   for data, `text.hidden(id)` for unlabeled fields. Ids are lowercase, dot-separated, unique, never
   translated. Composite widgets add suffixes (`.x`, `.clear`, `.menu`).
5. **No allocation in idle frames**: no `std::string`/`std::vector` built per frame; use `FrameText`
   (reset once per frame) and fixed-size buffers. The gallery allocation test must keep passing.
6. **Icons**: `icons::kName` from `Icons.hpp`; names known only at run time go through `findIcon()`.
7. **Accessibility**: never use colour as the only cue (axis chips carry letters); keep text contrast; hit
   targets at least 20 px at 100% scale.

## 3. Themes

- Theme files: `editor/data/themes/*.json`. Every theme defines all `kThemeTokenCount` tokens.
- A new token: add it to `ThemeToken`, `kTokenNames` (`Theme.cpp`), every theme file, the token table in
  the architecture page (EN and TR), and a contrast check in `editor/tests/TestTheme.cpp` if text or
  graphics are drawn with it.
- Metrics change only in `Theme::apply()`; the style is always rebuilt, never scaled in place.

## 4. Testing

- CPU tests (`editor/tests`): use `test::GalleryFixture` (headless renderer, fonts, theme) and
  `ui::UiDriver` to click, drag and type by stable id:

```cpp
test::GalleryFixture fixture;
ui::UiDriver driver(fixture.context, [&] { fixture.frame(); });
driver.runFrames(3);
CHECK(driver.click("gallery.save"));
```

- Platform input: `TestUiPlatform.cpp` shows how to feed window events through a headless window.
- The shell: `test::ShellFixture` (`editor/tests/shell/ShellTestSupport.hpp`) runs the whole window on a fixture
  project at 1280x720 with its own user directory; drive it with `UiDriver` and check results through commands
  (`ui.state`, `preferences.get`). Docked panels behind another tab are not drawn: show them with
  `ui.show_panel` first, and filter long lists (the keyboard table) before clicking rows. The idle-frame
  allocation test runs 120 warm-up frames.
- GPU tests (`editor/tests/gpu`): gallery and shell goldens in `editor/tests/data/golden` (helpers in
  `GoldenSupport.hpp`). After an intended visual
  change: run `illusion_editor_gpu_tests` with `ILLUSION_UPDATE_GOLDENS=1`, open the changed PNGs, check
  them, commit them. Captures of every run land in `<build>/ui-captures/`.
- Look at your change: `illusion_ui_gallery --headless --page widgets --theme dark --screenshot out.png`,
  also with `--theme high-contrast`, `--scale 1.5` and `--locale en-XA` (long, accented text).

## 5. Before committing

- Debug and Release build with MSVC and clang-cl (warnings are errors), all editor tests pass, the
  `ui/gallery-frame` benchmark is within budget.
- `python tools/check_layering.py`, `python tools/check_format.py`, docs parity and Doxygen pass.
