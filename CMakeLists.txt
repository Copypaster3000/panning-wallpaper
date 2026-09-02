cmake_minimum_required(VERSION 3.23)

project(PanningWallpaper VERSION 0.1.0 LANGUAGES CXX)

add_executable(PanningWallpaper WIN32
    src/application.cpp
    src/application.h
    src/command_line.cpp
    src/command_line.h
    src/coverage_geometry.cpp
    src/coverage_geometry.h
    src/desktop_host.cpp
    src/desktop_host.h
    src/image_decoder.cpp
    src/image_decoder.h
    src/main.cpp
    src/panning.cpp
    src/panning.h
    src/renderer.cpp
    src/renderer.h
    src/window_coverage.cpp
    src/window_coverage.h
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
    d3dcompiler
    dwmapi
    dxgi
    ole32
    powrprof
    shell32
    windowscodecs
    wtsapi32
)

include(CTest)

if(BUILD_TESTING)
    add_executable(PanningTests
        src/command_line.cpp
        src/command_line.h
        src/coverage_geometry.cpp
        src/coverage_geometry.h
        src/panning.cpp
        src/panning.h
        tests/panning_tests.cpp
    )
    target_compile_features(PanningTests PRIVATE cxx_std_20)
    target_include_directories(PanningTests PRIVATE src)

    if(MSVC)
        target_compile_options(PanningTests PRIVATE
            /EHsc
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME PanningTests COMMAND PanningTests)
endif()
