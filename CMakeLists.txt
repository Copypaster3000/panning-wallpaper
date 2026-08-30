cmake_minimum_required(VERSION 3.23)

project(PanningWallpaper VERSION 0.1.0 LANGUAGES CXX)

add_executable(PanningWallpaper WIN32
    src/application.cpp
    src/application.h
    src/desktop_host.cpp
    src/desktop_host.h
    src/main.cpp
    src/renderer.cpp
    src/renderer.h
)

target_compile_features(PanningWallpaper PRIVATE cxx_std_20)
target_compile_definitions(PanningWallpaper PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)

if(MSVC)
    target_compile_options(PanningWallpaper PRIVATE
        /EHsc
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )
endif()

target_link_libraries(PanningWallpaper PRIVATE
    d3d11
    dxgi
)
