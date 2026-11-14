# Minimal proxy DLL that loads an already-compiled ModdingAPI plugin.
#
# Builds d3dcompiler_47.dll (x64, MSVC only). The DLL exports the 29 forwarders
# of the real d3dcompiler_47.dll and, on DLL_PROCESS_ATTACH, spawns one worker
# thread that LoadLibrary()s moddingapi/mods/base_game/CPKLoader.dll and calls
# its InitializePlugin export. No game offsets, no pattern scans, no hooks.

cmake_minimum_required(VERSION 3.20)

project(nsc_hostdll VERSION 1.0.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "nsc_hostdll must be built with MSVC (x64). Use the GitHub Actions workflow on a non-Windows host.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "nsc_hostdll must be built as x64 (the game and the plugin are 64-bit).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Everything lands in <source>/dist so the artifact is easy to grab.
set(HOSTDLL_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} "${HOSTDLL_OUTPUT_DIR}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY${_cfg} "${HOSTDLL_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_cfg} "${HOSTDLL_OUTPUT_DIR}")
endforeach()

add_library(d3dcompiler_47_proxy SHARED
    src/hostdll.cpp
    src/log.cpp
    src/hostdll.h
    src/log.h
    src/forward.h
)

# Must be called d3dcompiler_47.dll: the game imports that name from its exe dir.
set_target_properties(d3dcompiler_47_proxy PROPERTIES
    OUTPUT_NAME "d3dcompiler_47"
    PREFIX ""
    SUFFIX ".dll"
    # Static CRT: the proxy has no runtime dependency of its own.
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(d3dcompiler_47_proxy PRIVATE src)

target_compile_definitions(d3dcompiler_47_proxy PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(d3dcompiler_47_proxy PRIVATE /W4 /permissive- /utf-8)
target_link_options(d3dcompiler_47_proxy PRIVATE /INCREMENTAL:NO)

message(STATUS "nsc_hostdll: output directory = ${HOSTDLL_OUTPUT_DIR}")
