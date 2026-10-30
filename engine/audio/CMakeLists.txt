# illusion::audio — sound playback, 3D audio and mixing for the ECS (miniaudio is private).

illusion_require_dependency(miniaudio)

# miniaudio and stb_vorbis are C libraries, compiled in their own target without the engine's C++ warning
# flags. Sanitizer builds instrument them because the audio thread synchronizes inside miniaudio.
add_library(illusion_audio_backend STATIC src/MiniaudioImpl.c)
target_link_libraries(illusion_audio_backend PUBLIC illusion::miniaudio)
illusion_apply_sanitizer(illusion_audio_backend)

add_library(illusion_audio STATIC
    src/AudioComponents.cpp
    src/AudioDecoder.cpp
    src/AudioMixer.cpp
    src/AudioWorld.cpp
    src/AudioWorldService.cpp)
add_library(illusion::audio ALIAS illusion_audio)

target_include_directories(illusion_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(illusion_audio
    PUBLIC illusion::core illusion::platform illusion::assets
    PRIVATE illusion_audio_backend)

illusion_configure_target(illusion_audio)

# illusion::audio_api — headers of the audio Components and of AudioService, for game modules: no code, so
# a shared module gets no copy of the library (illusion_add_module(... USES audio), ADR 0041).
add_library(illusion_audio_api INTERFACE)
add_library(illusion::audio_api ALIAS illusion_audio_api)
target_include_directories(illusion_audio_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_audio_api INTERFACE illusion::core)
