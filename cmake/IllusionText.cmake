# String tables: English text compiled into the program, with TextKey constants (see the localization
# manual).
#
#   illusion_add_string_table(<target> TABLE <name> SOURCE <en.json> NAMESPACE <ns> HEADER <path>)
#
# Generates <path> (e.g. illusion/input/InputStrings.hpp) with a TextKey constant per key and a source file
# with the English text, adds both to <target> and links it with illusion::text. The directory of SOURCE
# is checked by the CTest text.locales together with every translation in it.

include_guard(GLOBAL)

function(illusion_add_string_table target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "TABLE;SOURCE;NAMESPACE;HEADER" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "illusion_add_string_table: unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required TABLE SOURCE NAMESPACE HEADER)
        if(NOT arg_${required})
            message(FATAL_ERROR "illusion_add_string_table: ${required} is required")
        endif()
    endforeach()

    cmake_path(ABSOLUTE_PATH arg_SOURCE BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE OUTPUT_VARIABLE source)
    set(generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated/strings)
    set(header ${generated_dir}/${arg_HEADER})
    set(cpp ${generated_dir}/${arg_TABLE}StringTable.cpp)

    add_custom_command(
        OUTPUT ${header} ${cpp}
        COMMAND illusion_textc generate --source ${source} --table ${arg_TABLE} --namespace ${arg_NAMESPACE}
                --header ${header} --cpp ${cpp} --include ${arg_HEADER}
        DEPENDS illusion_textc ${source}
        COMMENT "Generating string table '${arg_TABLE}'"
        VERBATIM)

    target_sources(${target} PRIVATE ${header} ${cpp})
    target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${generated_dir}>)
    target_link_libraries(${target} PUBLIC illusion::text)

    cmake_path(GET source PARENT_PATH locale_dir)
    set_property(GLOBAL APPEND PROPERTY ILLUSION_LOCALE_DIRECTORIES ${locale_dir})
endfunction()
