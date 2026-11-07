# Game modules of this project, built by the editor with the engine's module SDK (ADR 0043).
# Every subdirectory with a CMakeLists.txt is one module.
cmake_minimum_required(VERSION 3.25)
project(illusion_project_modules LANGUAGES CXX)

find_package(illusion REQUIRED CONFIG)
illusion_project_modules()
