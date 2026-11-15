# illusion::save — saved games (ADR 0058): the Saved tag, the slot file container, the capture and application
# of the World section, the SaveSystem with its save thread and SaveSystemService, the host's SaveService.

add_library(illusion_save STATIC
    src/SaveCapture.cpp
    src/SaveComponents.cpp
    src/SaveFile.cpp
    src/SaveSystem.cpp
    src/SaveSystemService.cpp)
add_library(illusion::save ALIAS illusion_save)

target_include_directories(illusion_save PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_save PUBLIC illusion::core illusion::platform)

illusion_configure_target(illusion_save)

# illusion::save_api — the SaveService interface, for game modules: no code, so a shared module gets no copy of
# the library (illusion_add_module(... USES save), ADR 0041).
add_library(illusion_save_api INTERFACE)
add_library(illusion::save_api ALIAS illusion_save_api)
target_include_directories(illusion_save_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_save_api INTERFACE illusion::core)
