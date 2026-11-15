# illusion::save — saved games (ADR 0058): the Saved tag, the slot file container and the capture and
# application of the World section. CPU only; the save thread and SaveService come with SAVE-2.

add_library(illusion_save STATIC
    src/SaveCapture.cpp
    src/SaveComponents.cpp
    src/SaveFile.cpp)
add_library(illusion::save ALIAS illusion_save)

target_include_directories(illusion_save PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_save PUBLIC illusion::core illusion::platform)

illusion_configure_target(illusion_save)
