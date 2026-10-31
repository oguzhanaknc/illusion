# The module SDK (docs/adr/0043-project-modules-built-by-the-editor.md).
#
# illusion_write_module_sdk() writes a CMake package to <build>/sdk that a project's modules find with
# find_package(illusion CONFIG): the imported targets a module links (illusion::core and the header-only
# illusion::<name>_api targets), illusion_add_module() and illusion_project_modules(). Next to it,
# <build>/sdk/<Config>/illusion-sdk.json describes the toolchain of that configuration, which the editor
# passes to CMake when it configures a project's modules, so they get the build key of the editor.

include_guard(GLOBAL)

# Appends "<value>" as a JSON string to the list variable <list>.
function(illusion_sdk_json_string list value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${list} ${${list}} "\"${value}\"" PARENT_SCOPE)
endfunction()

function(illusion_write_module_sdk)
    if(ILLUSION_PROFILING)
        # Profiling builds link modules statically (ADR 0041); there is nothing to load.
        return()
    endif()
    set(sdk "${PROJECT_BINARY_DIR}/sdk")
    set(targets illusion_core illusion_glm illusion_yyjson illusion_physics_api illusion_audio_api
        illusion_input_api illusion_render_api illusion_runtime_api)
    foreach(target IN LISTS targets)
        string(REGEX REPLACE "^illusion_" "" name "${target}")
        set_target_properties(${target} PROPERTIES EXPORT_NAME ${name})
    endforeach()
    export(TARGETS ${targets} NAMESPACE illusion:: FILE "${sdk}/illusionTargets.cmake")
    set(ILLUSION_SDK_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/sdk/illusionConfig.cmake.in" "${sdk}/illusionConfig.cmake" @ONLY)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/sdk/illusionConfigVersion.cmake.in"
        "${sdk}/illusionConfigVersion.cmake" @ONLY)

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
    foreach(config IN LISTS configs)
        string(TOUPPER "${config}" upper)
        set(arguments "")
        illusion_sdk_json_string(arguments "-DCMAKE_BUILD_TYPE=${config}")
        illusion_sdk_json_string(arguments "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
        illusion_sdk_json_string(arguments "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
        illusion_sdk_json_string(arguments "-DCMAKE_CXX_FLAGS_${upper}=${CMAKE_CXX_FLAGS_${upper}}")
        illusion_sdk_json_string(arguments "-DCMAKE_MODULE_LINKER_FLAGS=${CMAKE_MODULE_LINKER_FLAGS}")
        illusion_sdk_json_string(arguments
            "-DCMAKE_MODULE_LINKER_FLAGS_${upper}=${CMAKE_MODULE_LINKER_FLAGS_${upper}}")
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
        illusion_sdk_json_string(fields "${sdk}")
        illusion_sdk_json_string(fields "${CMAKE_SHARED_MODULE_PREFIX}")
        illusion_sdk_json_string(fields "${CMAKE_SHARED_MODULE_SUFFIX}")
        list(GET fields 0 cmake)
        list(GET fields 1 generator_json)
        list(GET fields 2 config_json)
        list(GET fields 3 package)
        list(GET fields 4 prefix)
        list(GET fields 5 suffix)
        file(WRITE "${sdk}/${config}/illusion-sdk.json"
            "{\n  \"format\": \"illusion.sdk\",\n  \"version\": 1,\n  \"engineVersion\": \"${PROJECT_VERSION}\",\n"
            "  \"cmake\": ${cmake},\n  \"generator\": ${generator_json},\n  \"config\": ${config_json},\n"
            "  \"package\": ${package},\n  \"modulePrefix\": ${prefix},\n  \"moduleSuffix\": ${suffix},\n"
            "  \"arguments\": [\n    ${arguments}\n  ]\n}\n")
    endforeach()
endfunction()
