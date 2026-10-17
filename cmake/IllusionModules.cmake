# Game and plugin modules (see engine/core/include/illusion/core/Module.hpp and
# docs/adr/0027-module-system-and-hot-reload.md).
#
# illusion_add_module(<target> [SHARED | STATIC] SOURCES <files...> [LINK <libraries...>])
#
# Builds a module from sources that use ILL_MODULE(). SHARED builds a loadable library that a ModuleHost
# opens and hot-reloads; STATIC builds a library that the game links and adds with ModuleHost::add().
# Without either keyword, ILLUSION_SHARED_MODULES decides (development: shared; exported games: static).
# The module links illusion::core and the LINK libraries privately: a shared module carries its own copy
# of them and exports nothing but its entry point.

include_guard(GLOBAL)

function(illusion_add_module target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "SHARED;STATIC" "" "SOURCES;LINK")
    if(arg_SHARED AND arg_STATIC)
        message(FATAL_ERROR "illusion_add_module(${target}): use SHARED or STATIC, not both")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "illusion_add_module(${target}): no SOURCES")
    endif()
    set(shared ${ILLUSION_SHARED_MODULES})
    if(arg_SHARED)
        set(shared ON)
    elseif(arg_STATIC)
        set(shared OFF)
    endif()
    if(shared AND ILLUSION_PROFILING)
        # A shared module would start a second Tracy client inside the process.
        message(FATAL_ERROR "illusion_add_module(${target}): profiling builds use static modules")
    endif()

    if(shared)
        add_library(${target} MODULE ${arg_SOURCES})
        target_compile_definitions(${target} PRIVATE ILLUSION_SHARED_MODULE=1)
        set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
        # Keep the module's copy of the engine to itself: export only the entry point and bind the
        # module's references to its own definitions.
        if(APPLE)
            target_link_options(${target} PRIVATE "LINKER:-exported_symbol,_illusionModuleEntry")
        elseif(UNIX)
            target_link_options(${target} PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:-Bsymbolic")
        endif()
    else()
        add_library(${target} STATIC ${arg_SOURCES})
    endif()
    target_link_libraries(${target} PRIVATE illusion::core ${arg_LINK})
    illusion_configure_target(${target})
endfunction()
