# illusion::platform — windows, keyboard/mouse input, OS events, file system.

add_library(illusion_platform STATIC
    src/Archive.cpp
    src/FileSystem.cpp
    src/GlfwKeys.cpp
    src/Ime.cpp
    src/Input.cpp
    src/Platform.cpp
    src/Window.cpp)
add_library(illusion::platform ALIAS illusion_platform)

target_include_directories(illusion_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(glfw)
target_link_libraries(illusion_platform
    PUBLIC illusion::core
    PRIVATE glfw)
# The engine never uses OpenGL; stop glfw3.h from including GL/gl.h (absent on minimal Linux systems).
target_compile_definitions(illusion_platform PRIVATE GLFW_INCLUDE_NONE)

if(WIN32)
    # SHGetKnownFolderPath / CoTaskMemFree for userDataDirectory(); the input method API for setImeRect().
    target_link_libraries(illusion_platform PRIVATE shell32 ole32 imm32)
endif()

illusion_configure_target(illusion_platform)
