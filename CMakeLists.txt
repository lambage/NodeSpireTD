cmake_minimum_required(VERSION 3.31.6)
project(NodeSpireTD)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation directory" FORCE)
endif()

include(CTest)
include(FetchContent)

FetchContent_Declare(
    Freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-14-3
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(Freetype)

# FreeType's own CMakeLists.txt only exposes the "Freetype::Freetype" name via
# EXPORT_NAME after install(EXPORT ...); consumed in-tree via FetchContent (no
# install step) the real target is "freetype-interface". RmlUi's
# find_package("Freetype") + report_dependency_found_or_error() only checks
# for the "Freetype::Freetype" target's existence, so alias it ourselves
# (same pattern as the Lua::Lua alias below).
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype-interface)
endif()

# Pinned to 3.4.16 (not 3.2.20): SDL_mixer 3.2.4 requires the SDL_ALIGNED()
# macro and newer SDL_*AudioStream() signatures that 3.2.20's SDL_begin_code.h
# doesn't have yet, which fails to compile against the older SDL3.
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(SDL3)

# SDL3_mixer provides AudioEngine playback for music.mp3 and sfx.ogg.
# Vendored codec libs (ON by default) cover both formats we ship
# (MP3, Ogg Vorbis) without needing system decoder packages.
FetchContent_Declare(
    SDL3_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG        release-3.2.4
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(SDL3_mixer)

# Prefer the VULKAN_SDK environment variable (set automatically by the LunarG
# Vulkan SDK installer). Fall back to VULKAN_SDK_PATH for anyone who wants to
# override it explicitly, e.g. via -DVULKAN_SDK_PATH=... or a local cache entry.
if(DEFINED ENV{VULKAN_SDK} AND NOT VULKAN_SDK_PATH)
    set(VULKAN_SDK_PATH "$ENV{VULKAN_SDK}" CACHE PATH "Path to LunarG Vulkan SDK")
else()
    set(VULKAN_SDK_PATH "" CACHE PATH "Path to LunarG Vulkan SDK")
endif()

if(VULKAN_SDK_PATH AND EXISTS "${VULKAN_SDK_PATH}/Include/vulkan/vulkan.h")
    set(ENV{VULKAN_SDK} "${VULKAN_SDK_PATH}")
    set(Vulkan_INCLUDE_DIR "${VULKAN_SDK_PATH}/Include")
    set(Vulkan_LIBRARY "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib")
    list(PREPEND CMAKE_PREFIX_PATH "${VULKAN_SDK_PATH}")
endif()

find_package(Vulkan REQUIRED)

# Lua is fetched here (before RmlUi) so the vendored static "lua" target exists
# in time to satisfy RmlUi's Lua bindings dependency lookup below.
FetchContent_Declare(
    luasrc
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.5.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(luasrc)

file(GLOB LUA_SOURCES "${luasrc_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES 
    "${luasrc_SOURCE_DIR}/lua.c" 
    "${luasrc_SOURCE_DIR}/luac.c"
    "${luasrc_SOURCE_DIR}/onelua.c"
)

# Compile Lua as a static library
add_library(lua STATIC ${LUA_SOURCES})

# Target configuration
target_include_directories(lua PUBLIC "${luasrc_SOURCE_DIR}")

# Required for Linux/macOS systems to link system math and dynamic loading utilities
if(UNIX)
    target_link_libraries(lua PRIVATE m dl)
endif()

# Satisfies RmlUi's "if(NOT TARGET Lua::Lua)" check in CMake/Dependencies.cmake,
# so its Lua bindings link against our vendored Lua instead of find_package(Lua).
# Must be a real (IMPORTED) target, not an ALIAS: RmlUi in turn does
# `add_library(RmlUi::External::Lua ALIAS Lua::Lua)`, and CMake disallows an
# ALIAS target pointing at another ALIAS target.
add_library(Lua::Lua INTERFACE IMPORTED GLOBAL)
target_link_libraries(Lua::Lua INTERFACE lua)

# RmlUi provides the application UI.
set(RMLUI_LUA_BINDINGS ON CACHE BOOL "" FORCE)
# NodeSpireTD installs one application-level runtime dependency set below. Letting RmlUi install
# its own set as well scans rmlui.dll outside the staged application directory and cannot resolve
# our separately fetched freetype.dll on Windows.
set(RMLUI_INSTALL_RUNTIME_DEPENDENCIES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.3
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(rmlui)

# The vendored FreeType compiles its autohint
# code against HarfBuzz whenever HarfBuzz is available, but doesn't always add
# the static link dependency itself (it assumes dynamic loading in that case).
# Link it explicitly so rmlui_core resolves the hb_* symbols at link time.
if(TARGET HarfBuzz::HarfBuzz)
    target_link_libraries(rmlui_core PRIVATE HarfBuzz::HarfBuzz)
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.2.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(fmt)

# Header-only; no CMakeLists.txt of its own, so FetchContent_MakeAvailable just
# populates the source and skips add_subdirectory. Used by our forked
# RenderInterface_VK::LoadTexture (src/rmlui_backend/RmlUi_Renderer_VK.cpp) to
# decode PNG/JPEG/BMP/etc., replacing RmlUi's original TGA-only sample parser.
# stb doesn't tag releases, so this floats on master rather than a pinned
# version like the other dependencies above.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v30.2
    GIT_SHALLOW    ON
    GIT_SUBMODULES_RECURSE TRUE
)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Never resolve Abseil from build/install: that directory may contain artifacts from another
# configuration, and mixing a Debug Abseil DLL with Release Protobuf corrupts the CRT heap before main().
set(protobuf_FORCE_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
# Static Protobuf builds force Abseil to /MT[d] upstream, which conflicts with
# NodeSpireTD's /MD[d] runtime. Shared Protobuf keeps the dependency graph aligned.
set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(protobuf)

FetchContent_Declare(
    Boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG        boost-1.87.0
    GIT_SHALLOW    ON
    GIT_SUBMODULES_RECURSE TRUE
)

# Only Asio (and its System dependency) are needed for the LAN match transport.
set(BOOST_INCLUDE_LIBRARIES asio system)
set(BOOST_ENABLE_CMAKE ON)

FetchContent_MakeAvailable(Boost)

if(BUILD_TESTING)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    ON
    )

    set(gtest_build_tests OFF CACHE BOOL "" FORCE)
    set(gtest_build_samples OFF CACHE BOOL "" FORCE)
    set(gtest_build_gmock ON CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
endif()

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    ON
)

# GLM is consumed as headers only; building it as a real DLL on Windows can
# produce no import library, which then breaks downstream link steps.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.4.350.1
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(volk)

FetchContent_Declare(
    vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.4.354
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(vk_bootstrap)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    ON
)   

FetchContent_MakeAvailable(vma)

FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        v0.9.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(fastgltf)

add_subdirectory(src)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    include(CPack)

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "NodeSpireTD")
    set(CPACK_PACKAGE_VENDOR "NodeSpireTD")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NodeSpireTD desktop game")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "NodeSpireTD")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

    if(WIN32)
        set(CPACK_PACKAGE_FILE_NAME "NodeSpireTD-Windows-${CMAKE_BUILD_TYPE}")
    else()
        set(CPACK_PACKAGE_FILE_NAME "NodeSpireTD-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}")
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

