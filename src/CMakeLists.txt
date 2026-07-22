cmake_minimum_required(VERSION 3.31.6)

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
    scenes/LobbyScene.cpp
    scenes/MainMenuScene.cpp
    scenes/OptionsScene.cpp
    scenes/PlayLevelCameraController.cpp
    scenes/PlayLevelCombatController.cpp
    scenes/PlayLevelPickingController.cpp
    scenes/PlayLevelRouteController.cpp
    scenes/PlayLevelTowerPlacementController.cpp
    scenes/PlayLevelWaveController.cpp
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

# ── Lua metadata generation for LuaLS ─────────────────────────────────────────
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    set(LUA_META_OUTPUT "${CMAKE_SOURCE_DIR}/assets/lua/meta/engine_globals.lua")

    file(GLOB_RECURSE LUA_BINDING_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
    )

    add_custom_command(
        OUTPUT "${LUA_META_OUTPUT}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/gen_lua_metadata.py"
            --source-root "${CMAKE_SOURCE_DIR}/src"
            --output "${LUA_META_OUTPUT}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/gen_lua_metadata.py"
            ${LUA_BINDING_SOURCES}
        COMMENT "Generating Lua metadata from C++ Lua bindings"
        VERBATIM
    )

    add_custom_target(GenerateLuaMetadata DEPENDS "${LUA_META_OUTPUT}")
    add_dependencies(NodeSpireTD GenerateLuaMetadata)
else()
    message(WARNING "Python3 interpreter not found; Lua metadata will not auto-generate.")
endif()

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
            "${SHADER_SRC}/highlight.vert.spv"
            "${SHADER_SRC}/highlight.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.vert" -o "${SHADER_SRC}/mesh.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.frag" -o "${SHADER_SRC}/mesh.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/highlight.vert" -o "${SHADER_SRC}/highlight.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/highlight.frag" -o "${SHADER_SRC}/highlight.frag.spv"
        DEPENDS
            "${SHADER_SRC}/mesh.vert"
            "${SHADER_SRC}/mesh.frag"
            "${SHADER_SRC}/highlight.vert"
            "${SHADER_SRC}/highlight.frag"
        COMMENT "Compiling mesh shaders"
    )
    add_custom_target(MeshShaders DEPENDS
        "${SHADER_SRC}/mesh.vert.spv"
        "${SHADER_SRC}/mesh.frag.spv"
        "${SHADER_SRC}/highlight.vert.spv"
        "${SHADER_SRC}/highlight.frag.spv"
    )
    add_dependencies(NodeSpireTD MeshShaders)
else()
    message(WARNING "glslc not found — pre-compile mesh.vert/frag to .spv manually and place in assets/shaders/")
endif()
