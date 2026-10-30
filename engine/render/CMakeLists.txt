# illusion::render — GPU device (Vulkan through NVRHI), swapchains, GPU resources, scene rendering.

add_library(illusion_render STATIC
    src/Components.cpp
    src/DebugDraw.cpp
    src/DebugTriangle.cpp
    src/EnvironmentBaker.cpp
    src/GpuCulling.cpp
    src/PostProcessor.cpp
    src/ReadbackRing.cpp
    src/RenderAssets.cpp
    src/RenderDevice.cpp
    src/RenderResources.cpp
    src/SceneRenderer.cpp
    src/Shadows.cpp
    src/Swapchain.cpp
    src/UploadQueue.cpp
    src/VulkanDispatch.cpp)
add_library(illusion::render ALIAS illusion_render)

target_include_directories(illusion_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(nvrhi)
illusion_require_dependency(glfw)
target_link_libraries(illusion_render
    PUBLIC illusion::core illusion::platform illusion::assets nvrhi
    PRIVATE nvrhi_vk Vulkan::Headers glfw)

if(UNIX AND NOT APPLE)
    target_link_libraries(illusion_render PRIVATE ${CMAKE_DL_LIBS})
endif()

illusion_add_shaders(illusion_render
    NAMESPACE illusion::shaders
    HEADER illusion/render/BuiltinShaders.hpp
    SHADERS
        debugDrawVertex shaders/DebugDraw.slang vertexMain vertex
        debugDrawFragment shaders/DebugDraw.slang fragmentMain fragment
        debugTriangleVertex shaders/DebugTriangle.slang vertexMain vertex
        debugTriangleFragment shaders/DebugTriangle.slang fragmentMain fragment
        forwardPbrVertex shaders/ForwardPbr.slang vertexMain vertex
        forwardPbrFragment shaders/ForwardPbr.slang fragmentMain fragment
        prepassFragment shaders/ForwardPbr.slang prepassMain fragment
        prepassMaskedFragment shaders/ForwardPbr.slang prepassMaskedMain fragment
        forwardPbrBindlessVertex shaders/ForwardPbrBindless.slang vertexMain vertex
        forwardPbrBindlessFragment shaders/ForwardPbrBindless.slang fragmentMain fragment
        prepassBindlessFragment shaders/ForwardPbrBindless.slang prepassMain fragment
        prepassMaskedBindlessFragment shaders/ForwardPbrBindless.slang prepassMaskedMain fragment
        prepassIdsFragment shaders/ForwardPbr.slang prepassIdsMain fragment
        prepassMaskedIdsFragment shaders/ForwardPbr.slang prepassMaskedIdsMain fragment
        prepassIdsBindlessFragment shaders/ForwardPbrBindless.slang prepassIdsMain fragment
        prepassMaskedIdsBindlessFragment shaders/ForwardPbrBindless.slang prepassMaskedIdsMain fragment
        entityIdFragment shaders/ForwardPbr.slang entityIdMain fragment
        wireframeFragment shaders/ForwardPbr.slang wireframeMain fragment
        wireframeBindlessFragment shaders/ForwardPbrBindless.slang wireframeMain fragment
        occlusionCull shaders/OcclusionCull.slang cullMain compute
        hizReduce shaders/HiZ.slang hizReduceMain compute
        gtao shaders/Gtao.slang gtaoMain compute
        gtaoBlur shaders/GtaoBlur.slang gtaoBlurMain compute
        taa shaders/Taa.slang taaMain compute
        bloomDownsample shaders/BloomDownsample.slang downsampleMain compute
        bloomUpsample shaders/BloomUpsample.slang upsampleMain compute
        tonemapVertex shaders/Tonemap.slang vertexMain vertex
        tonemapFragment shaders/Tonemap.slang fragmentMain fragment
        shadowCasterVertex shaders/ShadowCaster.slang vertexMain vertex
        shadowCasterFragment shaders/ShadowCaster.slang fragmentMain fragment
        skyboxVertex shaders/Skybox.slang vertexMain vertex
        skyboxFragment shaders/Skybox.slang fragmentMain fragment
        iblEquirectToCube shaders/IblEquirectToCube.slang equirectToCube compute
        iblDownsample shaders/IblDownsample.slang downsampleCube compute
        iblPrefilter shaders/IblPrefilter.slang prefilterSpecular compute
        iblBrdfLut shaders/IblBrdfLut.slang integrateBrdf compute)

illusion_configure_target(illusion_render)

# illusion::render_api — headers of the saved, GPU-free render Components (SceneComponents.hpp), for
# game modules: no code, so a shared module gets no copy of the
# library (illusion_add_module(... USES render), ADR 0041).
add_library(illusion_render_api INTERFACE)
add_library(illusion::render_api ALIAS illusion_render_api)
target_include_directories(illusion_render_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/assets/include>)
target_link_libraries(illusion_render_api INTERFACE illusion::core)
