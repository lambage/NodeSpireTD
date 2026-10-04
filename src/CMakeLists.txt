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
        add_custom_target(${targetName}RuntimeDependencies
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${targetName}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${targetName}> $<TARGET_FILE_DIR:${targetName}>
            DEPENDS $<TARGET_RUNTIME_DLLS:${targetName}>
            COMMAND_EXPAND_LISTS
            COMMENT "Staging runtime dependencies for ${targetName}"
            VERBATIM
        )
        add_dependencies(${targetName} ${targetName}RuntimeDependencies)
    endif()
endfunction()

# SDL-native game Vulkan ownership shared by world and LambUI rendering.
add_library(NodeSpireVulkanContext STATIC
    VulkanContext.cpp
)

target_compile_definitions(NodeSpireVulkanContext
    PUBLIC VK_NO_PROTOTYPES
    PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

target_include_directories(NodeSpireVulkanContext PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(NodeSpireVulkanContext PUBLIC
    SDL3::SDL3
    GPUOpen::VulkanMemoryAllocator
    volk::volk
    vk-bootstrap::vk-bootstrap
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
            "${SHADER_SRC}/highlight.vert.spv"
            "${SHADER_SRC}/highlight.frag.spv"
            "${SHADER_SRC}/groundCircle.vert.spv"
            "${SHADER_SRC}/groundCircle.frag.spv"
            "${SHADER_SRC}/previewPanel.vert.spv"
            "${SHADER_SRC}/previewPanel.frag.spv"
            "${SHADER_SRC}/ui.vert.spv"
            "${SHADER_SRC}/ui.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.vert" -o "${SHADER_SRC}/mesh.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/mesh.frag" -o "${SHADER_SRC}/mesh.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/highlight.vert" -o "${SHADER_SRC}/highlight.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/highlight.frag" -o "${SHADER_SRC}/highlight.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/groundCircle.vert" -o "${SHADER_SRC}/groundCircle.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/groundCircle.frag" -o "${SHADER_SRC}/groundCircle.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/previewPanel.vert" -o "${SHADER_SRC}/previewPanel.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/previewPanel.frag" -o "${SHADER_SRC}/previewPanel.frag.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/ui.vert" -o "${SHADER_SRC}/ui.vert.spv"
        COMMAND ${GLSLC_EXECUTABLE} "${SHADER_SRC}/ui.frag" -o "${SHADER_SRC}/ui.frag.spv"
        DEPENDS
            "${SHADER_SRC}/mesh.vert"
            "${SHADER_SRC}/mesh.frag"
            "${SHADER_SRC}/highlight.vert"
            "${SHADER_SRC}/highlight.frag"
            "${SHADER_SRC}/groundCircle.vert"
            "${SHADER_SRC}/groundCircle.frag"
            "${SHADER_SRC}/previewPanel.vert"
            "${SHADER_SRC}/previewPanel.frag"
            "${SHADER_SRC}/ui.vert"
            "${SHADER_SRC}/ui.frag"
        COMMENT "Compiling mesh shaders"
    )
    add_custom_target(MeshShaders DEPENDS
        "${SHADER_SRC}/mesh.vert.spv"
        "${SHADER_SRC}/mesh.frag.spv"
        "${SHADER_SRC}/highlight.vert.spv"
        "${SHADER_SRC}/highlight.frag.spv"
        "${SHADER_SRC}/groundCircle.vert.spv"
        "${SHADER_SRC}/groundCircle.frag.spv"
        "${SHADER_SRC}/previewPanel.vert.spv"
        "${SHADER_SRC}/previewPanel.frag.spv"
        "${SHADER_SRC}/ui.vert.spv"
        "${SHADER_SRC}/ui.frag.spv"
    )
    # NodeSpireTD depends on MeshShaders below because its native PlayLevel scene
    # renders WorldRenderer before the LambUI HUD in the shared command buffer.

else()
    message(WARNING "glslc not found — pre-compile mesh.vert/frag to .spv manually and place in assets/shaders/")
endif()

# ── NodeSpireTD ────────────────────────────────────────────────────────────────
# SDL3+Vulkan LambUI app. The renderer is reconciled with VulkanContext's
# device, allocator, swapchain formats, and active command buffer.

add_executable(NodeSpireTD
    LambUiApp.cpp
    LambUiFontLoader.cpp
    SettingsManager.cpp
    GameplayTuningManager.cpp
    AudioEngine.cpp
    multiplayer/ChatCommandDispatcher.cpp
    multiplayer/FixedTickClock.cpp
    multiplayer/LanFramedConnection.cpp
    multiplayer/LanMatchClient.cpp
    multiplayer/LanMatchTransport.cpp
    multiplayer/LocalMatchHost.cpp
    multiplayer/MatchProtocolAdapter.cpp
    multiplayer/MatchSimulation.cpp
    multiplayer/MatchSnapshotBuilder.cpp
    multiplayer/MultiplayerSession.cpp
    multiplayer/PartyProtocolAdapter.cpp
    multiplayer/PlayerAccounts.cpp
    multiplayer/PlayerProfileStore.cpp
    lambui/SceneManager.cpp
    lambui/AppControl.cpp
    lambui/LuaUiScene.cpp
    lambui/LuaAudioBindings.cpp
    lambui/LuaSceneBindings.cpp
    lambui/playlevel/PlayLevelUiContract.cpp
    lambui/scenes/PlayLevelScene.cpp
    lambui/scenes/SplashScene.cpp
    lambui/scenes/MainMenuScene.cpp
    lambui/scenes/LobbyScene.cpp
    lambui/scenes/OptionsScene.cpp
    lambui_backend/VulkanUiRenderer.cpp
    lambui_backend/SdlUiInputBridge.cpp
    scenes/EnemyLoadController.cpp
    scenes/EnemySpawnFactory.cpp
    scenes/PlayLevelCombatController.cpp
    scenes/PlayLevelWaveController.cpp
    scenes/TowerPlacementPreviewResolver.cpp
    scenes/TowerLoadController.cpp
    scenes/TowerPlacementRules.cpp
    utility/StaticGeometryBVH.cpp
    utility/TemplateAnimator.cpp
    utility/WorldAssetLoader.cpp
    utility/WorldRenderer.cpp
)

target_compile_definitions(NodeSpireTD PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    NODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING=$<BOOL:${NODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING}>
)

target_include_directories(NodeSpireTD PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${stb_SOURCE_DIR}"
)

target_link_libraries(NodeSpireTD PRIVATE
    LambUI::lambui
    LambUI::lua
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    fmt::fmt
    glm::glm
    fastgltf
    NodeSpireMatchProtocol
    NodeSpirePartyProtocol
    NodeSpireVulkanContext
)

if(TARGET MeshShaders)
    add_dependencies(NodeSpireTD MeshShaders)
endif()

nodespire_stage_runtime_dependencies(NodeSpireTD)

if(UNIX)
    set_target_properties(NodeSpireTD PROPERTIES
        INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

install(TARGETS NodeSpireTD
    RUNTIME_DEPENDENCY_SET NodeSpireRuntimeDependencies
    RUNTIME DESTINATION "."
)
install(TARGETS SDL3-shared SDL3_mixer-shared spdlog
    RUNTIME DESTINATION "."
    LIBRARY DESTINATION "lib"
)
if(WIN32)
    install(RUNTIME_DEPENDENCY_SET NodeSpireRuntimeDependencies
        DESTINATION "."
        PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-win-.*"
        POST_EXCLUDE_REGEXES ".*[\\/]system32[\\/].*"
    )
else()
    install(RUNTIME_DEPENDENCY_SET NodeSpireRuntimeDependencies
        DESTINATION "lib"
        PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-win-.*"
        POST_EXCLUDE_REGEXES ".*[\\/]system32[\\/].*"
    )
endif()
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION ".")
