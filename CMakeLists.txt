cmake_minimum_required(VERSION 3.20)
project(NodeSpireTD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0135 NEW)

include(FetchContent)

# ============================================================
# Vulkan – headers + loader (system install)
# ============================================================

# Ensure you have the official LunarG Vulkan SDK installed on your machine.
# On Windows, provide an explicit SDK path fallback when env vars are missing.
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

# ============================================================
# GLFW – windowing/input, no OpenGL context
# ============================================================
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11      ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ============================================================
# GLM – math
# ============================================================
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ============================================================
# Volk – Vulkan meta-loader (no direct link to libvulkan)
# ============================================================
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.4.350.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(volk)

# ============================================================
# vk-bootstrap – Vulkan init boilerplate removal
# ============================================================
FetchContent_Declare(
    vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.3.302
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vk_bootstrap)

# ============================================================
# VulkanMemoryAllocator (VMA)
# ============================================================
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# ============================================================
# EnTT – Entity Component System
# ============================================================
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)

# ============================================================
# nlohmann/json – JSON serialization
# ============================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ============================================================
# Dear ImGui – UI (compiled manually; no upstream CMake targets)
# ============================================================
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# IMGUI_IMPL_VULKAN_NO_PROTOTYPES: ImGui Vulkan backend will not assume
# Vulkan symbols are linked – we supply them through volk at runtime.
target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
target_link_libraries(imgui_lib PUBLIC glfw volk)

# ============================================================
# ImNodes – node-graph editor widget
# Fetched and built manually so we can supply our imgui target.
# ============================================================
FetchContent_Declare(
    imnodes_src
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG        v0.5
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imnodes_src)
if(NOT imnodes_src_POPULATED)
    FetchContent_Populate(imnodes_src)
    add_library(imnodes STATIC
        ${imnodes_src_SOURCE_DIR}/imnodes.cpp
    )
    target_include_directories(imnodes PUBLIC ${imnodes_src_SOURCE_DIR})
    # IMGUI_DEFINE_MATH_OPERATORS is required for imnodes v0.5 to compile
    # against ImGui >= 1.89 where ImVec2 arithmetic is gated behind this macro.
    target_compile_definitions(imnodes PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    target_link_libraries(imnodes PUBLIC imgui_lib)
endif()

# ============================================================
# NodeSpireTD – main executable
# ============================================================
file(GLOB_RECURSE NODESPIRE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(NodeSpireTD ${NODESPIRE_SOURCES})

target_include_directories(NodeSpireTD PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(NodeSpireTD PRIVATE
    VK_NO_PROTOTYPES
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    IMGUI_DEFINE_MATH_OPERATORS
)

target_link_libraries(NodeSpireTD PRIVATE
    glfw
    glm::glm
    volk
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    EnTT::EnTT
    nlohmann_json::nlohmann_json
    imgui_lib
    imnodes
)

# Copy data directory next to the executable after build
add_custom_command(TARGET NodeSpireTD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:NodeSpireTD>/data"
    COMMENT "Copying data directory to build output"
)
