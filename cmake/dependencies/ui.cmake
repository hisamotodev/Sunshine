# Embedded ImGui management UI dependencies (SDL3 + Dear ImGui).
# Windows-only for now - the WebUI replacement work only targets Windows.
include_guard(GLOBAL)

CPMGetPackage(SDL3)
CPMGetPackage(imgui)

# Dear ImGui ships as source only (DOWNLOAD_ONLY); compile it and the SDL3 + SDL_Renderer
# backend ourselves, same as config-gui/CMakeLists.txt and
# third-party/libvirtualhid/tools/CMakeLists.txt, which already proved this combination
# builds cleanly under this repo's MinGW/UCRT64 toolchain.
add_library(sunshine_ui_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")

target_include_directories(sunshine_ui_imgui
        SYSTEM PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
        "${imgui_SOURCE_DIR}/misc/cpp")

target_link_libraries(sunshine_ui_imgui PUBLIC SDL3::SDL3)

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES sunshine_ui_imgui)
