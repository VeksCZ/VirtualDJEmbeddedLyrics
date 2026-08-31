cmake_minimum_required(VERSION 3.24)
project(VirtualDJEmbeddedLyrics VERSION 0.4.0 LANGUAGES CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lyrics_core STATIC
    src/AsyncLyricsLoader.cpp
    src/Diagnostics.cpp
    src/Id3Lyrics.cpp
    src/LrcParser.cpp
    src/LyricsLoader.cpp
)
target_include_directories(lyrics_core PUBLIC src)
if(WIN32)
    target_link_libraries(lyrics_core PUBLIC shell32)
endif()

option(BUILD_TESTING "Build parser tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(lyrics_tests tests/LyricsTests.cpp)
    target_link_libraries(lyrics_tests PRIVATE lyrics_core)
    add_test(NAME lyrics_tests COMMAND lyrics_tests)
    if(WIN32)
        add_executable(renderer_tests
            tests/RendererTests.cpp
            src/BlackoutRenderer.cpp
            src/VideoRenderer.cpp
        )
        target_include_directories(renderer_tests PRIVATE src)
        target_compile_definitions(renderer_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(renderer_tests PRIVATE /W4 /permissive- /EHsc)
        endif()
        target_link_libraries(renderer_tests PRIVATE d3d11 d3dcompiler)
        add_test(NAME renderer_tests COMMAND renderer_tests)
    endif()
endif()

if(WIN32)
    set(VDJ_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VirtualDJ8_SDK_20211003" CACHE PATH "VirtualDJ SDK headers")
    if(EXISTS "${VDJ_SDK_DIR}/vdjPlugin8.h" AND EXISTS "${VDJ_SDK_DIR}/vdjVideo8.h")
        function(add_vdj_lyrics_plugin target output_name is_master)
            add_library(${target} SHARED
                src/AdvancedDialog.cpp
                src/Plugin.cpp
                src/BlackoutRenderer.cpp
                src/TextTexture.cpp
                src/VideoRenderer.cpp
                src/PluginExports.def
            )
            target_include_directories(${target} PRIVATE src)
            target_include_directories(${target} SYSTEM PRIVATE "${VDJ_SDK_DIR}")
            target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN NODLLEXPORT)
            if(${is_master})
                target_compile_definitions(${target} PRIVATE EMBEDDED_LYRICS_MASTER=1)
            endif()
            if(MSVC)
                target_compile_options(${target} PRIVATE /W4 /external:W0 /permissive- /EHsc)
                set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            endif()
            target_link_libraries(${target} PRIVATE lyrics_core d3d11 d3dcompiler gdi32)
            set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${output_name}")
        endfunction()

        add_vdj_lyrics_plugin(EmbeddedLyricsDeck "LRC Deck" FALSE)
        add_vdj_lyrics_plugin(EmbeddedLyricsMaster "LRC Master" TRUE)

        add_library(Blackout SHARED
            src/BlackoutRenderer.cpp
            src/BlackoutPlugin.cpp
            src/PluginExports.def
        )
        target_include_directories(Blackout PRIVATE src)
        target_include_directories(Blackout SYSTEM PRIVATE "${VDJ_SDK_DIR}")
        target_compile_definitions(Blackout PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN NODLLEXPORT)
        if(MSVC)
            target_compile_options(Blackout PRIVATE /W4 /external:W0 /permissive- /EHsc)
            set_property(TARGET Blackout PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
        target_link_libraries(Blackout PRIVATE lyrics_core d3d11 d3dcompiler)
        set_target_properties(Blackout PROPERTIES PREFIX "" OUTPUT_NAME "LRC BlackOut")
    else()
        message(FATAL_ERROR "VirtualDJ SDK headers were not found")
    endif()
endif()
