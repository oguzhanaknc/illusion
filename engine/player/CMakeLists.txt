# illusion::player — the executable side of an exported game (ADR 0055): runPlayer() opens the window, mounts
# the packed data, adds the game's static modules and runs a GameSession. Games link it through
# illusion_add_game() (cmake/IllusionGame.cmake).

add_library(illusion_player STATIC
    src/Player.cpp
    src/PlayerSetup.cpp
    src/PresentPass.cpp)
add_library(illusion::player ALIAS illusion_player)

target_include_directories(illusion_player PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(illusion_player
    PUBLIC illusion::core illusion::runtime
    PRIVATE illusion::platform illusion::input illusion::assets illusion::physics illusion::audio
        illusion::particles illusion::render illusion::modules)

if(WIN32)
    # CommandLineToArgvW() for the UTF-16 command line.
    target_link_libraries(illusion_player PRIVATE shell32)
endif()

illusion_add_shaders(illusion_player
    NAMESPACE illusion::detail::shaders
    HEADER illusion/player/PlayerShaders.hpp
    SHADERS
        presentVertex shaders/Present.slang vertexMain vertex
        presentFragment shaders/Present.slang fragmentMain fragment)

# The text of the failure dialog (failureText()).
illusion_add_string_table(illusion_player
    TABLE player
    SOURCE data/locales/en.json
    NAMESPACE illusion::player::strings
    HEADER illusion/player/PlayerStrings.hpp)

illusion_configure_target(illusion_player)
