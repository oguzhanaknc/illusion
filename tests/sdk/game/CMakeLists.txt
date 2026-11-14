# A tiny game built outside the engine's build with the illusion package and its component `player`
# (ADR 0055; run by tests/sdk/PlayerSdk.cmake): one static module and the data of this directory.

cmake_minimum_required(VERSION 3.28)
project(sdk_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(illusion REQUIRED CONFIG COMPONENTS player)

illusion_add_module(hello STATIC SOURCES modules/hello/hello.cpp)
illusion_add_game(sdk_game
    MODULES hello
    NAME "SDK Game"
    ORGANIZATION "illusion tests"
    VERSION "1.0.0"
    STARTUP_SCENE "assets:/scenes/main.scene"
    PROJECT "${CMAKE_CURRENT_SOURCE_DIR}")
