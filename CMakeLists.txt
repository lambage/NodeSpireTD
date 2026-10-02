cmake_minimum_required(VERSION 3.31.6)
project(NodeSpireTD)

option(NODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING
    "Enable host-side gameplay tuning hooks intended for local dev/testing via MCP"
    OFF)

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

# Third-party sources and LambUI are checked out next to this repo by the repo
# tool (nodespiretd.xml manifest) rather than downloaded at configure time.
set(NODESPIRE_DEPS_DIR "${PROJECT_SOURCE_DIR}/../third_party" CACHE PATH
    "Directory holding NodeSpireTD's third-party source checkouts")
set(NODESPIRE_LAMBUI_DIR "${PROJECT_SOURCE_DIR}/../LambUI" CACHE PATH
    "LambUI source checkout")

# nodespire_add_dependency(<dir> [NO_ADD_SUBDIRECTORY])
# Sets <lowercased dir>_SOURCE_DIR like FetchContent does and add_subdirectory()s
# the checkout.
function(nodespire_add_dependency name)
    cmake_parse_arguments(arg "NO_ADD_SUBDIRECTORY" "" "" ${ARGN})
    set(src "${NODESPIRE_DEPS_DIR}/${name}")
    if(NOT IS_DIRECTORY "${src}")
        message(FATAL_ERROR
            "Dependency '${name}' not found at ${src}. "
            "Run 'repo sync' with the nodespiretd.xml manifest, or set NODESPIRE_DEPS_DIR.")
    endif()
    string(TOLOWER "${name}" lname)
    set(${lname}_SOURCE_DIR "${src}" PARENT_SCOPE)
    if(NOT arg_NO_ADD_SUBDIRECTORY)
        add_subdirectory("${src}" "${CMAKE_BINARY_DIR}/_deps/${lname}-build")
    endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA QUIET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(NODESPIRE_PIPEWIRE QUIET libpipewire-0.3>=0.3.44)
        pkg_check_modules(NODESPIRE_PULSEAUDIO QUIET libpulse>=0.9.15)
    endif()
    if(NOT ALSA_FOUND AND NOT NODESPIRE_PIPEWIRE_FOUND AND NOT NODESPIRE_PULSEAUDIO_FOUND)
        message(FATAL_ERROR
            "SDL3 requires a Linux audio development package. Install libpipewire-0.3-dev, "
            "libpulse-dev, or libasound2-dev (package names shown for Debian/Ubuntu), then reconfigure.")
    endif()
endif()

# Manifest pins release-3.4.16 (not 3.2.20): SDL_mixer 3.2.4 requires the
# SDL_ALIGNED() macro and newer SDL_*AudioStream() signatures that 3.2.20's
# SDL_begin_code.h doesn't have yet, which fails to compile against the older SDL3.
nodespire_add_dependency(SDL)

# SDL3_mixer provides AudioEngine playback for music.mp3 and sfx.ogg.
# Vendored codec libs (ON by default) cover both formats we ship
# (MP3, Ogg Vorbis) without needing system decoder packages; they are git
# submodules, hence sync-s="true" on this project in the manifest.
nodespire_add_dependency(SDL_mixer)

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

nodespire_add_dependency(lua NO_ADD_SUBDIRECTORY)

file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES 
    "${lua_SOURCE_DIR}/lua.c" 
    "${lua_SOURCE_DIR}/luac.c"
    "${lua_SOURCE_DIR}/onelua.c"
)

# Compile Lua as a static library
add_library(lua STATIC ${LUA_SOURCES})

# Target configuration
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}")

# Required for Linux/macOS systems to link system math and dynamic loading utilities
if(UNIX)
    target_link_libraries(lua PRIVATE m dl)
endif()

# Satisfies LambUI's lua/CMakeLists.txt "if(NOT TARGET Lua::Library)" check,
# so its Lua bindings link against our vendored Lua instead of building
# their own. Must be a real (IMPORTED) target, not an ALIAS
# pointing at another ALIAS target (same reasoning as the Lua::Lua alias above).
add_library(Lua::Lua INTERFACE IMPORTED GLOBAL)
target_link_libraries(Lua::Lua INTERFACE lua)
add_library(Lua::Library INTERFACE IMPORTED GLOBAL)
target_link_libraries(Lua::Library INTERFACE lua)

nodespire_add_dependency(fmt)

