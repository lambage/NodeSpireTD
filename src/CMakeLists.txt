cmake_minimum_required(VERSION 3.31.6)

set(NODESPIRE_PROTO_ROOT "${CMAKE_SOURCE_DIR}/proto")
set(NODESPIRE_MATCH_PROTO "${NODESPIRE_PROTO_ROOT}/nodespire/multiplayer/v1/match.proto")
set(NODESPIRE_PARTY_PROTO "${NODESPIRE_PROTO_ROOT}/nodespire/party/v1/party.proto")
set(NODESPIRE_PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

include("${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake")

add_library(NodeSpireMatchProtocol STATIC)

protobuf_generate(
    TARGET NodeSpireMatchProtocol
    LANGUAGE cpp
    PROTOS "${NODESPIRE_MATCH_PROTO}"
    IMPORT_DIRS "${NODESPIRE_PROTO_ROOT}"
    PROTOC_OUT_DIR "${NODESPIRE_PROTO_GENERATED_DIR}"
)

target_include_directories(NodeSpireMatchProtocol PUBLIC "${NODESPIRE_PROTO_GENERATED_DIR}")
target_link_libraries(NodeSpireMatchProtocol PUBLIC protobuf::libprotobuf Boost::asio)

if(WIN32)
    # Asio needs Winsock and a target Windows version for its socket backend.
    target_compile_definitions(NodeSpireMatchProtocol PUBLIC _WIN32_WINNT=0x0601)
    target_link_libraries(NodeSpireMatchProtocol PUBLIC ws2_32 mswsock)
endif()

# Kept as its own library/schema (nodespire.party.v1) separate from NodeSpireMatchProtocol so
# party/chat wire messages never share a generated C++ type with match-authoritative traffic.
add_library(NodeSpirePartyProtocol STATIC)

protobuf_generate(
    TARGET NodeSpirePartyProtocol
    LANGUAGE cpp
    PROTOS "${NODESPIRE_PARTY_PROTO}"
    IMPORT_DIRS "${NODESPIRE_PROTO_ROOT}"
    PROTOC_OUT_DIR "${NODESPIRE_PROTO_GENERATED_DIR}"
)

target_include_directories(NodeSpirePartyProtocol PUBLIC "${NODESPIRE_PROTO_GENERATED_DIR}")
target_link_libraries(NodeSpirePartyProtocol PUBLIC protobuf::libprotobuf)

function(nodespire_stage_runtime_dependencies targetName)
    if(WIN32)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${targetName}>
                    $<TARGET_RUNTIME_DLLS:${targetName}>
            COMMAND_EXPAND_LISTS
            COMMENT "Staging runtime dependencies for ${targetName}"
        )
    endif()
endfunction()

add_executable(NodeSpireTD-imgui 
    main.cpp
    AppController.cpp
    AudioEngine.cpp
    SettingsManager.cpp
    PlatformRuntime.cpp
    LocalAchievementService.cpp
    LocalFileSaveSystem.cpp
    Scenes.cpp
    VulkanContext.cpp
    LuaStateBootstrap.cpp
    multiplayer/ContentHash.cpp
    multiplayer/ChatCommandDispatcher.cpp
    multiplayer/FixedTickClock.cpp
    multiplayer/LanFramedConnection.cpp
    multiplayer/LanMatchClient.cpp
    multiplayer/LanMatchTransport.cpp
    multiplayer/LoopbackMatchSession.cpp
    multiplayer/LoopbackTransport.cpp
    multiplayer/LocalMatchHost.cpp
    multiplayer/MatchSimulation.cpp
    multiplayer/MatchProtocolAdapter.cpp
    multiplayer/MatchSnapshotBuilder.cpp
    multiplayer/MultiplayerSession.cpp
    multiplayer/PartyProtocolAdapter.cpp
    multiplayer/PlayerAccounts.cpp
    multiplayer/PlayerProfileStore.cpp
    utility/GameImageButton.cpp
    utility/GameButton.cpp
    utility/StaticGeometryBVH.cpp
    utility/TemplateAnimator.cpp
    utility/VulkanTexture.cpp
    utility/WorldAssetLoader.cpp
    utility/WorldRenderer.cpp
    ImGuiLayer.cpp

    scenes/EnemyLoadController.cpp
    scenes/GameScene.cpp
    scenes/LuaSceneStateBridge.cpp
    scenes/LevelDefinitionLoader.cpp
    scenes/LobbyScene.cpp
    scenes/MainMenuScene.cpp
    scenes/EnemySpawnFactory.cpp
    scenes/OptionsScene.cpp
    scenes/PlayLevelBootstrap.cpp
    scenes/PlayLevelCameraController.cpp
    scenes/PlayLevelCombatController.cpp
    scenes/PlayLevelFrameCoordinator.cpp
    scenes/PlayLevelPickingController.cpp
    scenes/PlayLevelRouteController.cpp
    scenes/PlayLevelTowerPlacementController.cpp
    scenes/TowerPlacementPreviewResolver.cpp
    scenes/TowerPlacementRules.cpp
    scenes/PlayLevelWaveController.cpp
    scenes/PlayLevelScene.cpp
    scenes/SceneSharedState.cpp
    scenes/SplashScreen.cpp
    scenes/TowerLoadController.cpp

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp

)

