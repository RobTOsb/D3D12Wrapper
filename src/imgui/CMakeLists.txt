

add_library(imgui STATIC
    imgui_demo.cpp
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_spectrum.cpp
    backends/imgui_impl_dx12.cpp
    backends/imgui_impl_win32.cpp
)

target_compile_definitions(imgui PUBLIC SPECTRUM_USE_DARK_THEME)

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
