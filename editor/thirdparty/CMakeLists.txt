# Dear ImGui (docking) and ImPlot, built with the editor's configuration header and no stock backends.

illusion_require_dependency(imgui)
illusion_require_dependency(implot)

illusion_add_editor_library(illusion_imgui STATIC
    "${ILLUSION_IMGUI_SOURCE_DIR}/imgui.cpp"
    "${ILLUSION_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${ILLUSION_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${ILLUSION_IMGUI_SOURCE_DIR}/imgui_widgets.cpp")
add_library(illusion::imgui ALIAS illusion_imgui)
target_include_directories(illusion_imgui SYSTEM PUBLIC "${ILLUSION_IMGUI_SOURCE_DIR}")
# The configuration header lives with the editor UI; it only needs illusion::core.
target_include_directories(illusion_imgui PUBLIC "${PROJECT_SOURCE_DIR}/editor/ui/include")
target_compile_definitions(illusion_imgui PUBLIC "IMGUI_USER_CONFIG=\"illusion/editor/ui/ImGuiConfig.hpp\"")
target_link_libraries(illusion_imgui PUBLIC illusion::core)
target_compile_features(illusion_imgui PUBLIC cxx_std_20)
illusion_apply_sanitizer(illusion_imgui)

illusion_add_editor_library(illusion_implot STATIC
    "${ILLUSION_IMPLOT_SOURCE_DIR}/implot.cpp"
    "${ILLUSION_IMPLOT_SOURCE_DIR}/implot_items.cpp")
add_library(illusion::implot ALIAS illusion_implot)
target_include_directories(illusion_implot SYSTEM PUBLIC "${ILLUSION_IMPLOT_SOURCE_DIR}")
target_link_libraries(illusion_implot PUBLIC illusion::imgui)
illusion_apply_sanitizer(illusion_implot)

# IM_ASSERT expands to ILL_ASSERT, which needs a conforming preprocessor (__VA_OPT__) on cl.exe.
foreach(target illusion_imgui illusion_implot)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /Zc:preprocessor /utf-8)
    endif()
endforeach()