target_compile_definitions(NodeSpireTD-imgui PRIVATE 
    VK_NO_PROTOTYPES 
    IMGUI_IMPL_VULKAN_USE_VOLK
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

target_include_directories(NodeSpireTD-imgui PRIVATE 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${vma_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NodeSpireTD-imgui PRIVATE 
    glm::glm
    nlohmann_json::nlohmann_json
    NodeSpireMatchProtocol
    NodeSpirePartyProtocol
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

nodespire_stage_runtime_dependencies(NodeSpireTD-imgui)

set(NODESPIRE_INSTALL_TARGETS NodeSpireTD-imgui)

install(TARGETS ${NODESPIRE_INSTALL_TARGETS}
    RUNTIME_DEPENDENCY_SET NodeSpireRuntimeDependencies
    RUNTIME DESTINATION "."
)
install(RUNTIME_DEPENDENCY_SET NodeSpireRuntimeDependencies
    DESTINATION "."
    PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-win-.*"
    POST_EXCLUDE_REGEXES ".*[\\/]system32[\\/].*"
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION ".")

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
    add_dependencies(NodeSpireTD-imgui GenerateLuaMetadata)
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
            "${SHADER_SRC}/groundCircle.vert.spv"
            "${SHADER_SRC}/groundCircle.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.vert" -o "${SHADER_SRC}/mesh.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.frag" -o "${SHADER_SRC}/mesh.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/highlight.vert" -o "${SHADER_SRC}/highlight.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/highlight.frag" -o "${SHADER_SRC}/highlight.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/groundCircle.vert" -o "${SHADER_SRC}/groundCircle.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/groundCircle.frag" -o "${SHADER_SRC}/groundCircle.frag.spv"
        DEPENDS
            "${SHADER_SRC}/mesh.vert"
            "${SHADER_SRC}/mesh.frag"
            "${SHADER_SRC}/highlight.vert"
            "${SHADER_SRC}/highlight.frag"
            "${SHADER_SRC}/groundCircle.vert"
            "${SHADER_SRC}/groundCircle.frag"
        COMMENT "Compiling mesh shaders"
    )
    add_custom_target(MeshShaders DEPENDS
        "${SHADER_SRC}/mesh.vert.spv"
        "${SHADER_SRC}/mesh.frag.spv"
        "${SHADER_SRC}/highlight.vert.spv"
        "${SHADER_SRC}/highlight.frag.spv"
        "${SHADER_SRC}/groundCircle.vert.spv"
        "${SHADER_SRC}/groundCircle.frag.spv"
    )
    add_dependencies(NodeSpireTD-imgui MeshShaders)

else()
    message(WARNING "glslc not found — pre-compile mesh.vert/frag to .spv manually and place in assets/shaders/")
endif()

# ── NodeSpireTD (RmlUi bring-up) ──────────────────────────────────────────────
# SDL3+Vulkan RmlUi app. The RmlUi SDL_VK sample backend is forked (not used via
# add_subdirectory("Backends")/rmlui_backend_SDL_VK) into src/rmlui_backend/ so
# RenderInterface_VK::LoadTexture can be modified to decode more than 24/32bit
# uncompressed TGA (see src/rmlui_backend/RmlUi_Renderer_VK.cpp). It is still
# self-contained (owns its own VkInstance/VkDevice) and not yet reconciled with
# VulkanContext. This is the seed for the eventual RmlUi replacement of
# NodeSpireTD-imgui.

add_executable(NodeSpireTD
    RmlUiApp.cpp
    RmlUiFontLoader.cpp
    rmlui_backend/RmlUi_Platform_SDL.cpp
    rmlui_backend/RmlUi_Renderer_VK.cpp
    rmlui_backend/RmlUi_Backend_SDL_VK.cpp
    rmlui/SceneManager.cpp
    rmlui/scenes/SplashScene.cpp
    rmlui/scenes/MainMenuScene.cpp
    rmlui/scenes/LobbyScene.cpp
    rmlui/scenes/OptionsScene.cpp
)

target_compile_definitions(NodeSpireTD PRIVATE
    RMLUI_SDL_VERSION_MAJOR=3
)

target_include_directories(NodeSpireTD PRIVATE
    "${rmlui_SOURCE_DIR}/Backends"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/rmlui_backend"
    "${stb_SOURCE_DIR}"
)

target_link_libraries(NodeSpireTD PRIVATE
    rmlui
    SDL3::SDL3
    Vulkan::Vulkan
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

nodespire_stage_runtime_dependencies(NodeSpireTD)

