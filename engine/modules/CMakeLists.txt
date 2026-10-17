# illusion::modules — loads, checks and hot-reloads game and plugin modules (see cmake/IllusionModules.cmake).

add_library(illusion_modules STATIC
    src/ModuleHost.cpp
    src/SharedLibrary.cpp)
add_library(illusion::modules ALIAS illusion_modules)

target_include_directories(illusion_modules PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(illusion_modules PUBLIC illusion::core PRIVATE ${CMAKE_DL_LIBS})

illusion_configure_target(illusion_modules)
