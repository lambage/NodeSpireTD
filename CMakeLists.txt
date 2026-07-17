cmake_minimum_required(VERSION 4.3)
project(NodeSpireTD)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/sfml/sfml
    GIT_TAG        3.1.0
)

set(SFML_BUILD_AUDIO ON CACHE INTERNAL "")
set(SFML_BUILD_GRAPHICS ON CACHE INTERNAL "")

FetchContent_MakeAvailable(SFML)

if(WIN32)
    set(VULKAN_SDK_PATH "E:/VulkanSDK/1.4.350.0" CACHE PATH "Path to LunarG Vulkan SDK")
    if(EXISTS "${VULKAN_SDK_PATH}/Include/vulkan/vulkan.h")
        set(ENV{VULKAN_SDK} "${VULKAN_SDK_PATH}")
        set(Vulkan_INCLUDE_DIR "${VULKAN_SDK_PATH}/Include")
        set(Vulkan_LIBRARY "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib")
        list(PREPEND CMAKE_PREFIX_PATH "${VULKAN_SDK_PATH}")
    endif()
endif()

find_package(Vulkan REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8-docking
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(imgui)

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


