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
    src/AudioWorld.cpp)
add_library(illusion::audio ALIAS illusion_audio)

target_include_directories(illusion_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(illusion_audio
    PUBLIC illusion::core illusion::platform
    PRIVATE illusion_audio_backend)

illusion_configure_target(illusion_audio)
