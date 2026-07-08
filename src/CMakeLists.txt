cmake_minimum_required(VERSION 4.3)

add_executable(NodeSpireTD 
    main.cpp
    AppController.cpp
    SettingsManager.cpp
    PlatformRuntime.cpp
    LocalAchievementService.cpp
    LocalFileSaveSystem.cpp
    VulkanContext.cpp
    utility/VulkanTexture.cpp
    ImGuiLayer.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_compile_definitions(NodeSpireTD PRIVATE 
    VK_NO_PROTOTYPES 
    IMGUI_IMPL_VULKAN_USE_VOLK
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

target_include_directories(NodeSpireTD PRIVATE 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${vma_SOURCE_DIR}/include
)

target_link_libraries(NodeSpireTD PRIVATE 
    sfml-graphics
    sfml-audio
    sfml-window
    sfml-system
    spdlog::spdlog
    Vulkan::Vulkan
    glm::glm
    volk
    vk-bootstrap
    nlohmann_json::nlohmann_json
)
