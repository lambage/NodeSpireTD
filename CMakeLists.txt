cmake_minimum_required(VERSION 3.31.6)
project(NodeSpireTD)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation directory" FORCE)
endif()

include(CTest)

include(FetchContent)
FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/sfml/sfml
    GIT_TAG        3.1.0
)

set(SFML_BUILD_AUDIO ON CACHE INTERNAL "")
set(SFML_BUILD_GRAPHICS ON CACHE INTERNAL "")

FetchContent_MakeAvailable(SFML)

# SDL3 is the windowing/input/audio replacement for SFML. Both are fetched
# side-by-side during the migration; SFML is removed once every call site
# (windowing, Vulkan surface creation, audio) has moved over.
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.20
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(SDL3)

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

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8-docking
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(imgui)

# RmlUi is the UI replacement for Dear ImGui.
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.3
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(rmlui)

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
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v30.2
    GIT_SHALLOW    ON
    GIT_SUBMODULES_RECURSE TRUE
)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
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

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()


