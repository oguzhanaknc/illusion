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

# --- Declared for upcoming modules (see ROADMAP.md) ---------------------------------------------------

# GLFW 3.5.1 (Zlib): window, keyboard, mouse.
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.5.1.tar.gz
    URL_HASH SHA256=5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# SDL 3.4.18 (Zlib): gamepad subsystem only.
FetchContent_Declare(sdl3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.tar.gz
    URL_HASH SHA256=c4b08b950bd29d83caae0ab8d884298d27046a9d30700fb42841c50992665c90
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

# glm 1.0.3 (MIT): math.
FetchContent_Declare(glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz
    URL_HASH SHA256=6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)

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

# NVRHI (MIT): graphics abstraction over Vulkan. Pinned to a commit because it needs git submodules
# (Vulkan-Headers).
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
    GIT_SUBMODULES_RECURSE TRUE
    SYSTEM)

# --- Helpers ------------------------------------------------------------------------------------------

# illusion_require_dependency(<name>)
# Fetches a declared dependency (once) and defines the illusion-side target for it:
#   doctest   -> illusion::doctest (and ILLUSION_DOCTEST_SOURCE_DIR)
#   nanobench -> illusion::nanobench
#   tracy     -> Tracy::TracyClient
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
    else()
        message(FATAL_ERROR "illusion_require_dependency: no integration written yet for '${name}'")
    endif()

    set_property(GLOBAL PROPERTY ILLUSION_DEP_${name} TRUE)
endfunction()
