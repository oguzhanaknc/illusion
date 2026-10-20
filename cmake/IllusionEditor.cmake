# Editor-only targets and the boundary that keeps them out of games (ADR 0030).
#
#   illusion_add_editor_library(<name> [STATIC|INTERFACE|...] <sources>...)
#   illusion_add_editor_executable(<name> <sources>...)
#
# Both create the target and mark it ILLUSION_EDITOR_ONLY. Editor executables are written to
# <build>/bin/<Config>/, next to the editor-data folder. At the end of the configure step,
# illusion_check_editor_boundary() walks the link dependencies of every target that is not editor-only
# (engine libraries, samples, tools, game modules, tests of the engine) and fails if any of them reaches an
# editor-only target, directly or through other targets.

include_guard(GLOBAL)

function(illusion_add_editor_library name)
    add_library(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES ILLUSION_EDITOR_ONLY TRUE)
endfunction()

function(illusion_add_editor_executable name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES
        ILLUSION_EDITOR_ONLY TRUE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
endfunction()

# Marks an existing target as editor-only (for third-party targets the editor defines).
function(illusion_mark_editor_only name)
    set_target_properties(${name} PROPERTIES ILLUSION_EDITOR_ONLY TRUE)
endfunction()

function(_illusion_collect_targets directory out)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        _illusion_collect_targets("${subdirectory}" nested)
        list(APPEND targets ${nested})
    endforeach()
    set(${out} ${targets} PARENT_SCOPE)
endfunction()

# Link items that name targets: plain names and $<LINK_ONLY:...>; other generator expressions and flags
# are skipped.
function(_illusion_link_targets target out)
    set(result "")
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(items ${target} ${property})
        if(NOT items)
            continue()
        endif()
        foreach(item IN LISTS items)
            string(REGEX REPLACE "^\\$<LINK_ONLY:(.+)>$" "\\1" item "${item}")
            if(TARGET "${item}")
                get_target_property(aliased "${item}" ALIASED_TARGET)
                if(aliased)
                    set(item "${aliased}")
                endif()
                list(APPEND result "${item}")
            endif()
        endforeach()
    endforeach()
    set(${out} ${result} PARENT_SCOPE)
endfunction()

function(illusion_check_editor_boundary)
    _illusion_collect_targets("${CMAKE_SOURCE_DIR}" targets)
    set(violations "")
    foreach(target IN LISTS targets)
        get_target_property(editorOnly ${target} ILLUSION_EDITOR_ONLY)
        if(editorOnly)
            continue()
        endif()
        # Breadth-first search that remembers how each target was reached, to print the path.
        set(queue ${target})
        set(visited ${target})
        set(path_${target} "${target}")
        while(queue)
            list(POP_FRONT queue current)
            _illusion_link_targets(${current} dependencies)
            foreach(dependency IN LISTS dependencies)
                if(dependency IN_LIST visited)
                    continue()
                endif()
                list(APPEND visited ${dependency})
                set(path_${dependency} "${path_${current}} -> ${dependency}")
                get_target_property(dependencyEditorOnly ${dependency} ILLUSION_EDITOR_ONLY)
                if(dependencyEditorOnly)
                    string(APPEND violations "
  '${target}' links the editor-only target '${dependency}' "
                                             "(${path_${dependency}})")
                    continue()
                endif()
                list(APPEND queue ${dependency})
            endforeach()
        endwhile()
    endforeach()
    if(violations)
        message(FATAL_ERROR "Games and the engine must not depend on editor code (see ADR 0030):${violations}")
    endif()
endfunction()

# Run the check once, after every target of the build exists.
get_property(illusion_boundary_deferred GLOBAL PROPERTY ILLUSION_EDITOR_BOUNDARY_DEFERRED)
if(NOT illusion_boundary_deferred)
    set_property(GLOBAL PROPERTY ILLUSION_EDITOR_BOUNDARY_DEFERRED TRUE)
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL illusion_check_editor_boundary)
endif()
