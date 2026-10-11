# illusion::render — GPU device (Vulkan through NVRHI), swapchains, built-in shaders.

add_library(illusion_render STATIC
    src/DebugTriangle.cpp
    src/RenderDevice.cpp
    src/Swapchain.cpp
    src/VulkanDispatch.cpp)
add_library(illusion::render ALIAS illusion_render)

target_include_directories(illusion_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(nvrhi)
illusion_require_dependency(glfw)
target_link_libraries(illusion_render
    PUBLIC illusion::core illusion::platform nvrhi
    PRIVATE nvrhi_vk Vulkan::Headers glfw)

if(UNIX AND NOT APPLE)
    target_link_libraries(illusion_render PRIVATE ${CMAKE_DL_LIBS})
endif()

illusion_add_shaders(illusion_render
    NAMESPACE illusion::shaders
    HEADER illusion/render/BuiltinShaders.hpp
    SHADERS
        debugTriangleVertex shaders/DebugTriangle.slang vertexMain vertex
        debugTriangleFragment shaders/DebugTriangle.slang fragmentMain fragment)

illusion_configure_target(illusion_render)
