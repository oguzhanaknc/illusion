# The engine's CMake package (docs/adr/0043-project-modules-built-by-the-editor.md, docs/adr/0055-exported-games.md).
#
# illusion_write_sdk() writes a CMake package to <build>/sdk that projects find with find_package(illusion CONFIG):
#
#   - the module SDK: the imported targets a module links (illusion::core and the header-only
#     illusion::<name>_api targets), illusion_add_module() and illusion_project_modules();
#   - the component `player` (find_package(illusion CONFIG COMPONENTS player)): illusion::player with every
#     engine and third-party static library it links, the imported executables illusion::pack and
#     illusion::icon, illusion_add_game() and illusion_package_game().
#
# Next to it, <build>/sdk/<Config>/illusion-sdk.json describes the toolchain of that configuration, which the
# editor passes to CMake when it configures a project's modules, so they get the build key of the editor, and
# names the illusion_pack that `export.build` cooks and packs a game's data with.
#
# Both target sets are export sets, so `cmake --install <build> --prefix <dir>` also writes a relocatable copy:
# headers in include/, static libraries in lib/<Config>/, illusion_pack in bin/<Config>/ and the package with
# its sdk.json files in lib/cmake/illusion/. Install components: `sdk` (headers, the module SDK, the package
# files) and `player` (the libraries behind illusion::player, illusion_pack, illusion_add_game()).

include_guard(GLOBAL)
include(GNUInstallDirs)

# Appends "<value>" as a JSON string to the list variable <list>.
function(illusion_sdk_json_string list value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${list} ${${list}} "\"${value}\"" PARENT_SCOPE)
endfunction()

# Every target that <roots> link, directly or through other targets, including the roots. Link items are
# searched for target names inside generator expressions too. Imported targets go to <imported>.
function(_illusion_sdk_link_closure out imported)
    set(queue ${ARGN})
    set(visited ${ARGN})
    set(found_imported "")
    while(queue)
        list(POP_FRONT queue current)
        foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(items ${current} ${property})
            if(NOT items)
                continue()
            endif()
            foreach(item IN LISTS items)
                # Exports leave out what only this build links (wrapped libraries, see _illusion_sdk_wrap).
                string(REGEX REPLACE "\\$<BUILD_LOCAL_INTERFACE:[^>]*>" "" item "${item}")
                string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" names "${item}")
                foreach(name IN LISTS names)
                    if(NOT TARGET "${name}")
                        continue()
                    endif()
                    get_target_property(aliased "${name}" ALIASED_TARGET)
                    if(aliased)
                        set(name "${aliased}")
                    endif()
                    get_target_property(is_imported "${name}" IMPORTED)
                    if(is_imported)
                        list(APPEND found_imported "${name}")
                    elseif(NOT name IN_LIST visited)
                        list(APPEND visited "${name}")
                        list(APPEND queue "${name}")
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endwhile()
    list(REMOVE_DUPLICATES found_imported)
    set(${out} ${visited} PARENT_SCOPE)
    set(${imported} ${found_imported} PARENT_SCOPE)
endfunction()