# Added before spdlog (and forced to use the fmt added above) so every
# translation unit -- including spdlog's own compiled library -- agrees on a
# single fmt::v11 ABI. Without this, spdlog defaults to its own bundled fmt
# copy (a different version/inline-namespace tag), and any TU that also
# transitively includes the external fmt headers before spdlog.h (e.g.
# LambUiApp.cpp via LambUI/UIManager.h) silently resolves spdlog's logging
# templates to the external fmt::v11 types instead, producing LNK2019s
# against the already-compiled spdlog DLL's fmt::v12 symbols.
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

nodespire_add_dependency(spdlog)

nodespire_add_dependency(json)

# Header-only; no CMakeLists.txt of its own, so it is not add_subdirectory()'d.
# Used by LambUI_Renderer_VK's texture loader
# (src/lambui_backend/LambUI_Renderer_VK.cpp) to decode
# PNG/JPEG/BMP/etc. for UITextureWidget images; also reused by LambUI's own
# FontAtlas.cpp for stb_truetype SDF baking (see LambUI/src/CMakeLists.txt).
# stb doesn't tag releases, so the manifest floats it on master.
nodespire_add_dependency(stb NO_ADD_SUBDIRECTORY)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Never resolve Abseil from build/install: that directory may contain artifacts from another
# configuration, and mixing a Debug Abseil DLL with Release Protobuf corrupts the CRT heap before main().
set(protobuf_FORCE_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
# Protobuf can only FetchContent Abseil itself; redirect that to our checkout.
nodespire_add_dependency(abseil-cpp NO_ADD_SUBDIRECTORY)
set(FETCHCONTENT_SOURCE_DIR_ABSL "${abseil-cpp_SOURCE_DIR}" CACHE PATH "" FORCE)
# Static Protobuf builds force Abseil to /MT[d] upstream, which conflicts with
# NodeSpireTD's /MD[d] runtime. Shared Protobuf keeps the dependency graph aligned.
set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)

nodespire_add_dependency(protobuf)

# Only Asio (and its System dependency) are needed for the LAN match transport.
set(BOOST_INCLUDE_LIBRARIES asio system)
set(BOOST_ENABLE_CMAKE ON)

nodespire_add_dependency(boost)

# LambUI provides the application UI (replaces RmlUi). Added after fmt and Lua
# above so it reuses the fmt::fmt and Lua::Library targets instead of adding
# its own, and shares NODESPIRE_DEPS_DIR for everything else.
# Boost is a soft, bring-your-own dependency for LambUI (LAMBUI_USE_BOOST):
# left OFF so its public optional/clamp usage is std::optional + an internal
# helper instead, needing no extra Boost components beyond asio/system above.
set(LAMBUI_USE_BOOST OFF CACHE BOOL "" FORCE)
set(LAMBUI_BUILD_LUA_BINDINGS ON CACHE BOOL "" FORCE)
set(LAMBUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LAMBUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LAMBUI_INSTALL OFF CACHE BOOL "" FORCE)
set(LAMBUI_DEPS_DIR "${NODESPIRE_DEPS_DIR}" CACHE PATH "" FORCE)
if(NOT EXISTS "${NODESPIRE_LAMBUI_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LambUI not found at ${NODESPIRE_LAMBUI_DIR}. "
        "Run 'repo sync' with the nodespiretd.xml manifest, or set NODESPIRE_LAMBUI_DIR.")
endif()
add_subdirectory("${NODESPIRE_LAMBUI_DIR}" "${CMAKE_BINARY_DIR}/_deps/lambui-build")

if(BUILD_TESTING)
    set(gtest_build_tests OFF CACHE BOOL "" FORCE)
    set(gtest_build_samples OFF CACHE BOOL "" FORCE)
    set(gtest_build_gmock ON CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    nodespire_add_dependency(googletest)
endif()

# GLM is consumed as headers only; building it as a real DLL on Windows can
# produce no import library, which then breaks downstream link steps.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
nodespire_add_dependency(glm)

nodespire_add_dependency(volk)

nodespire_add_dependency(vk-bootstrap)

nodespire_add_dependency(VulkanMemoryAllocator)

# fastgltf downloads simdjson into its own source tree unless simdjson::simdjson
# exists. IMPORTED so fastgltf's install(EXPORT) doesn't need it in an export set.
nodespire_add_dependency(simdjson NO_ADD_SUBDIRECTORY)
add_library(nodespire_simdjson STATIC "${simdjson_SOURCE_DIR}/singleheader/simdjson.cpp")
target_include_directories(nodespire_simdjson PUBLIC "${simdjson_SOURCE_DIR}/singleheader")
add_library(simdjson::simdjson INTERFACE IMPORTED GLOBAL)
target_link_libraries(simdjson::simdjson INTERFACE nodespire_simdjson)

nodespire_add_dependency(fastgltf)

add_subdirectory(src)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
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

    include(CPack)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
