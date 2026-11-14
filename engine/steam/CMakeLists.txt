# illusion::steam — the Steam store (ADR 0056), built only when ILLUSION_STEAMWORKS_SDK names a Steamworks SDK.
# The engine's package ships these sources, so a game's build makes the library against its own SDK path
# (cmake/IllusionSteam.cmake).

include("${PROJECT_SOURCE_DIR}/cmake/IllusionSteam.cmake")

if(ILLUSION_STEAMWORKS_SDK)
    illusion_add_steam_library("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
