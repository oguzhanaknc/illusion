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

# Jolt Physics 5.6.0 (MIT). Its CMake project lives in the Build/ subdirectory.
FetchContent_Declare(jolt
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    SOURCE_SUBDIR Build)

# miniaudio 0.11.25 (Public Domain / MIT-0): audio.
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
    elseif(name STREQUAL "slang")
        if(ILLUSION_SLANGC_EXECUTABLE)
            set(ILLUSION_SLANGC "${ILLUSION_SLANGC_EXECUTABLE}" CACHE INTERNAL "")
        elseif(NOT illusion_slang_platform)
            message(FATAL_ERROR "No prebuilt Slang for host ${CMAKE_HOST_SYSTEM_PROCESSOR}; set ILLUSION_SLANGC_EXECUTABLE")
        else()
            FetchContent_MakeAvailable(slang)
            set(ILLUSION_SLANGC "${slang_SOURCE_DIR}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}" CACHE INTERNAL "")
        endif()
    else()
        message(FATAL_ERROR "illusion_require_dependency: no integration written yet for '${name}'")
    endif()

    set_property(GLOBAL PROPERTY ILLUSION_DEP_${name} TRUE)
endfunction()
