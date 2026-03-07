cmake_minimum_required(VERSION 3.25)

# ============================================================
# C++ standard — project requires C++23 (nested namespaces,
# inline variables, structured bindings, std::format, etc.)
# ============================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Product version (major.minor.patch) — shared with UI project
# ============================================================
set(VER_MAJOR 1)
set(VER_MINOR 0)
set(VER_PATCH 0)

# ============================================================
# Build number (4th digit) — read from shared file
# Incremented by build.ps1 once per build, NOT by CMake.
# ============================================================
set(BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/build_number.txt")
if(EXISTS "${BUILD_NUMBER_FILE}")
    file(READ "${BUILD_NUMBER_FILE}" VER_BUILD)
    string(STRIP "${VER_BUILD}" VER_BUILD)
else()
    set(VER_BUILD 0)
endif()
message(STATUS "Version: ${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}.${VER_BUILD}")

# ============================================================
# Git commit hash extraction — embedded into DLL at build time
# ============================================================
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git diff --quiet HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GIT_DIRTY_RESULT
    ERROR_QUIET
)
if(GIT_DIRTY_RESULT EQUAL 0)
    set(GIT_IS_DIRTY 0)
else()
    set(GIT_IS_DIRTY 1)
    string(APPEND GIT_COMMIT_SHORT "-dirty")
endif()

# Build timestamp (ISO 8601)
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S" UTC)

message(STATUS "Git: ${GIT_COMMIT_SHORT} (${GIT_COMMIT_HASH})")
message(STATUS "Build: ${CMAKE_BUILD_TYPE} @ ${BUILD_TIMESTAMP}")

# Generate BuildInfo.h from template
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BuildInfo.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h
    @ONLY
)

# ============================================================
# MinHook — inline hooking library (MIT, vendor clone)
# Built as a separate static C library to avoid C/C++ linkage issues.
# ============================================================
enable_language(C)

add_library(minhook STATIC
    ${CMAKE_SOURCE_DIR}/vendor/minhook/src/buffer.c
    ${CMAKE_SOURCE_DIR}/vendor/minhook/src/hook.c
    ${CMAKE_SOURCE_DIR}/vendor/minhook/src/trampoline.c
    ${CMAKE_SOURCE_DIR}/vendor/minhook/src/hde/hde64.c
)

target_include_directories(minhook PUBLIC
    ${CMAKE_SOURCE_DIR}/vendor/minhook/include
)

if(MSVC)
    set_property(TARGET minhook PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(minhook PRIVATE /W3 /utf-8)
endif()

# ============================================================
# ZoltDumper.dll — Main injected DLL
# Zoltraak — 栔爾朵列 — The attack magic that deconstructs Godot
# ============================================================

add_library(ZoltDumper SHARED
    src/dllmain.cpp
    src/Macht.cpp
    src/Zoltraak.cpp
    src/Solliter.cpp
    src/Frieren.cpp
    src/Ausetiese.cpp
    src/Flamme.cpp
    src/DiAgolze.cpp
    src/Serie.cpp
    src/Ubel.cpp
    src/Stark.cpp
    src/Fern.cpp
    src/Himmel.cpp
    src/Heiter.cpp
    src/Qual.cpp
    src/version.rc
)

target_include_directories(ZoltDumper PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated   # BuildInfo.h
    ${CMAKE_SOURCE_DIR}/vendor/nlohmann/include
)

target_compile_definitions(ZoltDumper PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# MSVC-specific flags
if(MSVC)
    # Static CRT linkage
    set_property(TARGET ZoltDumper PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    # Remove default /EHsc to avoid conflict with /EHa
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    target_compile_options(ZoltDumper PRIVATE
        /utf-8
        /W4
        /permissive-
        /EHa            # Enable SEH exceptions for __try/__except
        /arch:AVX2      # AVX2 instruction set optimization
        /favor:blend    # Blend optimization for modern CPUs
    )
endif()

target_link_libraries(ZoltDumper PRIVATE
    ws2_32
    Shlwapi
    Psapi
    Version     # GetFileVersionInfoW / VerQueryValueW for PE VERSIONINFO
    minhook
)

# Output name without lib prefix
set_target_properties(ZoltDumper PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ZoltDumper"
)

# ============================================================
# Proxy DLL target — builds as version.dll for direct game-folder injection
# Eisen — 艾乍 — The iron warrior's armored defense
# ============================================================
option(BUILD_PROXY_DLL "Build the proxy DLL (version.dll) target" OFF)

if(BUILD_PROXY_DLL)
    add_library(ZoltDumper_Proxy SHARED
        src/dllmain.cpp
        src/Eisen.cpp
        src/Zoltraak.cpp
        src/Solliter.cpp
        src/Frieren.cpp
        src/Ausetiese.cpp
        src/Flamme.cpp
        src/DiAgolze.cpp
        src/Serie.cpp
        src/Ubel.cpp
        src/Stark.cpp
        src/Fern.cpp
        src/Himmel.cpp
        src/Heiter.cpp
        src/Qual.cpp
        src/version.rc
    )

    target_include_directories(ZoltDumper_Proxy PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/vendor/nlohmann/include
    )

    target_compile_definitions(ZoltDumper_Proxy PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        ZOLT_PROXY_BUILD
    )

    if(MSVC)
        set_property(TARGET ZoltDumper_Proxy PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        target_compile_options(ZoltDumper_Proxy PRIVATE
            /utf-8 /W4 /permissive- /EHa /arch:AVX2 /favor:blend
        )
    endif()

    target_link_libraries(ZoltDumper_Proxy PRIVATE
        ws2_32 Shlwapi Psapi Version delayimp minhook
    )

    target_link_options(ZoltDumper_Proxy PRIVATE
        /DELAYLOAD:version.dll
        /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/ProxyVersion.def
    )

    set_target_properties(ZoltDumper_Proxy PROPERTIES
        PREFIX ""
        OUTPUT_NAME "version"
    )
endif()
