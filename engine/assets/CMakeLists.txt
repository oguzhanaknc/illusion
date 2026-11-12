# illusion::assets — CPU-side asset data and importers: images, meshes, materials, glTF models.

add_library(illusion_assets STATIC
    src/AssetCooker.cpp
    src/AssetDatabase.cpp
    src/AssetFormat.cpp
    src/AssetImport.cpp
    src/AssetReflection.cpp
    src/AssetStreamer.cpp
    src/Atmosphere.cpp
    src/Bc6hEncoder.cpp
    src/Environment.cpp
    src/Gltf.cpp
    src/Image.cpp
    src/ImportSettings.cpp
    src/MaterialAsset.cpp
    src/Mesh.cpp
    src/TextureCompression.cpp
    src/ThirdPartyImpl.cpp)
add_library(illusion::assets ALIAS illusion_assets)

target_include_directories(illusion_assets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(cgltf)
illusion_require_dependency(stb)
illusion_require_dependency(meshoptimizer)
illusion_require_dependency(bc7enc)
illusion_require_dependency(bcdec)
target_link_libraries(illusion_assets
    PUBLIC illusion::core illusion::platform
    PRIVATE illusion::cgltf illusion::stb illusion::meshoptimizer illusion::bc7enc illusion::bcdec)

illusion_configure_target(illusion_assets)