# Installs the public headers of <targets> into include/: the BUILD_INTERFACE include directories of engine
# targets (in the source tree, or generated in the build tree outside the dependencies) and the directories
# that third-party targets name in their ILLUSION_INSTALL_HEADERS property.
function(_illusion_sdk_install_headers)
    set(directories "")
    set(third_party "")
    foreach(target IN LISTS ARGN)
        get_target_property(includes ${target} INTERFACE_INCLUDE_DIRECTORIES)
        if(includes)
            string(REGEX MATCHALL "\\$<BUILD_INTERFACE:[^>]+>" entries "${includes}")
            foreach(entry IN LISTS entries)
                string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.+)>$" "\\1" directory "${entry}")
                cmake_path(IS_PREFIX FETCHCONTENT_BASE_DIR "${directory}" NORMALIZE in_dependencies)
                cmake_path(IS_PREFIX PROJECT_SOURCE_DIR "${directory}" NORMALIZE in_source)
                cmake_path(IS_PREFIX PROJECT_BINARY_DIR "${directory}" NORMALIZE in_build)
                if(NOT in_dependencies AND (in_source OR in_build))
                    list(APPEND directories "${directory}")
                endif()
            endforeach()
        endif()
        get_target_property(headers ${target} ILLUSION_INSTALL_HEADERS)
        if(headers)
            list(APPEND third_party ${headers})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES directories)
    foreach(directory IN LISTS directories)
        install(DIRECTORY "${directory}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT sdk
            FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl")
    endforeach()
    list(REMOVE_DUPLICATES third_party)
    foreach(directory IN LISTS third_party)
        install(DIRECTORY "${directory}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT sdk
            FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.inl")
    endforeach()
endfunction()

# The package recreates a third-party static library that cannot be exported (it is in an export of its own
# project) as the imported library illusion::<name>_library behind its wrapper illusion::<name>, an INTERFACE
# target that links it with $<BUILD_LOCAL_INTERFACE:...> and names it in ILLUSION_SDK_WRAPS. Writes
# <sdk>/illusionWrappedTargets.cmake, which defines the imported libraries and includes one
# illusionWrappedTargets-<Config>.cmake per configuration with their locations (<sdk>/install/ holds those of the
# installed package), and installs the libraries with the player component.
function(_illusion_sdk_wrap sdk package_dir)
    file(RELATIVE_PATH to_prefix "${PROJECT_BINARY_DIR}/prefix/${package_dir}" "${PROJECT_BINARY_DIR}/prefix")
    string(REGEX REPLACE "/$" "" to_prefix "${to_prefix}")
    set(nl "\n")
    set(definitions "# Generated by cmake/IllusionSdk.cmake: the wrapped third-party libraries of illusion::player.${nl}")
    set(build_locations "")
    set(install_locations "")
    foreach(wrapper IN LISTS ARGN)
        get_target_property(library ${wrapper} ILLUSION_SDK_WRAPS)
        string(REGEX REPLACE "^illusion_" "" name "${wrapper}")
        set(imported "illusion::${name}_library")
        # Its link interface keeps system libraries, frameworks and flags; header-only targets go.
        get_target_property(items ${library} INTERFACE_LINK_LIBRARIES)
        set(links "")
        foreach(item IN LISTS items)
            string(REGEX REPLACE "^\\$<(LINK_ONLY|TARGET_NAME):(.+)>$" "\\2" bare "${item}")
            # The package config finds Threads.
            if(TARGET "${bare}" AND NOT bare STREQUAL "Threads::Threads")
                get_target_property(type "${bare}" TYPE)
                get_target_property(nested "${bare}" INTERFACE_LINK_LIBRARIES)
                if(type STREQUAL "INTERFACE_LIBRARY" AND NOT nested)
                    continue()
                endif()
                message(FATAL_ERROR "${wrapper}: the package cannot wrap ${library}, which links the target ${bare}")
            endif()
            list(APPEND links "${item}")
        endforeach()
        get_target_property(options ${library} INTERFACE_LINK_OPTIONS)
        if(NOT options)
            set(options "")
        endif()
        string(APPEND definitions
            "add_library(${imported} STATIC IMPORTED)${nl}"
            "set_target_properties(${imported} PROPERTIES${nl}"
            "    INTERFACE_LINK_LIBRARIES [==[${links}]==]${nl}"
            "    INTERFACE_LINK_OPTIONS [==[${options}]==])${nl}"
            "set_property(TARGET illusion::${name} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${imported})${nl}")
        set(property "IMPORTED_LOCATION_$<UPPER_CASE:$<CONFIG>>")
        set(configurations
            "set_property(TARGET ${imported} APPEND PROPERTY IMPORTED_CONFIGURATIONS $<UPPER_CASE:$<CONFIG>>)${nl}")
        string(APPEND build_locations "${configurations}"
            "set_target_properties(${imported} PROPERTIES ${property} \"$<TARGET_FILE:${library}>\")${nl}")
        string(APPEND install_locations "${configurations}"
            "set_target_properties(${imported} PROPERTIES ${property}${nl}    \"\${CMAKE_CURRENT_LIST_DIR}/"
            "${to_prefix}/${CMAKE_INSTALL_LIBDIR}/$<CONFIG>/$<TARGET_FILE_NAME:${library}>\")${nl}")
        install(FILES "$<TARGET_FILE:${library}>" DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT player)
    endforeach()
    string(APPEND definitions
        "file(GLOB _illusion_wrapped \"\${CMAKE_CURRENT_LIST_DIR}/illusionWrappedTargets-*.cmake\")${nl}"
        "foreach(_illusion_file IN LISTS _illusion_wrapped)${nl}"
        "    include(\"\${_illusion_file}\")${nl}"
        "endforeach()${nl}"
        "unset(_illusion_wrapped)${nl}"
        "unset(_illusion_file)${nl}")
    file(WRITE "${sdk}/illusionWrappedTargets.cmake" "${definitions}")
    file(GENERATE OUTPUT "${sdk}/illusionWrappedTargets-$<CONFIG>.cmake" CONTENT "${build_locations}")
    file(GENERATE OUTPUT "${sdk}/install/illusionWrappedTargets-$<CONFIG>.cmake" CONTENT "${install_locations}")
    install(FILES "${sdk}/illusionWrappedTargets.cmake" "${sdk}/install/illusionWrappedTargets-$<CONFIG>.cmake"
        DESTINATION "${package_dir}" COMPONENT player)
endfunction()

# Adds <targets> to the export set <set> with their install rules in <component>.
function(_illusion_sdk_export set component)
    foreach(target IN LISTS ARGN)
        get_target_property(editor_only ${target} ILLUSION_EDITOR_ONLY)
        if(editor_only)
            message(FATAL_ERROR "The illusion package must not hold the editor-only target '${target}' (ADR 0030)")
        endif()
        if(target MATCHES "^illusion_(.+)$")
            set_target_properties(${target} PROPERTIES EXPORT_NAME ${CMAKE_MATCH_1})
        endif()
    endforeach()
    install(TARGETS ${ARGN} EXPORT ${set}
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT ${component}
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT ${component}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT ${component}
        OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>/objects" COMPONENT ${component}
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endfunction()

function(illusion_write_sdk)
    if(ILLUSION_PROFILING)
        # Profiling builds link modules statically (ADR 0041) and link Tracy; they have no package.
        return()
    endif()
    set(sdk "${PROJECT_BINARY_DIR}/sdk")
    set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/illusion")

    set(module_targets illusion_core illusion_glm illusion_yyjson illusion_physics_api illusion_audio_api
        illusion_particles_api illusion_input_api illusion_render_api illusion_runtime_api)
    _illusion_sdk_export(illusionTargets sdk ${module_targets})
    export(EXPORT illusionTargets NAMESPACE illusion:: FILE "${sdk}/illusionTargets.cmake")
    install(EXPORT illusionTargets NAMESPACE illusion:: DESTINATION "${package_dir}" COMPONENT sdk)

    set(player OFF)
    set(header_targets ${module_targets})
    if(TARGET illusion_player)
        set(player ON)
        set(roots illusion_player)
        foreach(tool illusion_pack illusion_icon)
            if(TARGET ${tool})
                list(APPEND roots ${tool})
            endif()
        endforeach()
        _illusion_sdk_link_closure(player_targets imported ${roots})
        list(REMOVE_ITEM player_targets ${module_targets})
        list(REMOVE_ITEM imported Threads::Threads)
        if(imported)
            message(FATAL_ERROR "illusion::player links imported targets the package cannot recreate: ${imported}")
        endif()
        _illusion_sdk_export(illusionPlayerTargets player ${player_targets})
        set(wrappers "")
        foreach(target IN LISTS player_targets)
            get_target_property(wraps ${target} ILLUSION_SDK_WRAPS)
            if(wraps)
                list(APPEND wrappers ${target})
            endif()
        endforeach()
        _illusion_sdk_wrap("${sdk}" "${package_dir}" ${wrappers})
        export(EXPORT illusionPlayerTargets NAMESPACE illusion:: FILE "${sdk}/illusionPlayerTargets.cmake")
        install(EXPORT illusionPlayerTargets NAMESPACE illusion:: DESTINATION "${package_dir}" COMPONENT player)
        install(FILES "${PROJECT_SOURCE_DIR}/cmake/IllusionGame.cmake" "${PROJECT_SOURCE_DIR}/cmake/IllusionGamePackage.cmake"
            "${PROJECT_SOURCE_DIR}/cmake/PackGameData.cmake"
            DESTINATION "${package_dir}" COMPONENT player)
        list(APPEND header_targets ${player_targets})
    endif()
    _illusion_sdk_install_headers(${header_targets})

    # The build tree's package uses the engine's CMake files in place; the installed one carries copies.
    set(ILLUSION_SDK_CMAKE_DIR "${PROJECT_SOURCE_DIR}/cmake")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/sdk/illusionConfig.cmake.in" "${sdk}/illusionConfig.cmake" @ONLY)
    set(ILLUSION_SDK_CMAKE_DIR "\${CMAKE_CURRENT_LIST_DIR}")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/sdk/illusionConfig.cmake.in" "${sdk}/install/illusionConfig.cmake"
        @ONLY)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/sdk/illusionConfigVersion.cmake.in"
        "${sdk}/illusionConfigVersion.cmake" @ONLY)
    install(FILES "${sdk}/install/illusionConfig.cmake" "${sdk}/illusionConfigVersion.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/IllusionCompilerSettings.cmake" "${PROJECT_SOURCE_DIR}/cmake/IllusionModules.cmake"
        DESTINATION "${package_dir}" COMPONENT sdk)

    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(multi_config)
        set(configs ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(configs ${CMAKE_BUILD_TYPE})
    endif()
    # Project modules are built one configuration at a time.
    if(CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
        set(generator "Ninja")
    else()
        set(generator "${CMAKE_GENERATOR}")
    endif()
    # On Windows, MSVC and clang-cl need the Visual Studio developer environment (INCLUDE, LIB, rc.exe on PATH).
    # The editor runs the vcvars script of the compiler's Visual Studio when it was not started inside one.
    set(environment_script "")
    if(WIN32 AND MSVC AND NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
            set(vcvars vcvarsarm64.bat)
        else()
            set(vcvars vcvars64.bat)
        endif()
        set(directory "${CMAKE_CXX_COMPILER}")
        foreach(level RANGE 12)
            cmake_path(GET directory PARENT_PATH parent)
            if(parent STREQUAL directory)
                break()
            endif()
            set(directory "${parent}")
            if(EXISTS "${directory}/VC/Auxiliary/Build/${vcvars}")
                set(environment_script "${directory}/VC/Auxiliary/Build/${vcvars}")
                break()
            endif()
        endforeach()
    endif()
    set(environment_json "")
    illusion_sdk_json_string(environment_json "${environment_script}")
    if(player)
        set(player_json "true")
    else()
        set(player_json "false")
    endif()
    foreach(config IN LISTS configs)
        string(TOUPPER "${config}" upper)
        set(arguments "")
        illusion_sdk_json_string(arguments "-DCMAKE_BUILD_TYPE=${config}")
        illusion_sdk_json_string(arguments "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
        illusion_sdk_json_string(arguments "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
        illusion_sdk_json_string(arguments "-DCMAKE_CXX_FLAGS_${upper}=${CMAKE_CXX_FLAGS_${upper}}")
        foreach(kind MODULE EXE)
            illusion_sdk_json_string(arguments "-DCMAKE_${kind}_LINKER_FLAGS=${CMAKE_${kind}_LINKER_FLAGS}")
            illusion_sdk_json_string(arguments
                "-DCMAKE_${kind}_LINKER_FLAGS_${upper}=${CMAKE_${kind}_LINKER_FLAGS_${upper}}")
        endforeach()
        illusion_sdk_json_string(arguments "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        foreach(variable CMAKE_MAKE_PROGRAM CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_OSX_ARCHITECTURES
                CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_SYSROOT CMAKE_CXX_SCAN_FOR_MODULES)
            if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
                illusion_sdk_json_string(arguments "-D${variable}=${${variable}}")
            endif()
        endforeach()
        if(CMAKE_GENERATOR_PLATFORM)
            illusion_sdk_json_string(arguments "-A")
            illusion_sdk_json_string(arguments "${CMAKE_GENERATOR_PLATFORM}")
        endif()
        if(CMAKE_GENERATOR_TOOLSET)
            illusion_sdk_json_string(arguments "-T")
            illusion_sdk_json_string(arguments "${CMAKE_GENERATOR_TOOLSET}")
        endif()
        list(JOIN arguments ",\n    " arguments)
        set(fields "")
        illusion_sdk_json_string(fields "${CMAKE_COMMAND}")
        illusion_sdk_json_string(fields "${generator}")
        illusion_sdk_json_string(fields "${config}")
        illusion_sdk_json_string(fields "${CMAKE_SHARED_MODULE_PREFIX}")
        illusion_sdk_json_string(fields "${CMAKE_SHARED_MODULE_SUFFIX}")
        list(GET fields 0 cmake)
        list(GET fields 1 generator_json)
        list(GET fields 2 config_json)
        list(GET fields 3 prefix)
        list(GET fields 4 suffix)
        # The build tree's file names the package and illusion_pack (which exports run) by their absolute paths;
        # the installed one by paths relative to the file's directory, so that the installed SDK can move. The
        # files are written at generate time, when the location of illusion_pack is known.
        file(RELATIVE_PATH pack_directory "${PROJECT_BINARY_DIR}/prefix/${package_dir}/${config}"
            "${PROJECT_BINARY_DIR}/prefix/${CMAKE_INSTALL_BINDIR}/${config}")
        foreach(variant build install)
            set(package "")
            set(pack "")
            if(variant STREQUAL "build")
                illusion_sdk_json_string(package "${sdk}")
                if(player AND TARGET illusion_pack)
                    set(pack "\"$<TARGET_FILE:illusion_pack>\"")
                endif()
                set(file "${sdk}/${config}/illusion-sdk.json")
            else()
                illusion_sdk_json_string(package "..")
                if(player AND TARGET illusion_pack)
                    set(pack "\"${pack_directory}/$<TARGET_FILE_NAME:illusion_pack>\"")
                endif()
                set(file "${sdk}/install/${config}/illusion-sdk.json")
            endif()
            if(NOT pack)
                set(pack "\"\"")
            endif()
            string(CONCAT json
                "{\n  \"format\": \"illusion.sdk\",\n  \"version\": 2,\n  \"engineVersion\": \"${PROJECT_VERSION}\",\n"
                "  \"cmake\": ${cmake},\n  \"generator\": ${generator_json},\n  \"config\": ${config_json},\n"
                "  \"package\": ${package},\n  \"player\": ${player_json},\n  \"pack\": ${pack},\n"
                "  \"modulePrefix\": ${prefix},\n  \"moduleSuffix\": ${suffix},\n"
                "  \"environmentScript\": ${environment_json},\n"
                "  \"arguments\": [\n    ${arguments}\n  ]\n}\n")
            file(GENERATE OUTPUT "${file}" CONTENT "${json}" CONDITION "$<CONFIG:${config}>")
        endforeach()
    endforeach()
    install(FILES "${sdk}/install/$<CONFIG>/illusion-sdk.json" DESTINATION "${package_dir}/$<CONFIG>" COMPONENT sdk)
endfunction()
