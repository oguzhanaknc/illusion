# Third-party dependencies.
#
# Every dependency is pinned to an exact release archive verified by SHA-256 (or an exact commit for
# repositories that need git submodules). Declarations are cheap: nothing is downloaded until a module
# calls illusion_require_dependency(<name>). When you add or bump a dependency, also update
# THIRD_PARTY_NOTICES.md (see .claude/skills/add-dependency/SKILL.md).

include_guard(GLOBAL)
include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# SYSTEM marks the dependencies' include directories as system headers so our strict warning flags
# (and -Werror) do not apply to third-party code.

# --- Used today -------------------------------------------------------------------------------------

# doctest 2.5.3 (MIT). SOURCE_SUBDIR points at a path without CMakeLists.txt so only the sources are
# fetched; we define our own minimal target below.
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# nanobench 4.6.0 (MIT), header-only micro-benchmark library.
FetchContent_Declare(nanobench
    URL https://github.com/martinus/nanobench/archive/refs/tags/v4.6.0.tar.gz
    URL_HASH SHA256=80fe0835e85da6acc57285354d86b17d5d1dc7182432a5eaea3b2098a17d4c26
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# Tracy 0.14.1 (BSD-3-Clause). The Tracy profiler GUI must be the same version.
FetchContent_Declare(tracy
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
    URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# cgltf 1.15 (MIT): glTF 2.0 parser (single header).
FetchContent_Declare(cgltf
    URL https://github.com/jkuhlmann/cgltf/archive/refs/tags/v1.15.tar.gz
    URL_HASH SHA256=84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# stb (public domain / MIT): stb_image for PNG, JPEG and Radiance HDR decoding. No release tags;
# pinned to a commit archive.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# yyjson 0.13.0 (MIT): JSON reader and writer (scene files). Compiled as a small static library.
FetchContent_Declare(yyjson
    URL https://github.com/ibireme/yyjson/archive/refs/tags/0.13.0.tar.gz
    URL_HASH SHA256=34e0f62a2bc11ab20d601e8ca1cc2b2079503aa45119a19133d89d19b94a0fae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# --- Declared for upcoming modules (see ROADMAP.md) ---------------------------------------------------

# GLFW 3.5.1 (Zlib): window, keyboard, mouse.
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.5.1.tar.gz
    URL_HASH SHA256=5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# SDL 3.4.18 (Zlib): gamepad subsystem only (joystick, haptic, HIDAPI, sensors); no video or audio.
FetchContent_Declare(sdl3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.tar.gz
    URL_HASH SHA256=c4b08b950bd29d83caae0ab8d884298d27046a9d30700fb42841c50992665c90
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# glm 1.0.3 (MIT): math. Header-only; we define our own target with the engine's configuration.
FetchContent_Declare(glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz
    URL_HASH SHA256=6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# Jolt Physics 5.6.0 (MIT): rigid-body physics. Its CMake project lives in the Build/ subdirectory.
FetchContent_Declare(jolt
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR Build)

# miniaudio 0.11.25 (Public Domain / MIT-0): audio devices, mixing, WAV/FLAC/MP3 decoding, and Ogg Vorbis
# through the stb_vorbis copy in its extras/ directory (public domain / MIT).
FetchContent_Declare(miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# NVRHI (MIT): graphics abstraction over Vulkan. No release tags; pinned to a commit archive.
FetchContent_Declare(nvrhi
    URL https://github.com/NVIDIA-RTX/NVRHI/archive/6b96fb03e07539f08327aea76c56d55f1de9d906.tar.gz
    URL_HASH SHA256=f46c733ccc555fc457aaa3df510ee7df0ca089f5d1129d92903029e4fb67c560
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# Vulkan-Headers 1.4.352 (Apache-2.0 OR MIT): the version NVRHI is tested against. The Vulkan loader is
# loaded at run time, so no Vulkan SDK is needed to build.
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.352.tar.gz
    URL_HASH SHA256=4850909d22a8a9767c27daea2b972e49d7c298560573d5b6221ee50db9bf49db
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# Slang 2026.19 shader compiler (Apache-2.0 WITH LLVM-exception), prebuilt for the build host. Only the
# compiler runs at build time; nothing from Slang is linked into the engine.
set(ILLUSION_SLANG_VERSION 2026.19)
if(CMAKE_HOST_WIN32)
    set(illusion_slang_platform windows-x86_64)
    set(illusion_slang_hash fa7629c9ce59c87c821614ae4b52e52f785d4c659a5a06a89fa9ec86d615eeee)
elseif(CMAKE_HOST_APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(illusion_slang_platform macos-aarch64)
    set(illusion_slang_hash fa46822c92d81404a7951de3746093985c1e6f520f4b533e2aa3bea576f0f2f0)
elseif(CMAKE_HOST_APPLE)
    set(illusion_slang_platform macos-x86_64)
    set(illusion_slang_hash 490b6447e3ea6c8fa9dfc45b1dc227db7cf80a60981c4390ed0fdc8ddbedee11)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(illusion_slang_platform linux-x86_64)
    set(illusion_slang_hash bd6cfc47b7353b2cffa36a866b1584749f76f4b2dbebf6dda26bc3fb40dc3c0e)
else()
    set(illusion_slang_platform "")
endif()
if(illusion_slang_platform)
    FetchContent_Declare(slang
        URL https://github.com/shader-slang/slang/releases/download/v${ILLUSION_SLANG_VERSION}/slang-${ILLUSION_SLANG_VERSION}-${illusion_slang_platform}.tar.gz
        URL_HASH SHA256=${illusion_slang_hash}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _illusion_no_cmake)
endif()

# --- Editor only (never linked into games; see ADR 0030 and 0031) -------------------------------------

# Dear ImGui 1.93.0, docking branch (MIT). The editor defines the illusion::imgui target with its own
# configuration header (editor/ui/include/illusion/editor/ui/ImGuiConfig.hpp) and its own backends.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0-docking.tar.gz
    URL_HASH SHA256=746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# ImPlot (MIT), pinned to a commit with the fixes for Dear ImGui 1.93.
FetchContent_Declare(implot
    URL https://github.com/epezent/implot/archive/851cdbdd60d1dfb64c23e987969694de0f547a6b.tar.gz
    URL_HASH SHA256=e11623920915f2fea586773ee3be7340d4c405b943b0f526aafaf5a64bc7cbd1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR _illusion_no_cmake)

# Editor fonts, fetched as single files. Roboto v2.138 and the last Apache-2.0 commit of Roboto Mono:
# never move to Roboto 3.x or a later Roboto Mono, which are under the OFL. Material Symbols Rounded is the
# variable icon font (Apache-2.0); its .codepoints file generates editor/ui Icons.hpp.
set(illusion_roboto_url "https://raw.githubusercontent.com/googlefonts/roboto/ab4876f650a4aeb9b661c83fee47f908c739bb70")
set(illusion_roboto_mono_url "https://raw.githubusercontent.com/googlefonts/RobotoMono/30867cc8bedee415663cd39f5240b92453138840")
set(illusion_symbols_url "https://raw.githubusercontent.com/google/material-design-icons/49d4db35df873165d6bd6ba09b063c7dafbac2f4")
set(ILLUSION_EDITOR_FONT_FILES
    "font_roboto_regular|${illusion_roboto_url}/src/hinted/Roboto-Regular.ttf|56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d|Roboto-Regular.ttf"
    "font_roboto_medium|${illusion_roboto_url}/src/hinted/Roboto-Medium.ttf|2879a5ecb7fbfa13a7fc3e2cdd7fecbf73aa45e91b541dfdfa2c442eed0aac21|Roboto-Medium.ttf"
    "font_roboto_bold|${illusion_roboto_url}/src/hinted/Roboto-Bold.ttf|61f89f8db49261c2f6106e8dccc35df7b2f7ed909020db40a3fc905e95f99334|Roboto-Bold.ttf"
    "font_roboto_license|${illusion_roboto_url}/LICENSE|c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4|Roboto-LICENSE.txt"
    "font_roboto_mono_regular|${illusion_roboto_mono_url}/fonts/ttf/RobotoMono-Regular.ttf|7432e74ff02682c6e207be405f00381569ec96aa247d232762fe721ae41b39e2|RobotoMono-Regular.ttf"
    "font_roboto_mono_license|${illusion_roboto_mono_url}/LICENSE.txt|c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4|RobotoMono-LICENSE.txt"
    "font_symbols|${illusion_symbols_url}/variablefont/MaterialSymbolsRounded%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf|32e4011709e055596eb8d2dd0f7cb547b47c1d40ac9ecb8fad54f7f7eac202e7|MaterialSymbolsRounded.ttf"
    "font_symbols_codepoints|${illusion_symbols_url}/variablefont/MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.codepoints|0c9dae3ed678a81549c3be046bc8045335e5f57f2cbe2287360fbf0c2cc1355f|MaterialSymbols.codepoints"
    "font_symbols_license|${illusion_symbols_url}/LICENSE|58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd|MaterialSymbols-LICENSE.txt")
foreach(entry IN LISTS ILLUSION_EDITOR_FONT_FILES)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 font_name)
    list(GET fields 1 font_url)
    list(GET fields 2 font_hash)
    list(GET fields 3 font_file)
    FetchContent_Declare(${font_name}
        URL "${font_url}"
        URL_HASH SHA256=${font_hash}
        DOWNLOAD_NO_EXTRACT TRUE
        DOWNLOAD_NAME "${font_file}"
        SOURCE_SUBDIR _illusion_no_cmake)
endforeach()

# --- Helpers ------------------------------------------------------------------------------------------

# illusion_require_dependency(<name>)
# Fetches a declared dependency (once) and defines the illusion-side target for it:
#   doctest   -> illusion::doctest (and ILLUSION_DOCTEST_SOURCE_DIR)
#   nanobench -> illusion::nanobench
#   tracy     -> Tracy::TracyClient
#   glm       -> illusion::glm
#   glfw      -> glfw
#   nvrhi     -> nvrhi, nvrhi_vk (Vulkan backend only) and Vulkan::Headers
#   slang     -> ILLUSION_SLANGC (path of the slangc executable)
#   sdl3      -> SDL3::SDL3-static (gamepad subsystems only)
#   cgltf     -> illusion::cgltf (headers; the implementation is compiled in illusion::assets)
#   stb       -> illusion::stb (headers; the implementation is compiled in illusion::assets)
#   yyjson    -> illusion::yyjson (static library; strict JSON only)
#   jolt      -> Jolt::Jolt (static library)
#   miniaudio -> illusion::miniaudio (headers; the implementation is compiled in illusion::audio)
#   imgui     -> ILLUSION_IMGUI_SOURCE_DIR (sources; the editor defines illusion::imgui)
#   implot    -> ILLUSION_IMPLOT_SOURCE_DIR (sources; the editor defines illusion::implot)
#   fonts     -> ILLUSION_EDITOR_FONTS (list of downloaded font, codepoint and licence files)
function(illusion_require_dependency name)
    get_property(done GLOBAL PROPERTY ILLUSION_DEP_${name})
    if(done)
        return()
    endif()

    if(name STREQUAL "doctest")
        FetchContent_MakeAvailable(doctest)
        add_library(illusion_doctest INTERFACE)
        add_library(illusion::doctest ALIAS illusion_doctest)
        target_include_directories(illusion_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
        # Use real std headers so doctest can stringify std::string_view (needs a complete std::ostream).
        target_compile_definitions(illusion_doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
        # Exposed for doctest_discover_tests (scripts/cmake/doctest.cmake).
        set(ILLUSION_DOCTEST_SOURCE_DIR "${doctest_SOURCE_DIR}" CACHE INTERNAL "")
    elseif(name STREQUAL "nanobench")
        FetchContent_MakeAvailable(nanobench)
        add_library(illusion_nanobench INTERFACE)
        add_library(illusion::nanobench ALIAS illusion_nanobench)
        target_include_directories(illusion_nanobench SYSTEM INTERFACE "${nanobench_SOURCE_DIR}/src/include")
    elseif(name STREQUAL "tracy")
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
        set(TRACY_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(tracy)
    elseif(name STREQUAL "glm")
        FetchContent_MakeAvailable(glm)
        add_library(illusion_glm INTERFACE)
        add_library(illusion::glm ALIAS illusion_glm)
        target_include_directories(illusion_glm SYSTEM INTERFACE "${glm_SOURCE_DIR}")
        # Vulkan conventions: depth in [0, 1]; angles in radians; no implicit experimental extensions.
        target_compile_definitions(illusion_glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS
            GLM_FORCE_SILENT_WARNINGS)
    elseif(name STREQUAL "glfw")
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    elseif(name STREQUAL "nvrhi")
        set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
        set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(vulkan_headers)
        set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
        set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
        set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
        set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
        set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
        set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
        set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(nvrhi)
        # nvrhi_vk uses nvrhi's common code but does not declare it; single-pass linkers (GNU ld) then
        # fail when nvrhi precedes nvrhi_vk on the link line.
        target_link_libraries(nvrhi_vk PUBLIC nvrhi)
    elseif(name STREQUAL "sdl3")
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        foreach(subsystem AUDIO VIDEO GPU RENDER CAMERA POWER DIALOG TRAY)
            set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
        endforeach()
        foreach(subsystem JOYSTICK HAPTIC HIDAPI SENSOR VIRTUAL_JOYSTICK)
            set(SDL_${subsystem} ON CACHE BOOL "" FORCE)
        endforeach()
        # Video is disabled on purpose (GLFW owns windows); tell SDL's Linux build not to require X11/Wayland.
        set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(sdl3)
    elseif(name STREQUAL "cgltf")
        FetchContent_MakeAvailable(cgltf)
        add_library(illusion_cgltf INTERFACE)
        add_library(illusion::cgltf ALIAS illusion_cgltf)
        target_include_directories(illusion_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
    elseif(name STREQUAL "stb")
        FetchContent_MakeAvailable(stb)
        add_library(illusion_stb INTERFACE)
        add_library(illusion::stb ALIAS illusion_stb)
        target_include_directories(illusion_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
    elseif(name STREQUAL "jolt")
        # Library only, built with the engine's flags and runtime. The instruction set baseline is SSE4.2
        # (x86-64) so games run on every CPU Steam supports; GPU compute, the debug renderer, the
        # profiler and object streams are not used.
        foreach(option USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD
                GENERATE_DEBUG_SYMBOLS OVERRIDE_CXX_FLAGS INTERPROCEDURAL_OPTIMIZATION ENABLE_ALL_WARNINGS ENABLE_INSTALL
                USE_STATIC_MSVC_RUNTIME_LIBRARY FLOATING_POINT_EXCEPTIONS_ENABLED ENABLE_OBJECT_STREAM
                DEBUG_RENDERER_IN_DEBUG_AND_RELEASE PROFILER_IN_DEBUG_AND_RELEASE
                JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE)
            set(${option} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(USE_SSE4_1 ON CACHE BOOL "" FORCE)
        set(USE_SSE4_2 ON CACHE BOOL "" FORCE)
        # Our JoltJobSystem derives from a Jolt class, so Jolt must emit its typeinfo like the engine does.
        set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(jolt)
        # Jolt's jobs synchronize inside the library, so sanitizer builds instrument it too.
        illusion_apply_sanitizer(Jolt)
    elseif(name STREQUAL "miniaudio")
        FetchContent_MakeAvailable(miniaudio)
        # Headers only; illusion::audio compiles the implementation (with stb_vorbis from miniaudio's
        # extras for Ogg Vorbis). The definitions below change miniaudio's structures, so every user of the
        # header must see them: no resource manager (clips come through the VFS), encoders or generators.
        add_library(illusion_miniaudio INTERFACE)
        add_library(illusion::miniaudio ALIAS illusion_miniaudio)
        target_include_directories(illusion_miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")
        target_compile_definitions(illusion_miniaudio INTERFACE MA_NO_RESOURCE_MANAGER MA_NO_ENCODING
            MA_NO_GENERATION)
        find_package(Threads REQUIRED)
        target_link_libraries(illusion_miniaudio INTERFACE Threads::Threads)
        if(APPLE)
            # Link Core Audio at build time instead of loading it at run time, so apps pass notarization.
            target_compile_definitions(illusion_miniaudio INTERFACE MA_NO_RUNTIME_LINKING)
            target_link_libraries(illusion_miniaudio INTERFACE "-framework CoreFoundation"
                "-framework CoreAudio" "-framework AudioToolbox")
        elseif(UNIX)
            # ALSA, PulseAudio and JACK are loaded at run time, so no development packages are needed.
            target_link_libraries(illusion_miniaudio INTERFACE ${CMAKE_DL_LIBS} m)
        endif()
    elseif(name STREQUAL "yyjson")
        FetchContent_MakeAvailable(yyjson)
        add_library(illusion_yyjson STATIC "${yyjson_SOURCE_DIR}/src/yyjson.c")
        add_library(illusion::yyjson ALIAS illusion_yyjson)
        target_include_directories(illusion_yyjson SYSTEM PUBLIC "${yyjson_SOURCE_DIR}/src")
        # Only standard JSON and the DOM reader/writer are used.
        target_compile_definitions(illusion_yyjson PUBLIC YYJSON_DISABLE_NON_STANDARD=1
            YYJSON_DISABLE_INCR_READER=1 YYJSON_DISABLE_UTILS=1)
    elseif(name STREQUAL "slang")
        if(ILLUSION_SLANGC_EXECUTABLE)
            set(ILLUSION_SLANGC "${ILLUSION_SLANGC_EXECUTABLE}" CACHE INTERNAL "")
        elseif(NOT illusion_slang_platform)
            message(FATAL_ERROR "No prebuilt Slang for host ${CMAKE_HOST_SYSTEM_PROCESSOR}; set ILLUSION_SLANGC_EXECUTABLE")
        else()
            FetchContent_MakeAvailable(slang)
            set(ILLUSION_SLANGC "${slang_SOURCE_DIR}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}" CACHE INTERNAL "")
        endif()
    elseif(name STREQUAL "imgui")
        FetchContent_MakeAvailable(imgui)
        set(ILLUSION_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "")
    elseif(name STREQUAL "implot")
        FetchContent_MakeAvailable(implot)
        set(ILLUSION_IMPLOT_SOURCE_DIR "${implot_SOURCE_DIR}" CACHE INTERNAL "")
    elseif(name STREQUAL "fonts")
        set(files "")
        foreach(entry IN LISTS ILLUSION_EDITOR_FONT_FILES)
            string(REPLACE "|" ";" fields "${entry}")
            list(GET fields 0 font_name)
            list(GET fields 3 font_file)
            FetchContent_MakeAvailable(${font_name})
            list(APPEND files "${${font_name}_SOURCE_DIR}/${font_file}")
        endforeach()
        set(ILLUSION_EDITOR_FONTS "${files}" CACHE INTERNAL "")
    else()
        message(FATAL_ERROR "illusion_require_dependency: no integration written yet for '${name}'")
    endif()

    set_property(GLOBAL PROPERTY ILLUSION_DEP_${name} TRUE)
endfunction()
