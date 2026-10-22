# illusion::assets — CPU-side asset data and importers: images, meshes, materials, glTF models.

add_library(illusion_assets STATIC
    src/AssetCooker.cpp
    src/AssetDatabase.cpp
    src/AssetFormat.cpp
    src/AssetImport.cpp
    src/AssetReflection.cpp
    src/AssetStreamer.cpp
    src/Environment.cpp
    src/Gltf.cpp
    src/Image.cpp
    src/Mesh.cpp
    src/ThirdPartyImpl.cpp)
add_library(illusion::assets ALIAS illusion_assets)

target_include_directories(illusion_assets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(cgltf)
illusion_require_dependency(stb)
target_link_libraries(illusion_assets
    PUBLIC illusion::core illusion::platform
    PRIVATE illusion::cgltf illusion::stb)

illusion_configure_target(illusion_assets)
