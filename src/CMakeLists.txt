cmake_minimum_required(VERSION 4.3)

add_executable(NodeSpireTD 
    main.cpp
    AppController.cpp
    SettingsManager.cpp
    PlatformRuntime.cpp
    LocalAchievementService.cpp
    LocalFileSaveSystem.cpp
    Scenes.cpp
    VulkanContext.cpp
    LuaStateBootstrap.cpp
    utility/VulkanTexture.cpp
    utility/TemplateAnimator.cpp
    utility/WorldAssetLoader.cpp
    utility/WorldRenderer.cpp
    ImGuiLayer.cpp

    scenes/GameScene.cpp
    scenes/LevelSelectionScene.cpp
    scenes/MainMenuScene.cpp
    scenes/OptionsScene.cpp
    scenes/PlayLevelScene.cpp
    scenes/SceneSharedState.cpp
    scenes/SplashScreen.cpp

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NodeSpireTD PRIVATE 
    glm::glm
    nlohmann_json::nlohmann_json
    fastgltf
    lua
    sfml-graphics
    sfml-audio
    sfml-window
    sfml-system
    spdlog::spdlog
    volk
    vk-bootstrap
    Vulkan::Vulkan
)

# ── Shader compilation ─────────────────────────────────────────────────────────
find_program(GLSLC_EXECUTABLE NAMES glslc
    HINTS "${VULKAN_SDK_PATH}/Bin" "$ENV{VULKAN_SDK}/Bin"
)

if(GLSLC_EXECUTABLE)
    set(SHADER_SRC "${CMAKE_SOURCE_DIR}/assets/shaders")
    add_custom_command(
        OUTPUT
            "${SHADER_SRC}/mesh.vert.spv"
            "${SHADER_SRC}/mesh.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.vert" -o "${SHADER_SRC}/mesh.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.frag" -o "${SHADER_SRC}/mesh.frag.spv"
        DEPENDS
            "${SHADER_SRC}/mesh.vert"
            "${SHADER_SRC}/mesh.frag"
        COMMENT "Compiling mesh shaders"
    )
    add_custom_target(MeshShaders DEPENDS
        "${SHADER_SRC}/mesh.vert.spv"
        "${SHADER_SRC}/mesh.frag.spv"
    )
    add_dependencies(NodeSpireTD MeshShaders)
else()
    message(WARNING "glslc not found — pre-compile mesh.vert/frag to .spv manually and place in assets/shaders/")
endif()
