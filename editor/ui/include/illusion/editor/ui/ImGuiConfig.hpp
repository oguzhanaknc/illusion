#pragma once

/// @file
/// Dear ImGui configuration of the editor, used as IMGUI_USER_CONFIG by illusion::imgui: every file that
/// includes imgui.h through that target sees these definitions.
///
/// - Characters are 32-bit (Material Symbols has icons above the Basic Multilingual Plane).
/// - Obsolete functions, file functions, shell functions and Win32 clipboard and IME helpers are compiled
///   out: settings go through the editor's file system, and the clipboard and IME through
///   illusion::platform (see UiPlatformBackend).
/// - There is no embedded default font; fonts come from editor-data (see FontManager).
/// - Test engine hooks are compiled in and implemented by UiItemRegistry, which UiDriver uses in tests.
/// - Dear ImGui's assertions are illusion assertions.

#include <illusion/core/Assert.hpp>

/// ImWchar is 32-bit, for icons above U+FFFF.
#define IMGUI_USE_WCHAR32
/// Functions Dear ImGui has obsoleted are not available.
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
/// No ImFileOpen() and imgui.ini: settings go through UiContext::saveSettings().
#define IMGUI_DISABLE_FILE_FUNCTIONS
/// No default "open URL in browser" handler.
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
/// No Win32 clipboard and IME helpers: UiPlatformBackend provides them for every OS.
#define IMGUI_DISABLE_WIN32_FUNCTIONS
/// No embedded ProggyClean font: FontManager loads the editor fonts.
#define IMGUI_DISABLE_DEFAULT_FONT
/// Test engine hooks, implemented by UiItemRegistry.
#define IMGUI_ENABLE_TEST_ENGINE

/// Dear ImGui's assertion macro, mapped to ILL_ASSERT (the expression is passed as an argument, so braces
/// in it are not read as format fields).
#define IM_ASSERT(expression) ILL_ASSERT(expression, "{}", #expression)
