# Compiler flags shared by every illusion target. Apply with illusion_configure_target(<target>).

include_guard(GLOBAL)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # std::format requires macOS 13.3+ with libc++.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version" FORCE)
endif()

if(ILLUSION_SANITIZER AND MSVC)
    message(FATAL_ERROR "ILLUSION_SANITIZER is only supported with GCC/Clang")
endif()

# Warnings understood by GCC and Clang (including clang-cl).
set(ILLUSION_GNU_WARNINGS
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Woverloaded-virtual -Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2
    -Wimplicit-fallthrough -Wnull-dereference)

function(illusion_configure_target target)
    if(MSVC)
        # cl.exe and clang-cl share the MSVC command line.
        target_compile_options(${target} PRIVATE /permissive- /utf-8 /Zc:__cplusplus /Zc:inline /EHsc)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(${target} PRIVATE /W4 /Zc:preprocessor /Zc:throwingNew /MP
                /wd4324) # "structure was padded due to alignment specifier": intended for cache-line alignment
        else()
            # clang-cl: /W4 means -Wall -Wextra, while a literal -Wall would mean MSVC /Wall, i.e.
            # -Weverything. Pass the remaining warnings without -Wall/-Wextra.
            set(clang_cl_warnings ${ILLUSION_GNU_WARNINGS})
            list(REMOVE_ITEM clang_cl_warnings -Wall -Wextra)
            target_compile_options(${target} PRIVATE /W4 ${clang_cl_warnings})
        endif()
        if(ILLUSION_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE ${ILLUSION_GNU_WARNINGS})
        if(ILLUSION_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    illusion_apply_sanitizer(${target})
endfunction()

# Instruments <target> with ILLUSION_SANITIZER. Also used for third-party code whose synchronization the
# sanitizer must see (a thread sanitizer reports false races across uninstrumented code).
function(illusion_apply_sanitizer target)
    if(ILLUSION_SANITIZER STREQUAL "address")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    elseif(ILLUSION_SANITIZER STREQUAL "thread")
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    elseif(ILLUSION_SANITIZER)
        message(FATAL_ERROR "Unknown ILLUSION_SANITIZER value '${ILLUSION_SANITIZER}'")
    endif()
endfunction()
