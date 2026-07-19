#include "scenes/PlayLevelScene.hpp"

#include "LuaStateBootstrap.hpp"
#include "VulkanContext.hpp"
#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"
#include "utility/WorldRenderer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>


PlayLevelScene::PlayLevelScene() = default;
PlayLevelScene::~PlayLevelScene() = default;

namespace {

constexpr float kPreWaveCountdownSeconds = 5.0f;
constexpr float kDefaultWaveRoundDurationSeconds = 30.0f;

constexpr float kPickStaticRadiusScale = 1.0f;
constexpr float kPickStaticRadiusPadding = 0.05f;
constexpr float kPickStaticMinRadius = 0.10f;
constexpr float kPickDynamicRadiusScale = 1.45f;
constexpr float kPickDynamicRadiusPadding = 0.35f;
constexpr float kPickDynamicMinRadius = 0.65f;
constexpr float kDebugOverlayFovRadians = glm::radians(60.0f);
constexpr int kTowerPoolPlacementsPerType = 32;
constexpr float kTowerHiddenY = -10000.0f;

enum class ProjectionRejectReason {
    None = 0,
    BehindCamera = 1,
    ClipW = 2,
    NdcZ = 3,
};

bool isProjectAssetPath(const std::filesystem::path& path) {
    const std::string normalized = path.generic_string();
    return normalized.rfind("assets/", 0) == 0;
}

PlayLevelScene* luaSceneSelf(lua_State* L) {
    return static_cast<PlayLevelScene*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int pushCommandResult(lua_State* L, bool ok, const char* reason) {
    lua_newtable(L);
    lua_pushboolean(L, ok);
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, reason);
    lua_setfield(L, -2, "reason");
    return 1;
}

glm::vec3 luaReadVec3Field(lua_State* L, int tableIndex, const char* fieldName, const glm::vec3& defaultValue) {
    lua_getfield(L, tableIndex, fieldName);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return defaultValue;
    }

    glm::vec3 value = defaultValue;
    lua_getfield(L, -1, "x");
    if (lua_isnumber(L, -1)) {
        value.x = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "y");
    if (lua_isnumber(L, -1)) {
        value.y = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "z");
    if (lua_isnumber(L, -1)) {
        value.z = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_pop(L, 1);
    return value;
}

void publishLevelUiTextures(lua_State* L, const WorldAssetSpec& spec) {
    lua_newtable(L);
    const int rootTable = lua_gettop(L);
    for (std::size_t i = 0; i < spec.uiTextures.size(); ++i) {
        const auto& entry = spec.uiTextures[i];
        lua_newtable(L);

        lua_pushstring(L, entry.id.c_str());
        lua_setfield(L, -2, "id");

        const std::string path = entry.texturePath.string();
        lua_pushstring(L, path.c_str());
        lua_setfield(L, -2, "path");

        lua_seti(L, rootTable, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(L, "LevelUiTextures");
}

bool projectWorldToScreen(const glm::vec3& worldPos,
                          const glm::mat4& view,
                          const glm::mat4& proj,
                          const ImVec2& displaySize,
                          const ImVec2& renderSize,
                          ImVec2& outScreen,
                          float& outDepthAbs,
                          ProjectionRejectReason* outRejectReason = nullptr) {
    if (outRejectReason) {
        *outRejectReason = ProjectionRejectReason::None;
    }

    const glm::vec4 viewPos = view * glm::vec4(worldPos, 1.0f);
    // Camera looks down -Z in view space; z >= 0 means behind camera.
    if (viewPos.z >= -1e-4f) {
        if (outRejectReason) {
            *outRejectReason = ProjectionRejectReason::BehindCamera;
        }
        return false;
    }
    outDepthAbs = -viewPos.z;

    const glm::vec4 clip = proj * viewPos;
    if (clip.w <= 1e-6f) {
        if (outRejectReason) {
            *outRejectReason = ProjectionRejectReason::ClipW;
        }
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        if (outRejectReason) {
            *outRejectReason = ProjectionRejectReason::NdcZ;
        }
        return false;
    }

    const float renderX = (ndc.x * 0.5f + 0.5f) * renderSize.x;
    const float renderY = (ndc.y * 0.5f + 0.5f) * renderSize.y;

    const float sx = (renderSize.x > 1e-5f) ? (displaySize.x / renderSize.x) : 1.0f;
    const float sy = (renderSize.y > 1e-5f) ? (displaySize.y / renderSize.y) : 1.0f;
    outScreen.x = renderX * sx;
    outScreen.y = renderY * sy;
    return true;
}

bool parseTowerPoolGroup(const std::string& group, std::string& outTowerId, int& outPoolIndex) {
    constexpr const char* kPrefix = "tower_pool:";
    if (group.rfind(kPrefix, 0) != 0) {
        return false;
    }

    const std::size_t idStart = std::char_traits<char>::length(kPrefix);
    const std::size_t sep = group.find(':', idStart);
    if (sep == std::string::npos || sep <= idStart) {
        return false;
    }

    const std::string towerId = group.substr(idStart, sep - idStart);
    const std::string indexPart = group.substr(sep + 1);
    if (towerId.empty() || indexPart.empty()) {
        return false;
    }

    int value = 0;
    for (char c : indexPart) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }

    outTowerId = towerId;
    outPoolIndex = value;
    return true;
}

std::string makeTowerIconTextureId(const std::string& towerId) {
    return "tower_icon:" + towerId;
}

} // namespace

// ─── camera helpers ───────────────────────────────────────────────────────────

// Forward direction from yaw + pitch (Y-up, right-handed)
static glm::vec3 cameraForward(float yaw, float pitch) {
    return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)));
}

glm::mat4 PlayLevelScene::buildViewMatrix() const {
    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    return glm::lookAt(camPos_, camPos_ + fwd, glm::vec3(0.0f, 1.0f, 0.0f));
}

void PlayLevelScene::updateCamera(float dt) {
    const ImGuiIO& io = ImGui::GetIO();

    // ── Mouse look (hold RMB) ─────────────────────────────────────────────
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        camYaw_ -= io.MouseDelta.x * 0.003f;
        camPitch_ -= io.MouseDelta.y * 0.003f;
        camPitch_ = glm::clamp(camPitch_, -1.48f, 1.48f); // ±~85°
    }

    // ── WASD + Space/Shift movement ───────────────────────────────────────
    // Speed: hold Shift to sprint (×4)
    const float baseSpeed = 8.0f;
    const float speed = baseSpeed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f) * dt;

    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (ImGui::IsKeyDown(ImGuiKey_W)) {
        camPos_ += fwd * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
        camPos_ -= fwd * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
        camPos_ -= right * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
        camPos_ += right * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_Space)) {
        camPos_.y += speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_Q)) {
        camPos_.y -= speed;
    }
}

// ─── scene lifecycle ──────────────────────────────────────────────────────────

void PlayLevelScene::onEnter(SceneSharedState& state) {
    // Reset camera
    camPos_ = {0.0f, 5.0f, 20.0f};
    camYaw_ = 3.14159f;
    camPitch_ = -0.25f;

    LuaStateBootstrap::initializeEngineState(L_, state.vulkanContext);
    registerLuaGameplayApi();

    gameplayState_.resetForNewRun();
    towerArchetypes_.clear();
    towerLoadoutIds_.clear();
    towerPoolGroupsById_.clear();
    towerGhostGroupById_.clear();
    selectedTowerLoadoutIndex_ = -1;
    placedTowers_.clear();
    towerPlacementHasHit_ = false;
    towerPlacementCanPlace_ = false;
    towerPlacementWorldPos_ = glm::vec3(0.0f);
    pendingCommands_.clear();
    activeEnemies_.clear();
    enemyArchetypes_.clear();
    waveDefinitions_.clear();
    routePoints_.clear();
    routeSegmentLengths_.clear();
    routeTotalLength_ = 0.0f;
    activeWaveIndex_ = -1;
    activeWaveSpawnIndex_ = 0;
    activeWaveSpawnedFromCurrent_ = 0;
    debugSelection_ = {};
    hoverSelection_ = {};
    hoveredInstanceIndex_ = -1;
    selectedInstanceIndex_ = -1;
    debugDrawPickSpheres_ = false;
    debugDrawHoverHighlight_ = true;
    debugPickEnabled_ = true;
    debugPickStatus_ = "click in world to inspect";
    selectedMapAssetPath_ = state.activeLevelAssetPath;
    selectedLevelScriptPath_ = state.activeLevelScriptPath;
    selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
    worldAssetSpec_ = {};

    if (selectedLevelScriptPath_.empty()) {
        selectedLevelScriptPath_ = "assets/scenes/PlayLevel.level.lua";
    }

    if (!loadLevelDefinition(state)) {
        spdlog::warn("PlayLevelScene: failed to load level definition {}.", selectedLevelScriptPath_.string());
    }

    if (enemyArchetypes_.empty() && !loadEnemyArchetype("assets/models/enemy/goblin1.enemy.lua")) {
        spdlog::warn("PlayLevelScene: using built-in enemy defaults because no archetype could be loaded.");
        EnemyArchetype fallback{};
        enemyArchetypes_[fallback.id] = fallback;
        defaultEnemyId_ = fallback.id;
    }

    if (waveDefinitions_.empty() && !selectedWavesScriptPath_.empty() && !loadWaveDefinitions(selectedWavesScriptPath_)) {
        spdlog::warn("PlayLevelScene: using fallback wave definition because {} failed to load.", selectedWavesScriptPath_);
    }

    if (waveDefinitions_.empty()) {
        WaveDefinition fallback;
        fallback.spawns.push_back(WaveSpawnDefinition{});
        waveDefinitions_.push_back(std::move(fallback));
    }

    discoverTowerArchetypes();

    for (const auto& [towerId, tower] : towerArchetypes_) {
        if (tower.modelPath.empty()) {
            continue;
        }

        if (!tower.previewImagePath.empty()) {
            WorldUiTextureSpec iconTex;
            iconTex.id = makeTowerIconTextureId(towerId);
            iconTex.texturePath = tower.previewImagePath;
            worldAssetSpec_.uiTextures.push_back(std::move(iconTex));
        }

        const std::string ghostGroup = "tower_pool_ghost:" + towerId;
        towerGhostGroupById_[towerId] = ghostGroup;

        WorldModelPlacementSpec ghostPlacement;
        ghostPlacement.modelPath = tower.modelPath;
        ghostPlacement.debugGroup = ghostGroup;
        ghostPlacement.debugLabel = tower.displayName + "_ghost";
        ghostPlacement.positionOffset = glm::vec3(0.0f, kTowerHiddenY, 0.0f);
        ghostPlacement.eulerDegrees = glm::vec3(0.0f, tower.facingYawOffsetDegrees, 0.0f);
        ghostPlacement.scale = glm::vec3(std::max(0.01f, tower.renderScale));
        worldAssetSpec_.extraWorldModels.push_back(std::move(ghostPlacement));

        auto& poolGroups = towerPoolGroupsById_[towerId];
        poolGroups.reserve(kTowerPoolPlacementsPerType);
        for (int i = 0; i < kTowerPoolPlacementsPerType; ++i) {
            const std::string poolGroup = "tower_pool:" + towerId + ":" + std::to_string(i);
            poolGroups.push_back(poolGroup);

            WorldModelPlacementSpec placedPlacement;
            placedPlacement.modelPath = tower.modelPath;
            placedPlacement.debugGroup = poolGroup;
            placedPlacement.debugLabel = tower.displayName + "_placed_" + std::to_string(i + 1);
            placedPlacement.positionOffset = glm::vec3(0.0f, kTowerHiddenY, 0.0f);
            placedPlacement.eulerDegrees = glm::vec3(0.0f, tower.facingYawOffsetDegrees, 0.0f);
            placedPlacement.scale = glm::vec3(std::max(0.01f, tower.renderScale));
            worldAssetSpec_.extraWorldModels.push_back(std::move(placedPlacement));
        }
    }

    publishLevelUiTextures(L_, worldAssetSpec_);

    {
        std::vector<std::filesystem::path> templateModels;
        templateModels.reserve(enemyArchetypes_.size());
        for (const auto& [id, archetype] : enemyArchetypes_) {
            (void)id;
            if (archetype.modelPath.empty()) {
                continue;
            }
            const std::filesystem::path modelPath = archetype.modelPath;
            if (std::find(templateModels.begin(), templateModels.end(), modelPath) == templateModels.end()) {
                templateModels.push_back(modelPath);
            }
        }
        if (!templateModels.empty()) {
            worldAssetSpec_.animatedTemplateModelPaths = std::move(templateModels);
        }
    }

    scriptRef_ = loadLuaScript(state, "assets/scenes/PlayLevel.lua");
    luaOnEnter(scriptRef_);

    worldRenderer_.reset();
    loadStatus_.clear();

    std::filesystem::path assetPath = selectedMapAssetPath_;
    if (std::filesystem::is_directory(assetPath)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
            const auto ext = entry.path().extension().string();
            if (ext == ".glb" || ext == ".gltf") {
                assetPath = entry.path();
                break;
            }
        }
    }

    if (!state.vulkanContext) {
        loadStatus_ = "No Vulkan context available.";
        return;
    }

    worldRenderer_ = std::make_unique<WorldRenderer>(L_, *state.vulkanContext);
    worldRenderer_->beginLoad(assetPath, worldAssetSpec_); // non-blocking
}

void PlayLevelScene::onExit(SceneSharedState& state) {
    luaOnExit(state, scriptRef_);

    if (state.vulkanContext) {
        state.vulkanContext->waitIdle();
    }
    worldRenderer_.reset();
}

void PlayLevelScene::renderWorld(VkCommandBuffer cmd, VkExtent2D extent) {
    lastRenderExtent_ = extent;
    if (worldRenderer_ && worldRenderer_->isLoaded()) {
        worldRenderer_->render(cmd, extent, buildViewMatrix());
    }
}

// ─── per-frame ────────────────────────────────────────────────────────────────

SceneFrameResult PlayLevelScene::render(SceneSharedState& state, float dt) {
    SceneFrameResult result;

    applyPendingGameplayCommands();
    updateWaveSimulation(dt);

    // Tick GPU uploads (one step per frame while loading)
    if (worldRenderer_ && !worldRenderer_->isLoaded() && !worldRenderer_->loadFailed()) {
        worldRenderer_->tickLoad();
    }

    const bool isLoaded = worldRenderer_ && worldRenderer_->isLoaded();

    if (isLoaded) {
        updateRouteFromWorld();
        syncTowerInstanceTransforms();
        syncPlacedTowerModels();
        updateCamera(dt);
        updateTowerPlacementFromInput();
        syncTowerInstanceTransforms();
        syncPlacedTowerModels();
        const bool hoverChanged = updateDebugHoverFromMouse();
        if (hoverChanged) {
            // Re-apply instance transforms so hover visual feedback is in the same frame as hover detection.
            syncTowerInstanceTransforms();
            syncPlacedTowerModels();
        }
        updateDebugPickFromMouse();
        worldRenderer_->setHighlightedInstances(hoveredInstanceIndex_, selectedInstanceIndex_);
    }

    SceneFrameResult luaResult = luaOnRender(state, scriptRef_, dt);
    if (luaResult.requestQuit) {
        result.requestQuit = true;
    }
    if (luaResult.requestApplySettings) {
        result.requestApplySettings = true;
    }
    if (luaResult.requestAcceptDisplayChanges) {
        result.requestAcceptDisplayChanges = true;
    }
    if (luaResult.requestRevertDisplayChanges) {
        result.requestRevertDisplayChanges = true;
    }
    if (luaResult.requestTransition) {
        result.requestTransition = true;
        result.transitionTarget = luaResult.transitionTarget;
        result.transitionMessage = luaResult.transitionMessage;
        result.transitionMinDurationSeconds = luaResult.transitionMinDurationSeconds;
    }

    if (isLoaded) {
        drawTowerPlacementOverlay();
        drawDebugPickSpheresOverlay();
        drawHoverHighlightOverlay();
    }

    return result;
}

bool PlayLevelScene::requestSpendMoney(float amount) {
    if (amount <= 0 || gameplayState_.matchStatus != MatchStatus::Running) {
        return false;
    }
    if (gameplayState_.playerMoney < amount) {
        return false;
    }

    gameplayState_.playerMoney -= amount;
    return true;
}

bool PlayLevelScene::requestDamageBase(float amount) {
    if (amount <= 0 || gameplayState_.matchStatus != MatchStatus::Running) {
        return false;
    }

    gameplayState_.baseHealth = std::max(0.0f, gameplayState_.baseHealth - amount);
    if (gameplayState_.baseHealth == 0.0f) {
        gameplayState_.matchStatus = MatchStatus::Defeat;
    }
    return true;
}

bool PlayLevelScene::requestStartWave() {
    return beginWaveCountdown();
}

bool PlayLevelScene::beginWaveCountdown() {
    if (!validateStartWaveRequest().empty()) {
        return false;
    }

    gameplayState_.waveCountdownActive = true;
    gameplayState_.waveCountdownRemainingSeconds = kPreWaveCountdownSeconds;
    return true;
}

bool PlayLevelScene::beginWaveSpawning() {
    if (gameplayState_.currentWave < 1 || gameplayState_.currentWave > static_cast<int>(waveDefinitions_.size())) {
        return false;
    }

    activeWaveIndex_ = std::clamp(gameplayState_.currentWave - 1, 0, static_cast<int>(waveDefinitions_.size()) - 1);
    const WaveDefinition& waveDef = waveDefinitions_[activeWaveIndex_];

    int totalToSpawn = 0;
    for (const WaveSpawnDefinition& spawn : waveDef.spawns) {
        totalToSpawn += std::max(1, spawn.count);
    }

    gameplayState_.enemiesToSpawn = totalToSpawn;
    gameplayState_.enemiesAlive = static_cast<int>(activeEnemies_.size());
    gameplayState_.enemiesDefeated = 0;
    gameplayState_.spawnAccumulatorSeconds = 0.0f;
    gameplayState_.defeatAccumulatorSeconds = 0.0f;
    gameplayState_.waveRoundDurationSeconds =
        (waveDef.roundDurationSeconds > 0.0f) ? waveDef.roundDurationSeconds : kDefaultWaveRoundDurationSeconds;
    gameplayState_.waveRoundRemainingSeconds = gameplayState_.waveRoundDurationSeconds;
    gameplayState_.waveCountdownActive = false;
    gameplayState_.waveCountdownRemainingSeconds = 0.0f;
    activeWaveSpawnIndex_ = 0;
    activeWaveSpawnedFromCurrent_ = 0;
    gameplayState_.waveInProgress = true;
    return true;
}

void PlayLevelScene::completeWaveAndAdvance() {
    gameplayState_.waveInProgress = false;
    gameplayState_.waveCountdownActive = false;
    gameplayState_.waveCountdownRemainingSeconds = 0.0f;
    gameplayState_.waveRoundDurationSeconds = 0.0f;
    gameplayState_.waveRoundRemainingSeconds = 0.0f;
    gameplayState_.enemiesToSpawn = 0;
    gameplayState_.spawnAccumulatorSeconds = 0.0f;
    activeWaveIndex_ = -1;
    activeWaveSpawnIndex_ = 0;
    activeWaveSpawnedFromCurrent_ = 0;

    if (gameplayState_.currentWave <= static_cast<int>(waveDefinitions_.size())) {
        gameplayState_.currentWave += 1;
    }
}

bool PlayLevelScene::loadLevelDefinition(SceneSharedState& state) {
    if (!L_) {
        return false;
    }

    const std::filesystem::path scriptPath = selectedLevelScriptPath_;
    if (!std::filesystem::exists(scriptPath)) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.string().c_str()) != LUA_OK) {
        spdlog::error("PlayLevelScene: failed to load level definition {}: {}", scriptPath.string(),
                      lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("PlayLevelScene: level definition execution error {}: {}", scriptPath.string(),
                      lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (!lua_istable(L_, -1)) {
        spdlog::error("PlayLevelScene: level definition must return a table: {}", scriptPath.string());
        lua_pop(L_, 1);
        return false;
    }

    const std::filesystem::path scriptDir = scriptPath.parent_path();

    lua_getfield(L_, -1, "mapAssetPath");
    if (lua_isstring(L_, -1)) {
        std::filesystem::path rawPath = lua_tostring(L_, -1);
        if (!rawPath.empty()) {
            if (rawPath.is_relative() && !isProjectAssetPath(rawPath)) {
                rawPath = scriptDir / rawPath;
            }
            selectedMapAssetPath_ = rawPath;
        }
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "wavesScriptPath");
    if (lua_isstring(L_, -1)) {
        std::filesystem::path rawPath = lua_tostring(L_, -1);
        if (!rawPath.empty()) {
            if (rawPath.is_relative() && !isProjectAssetPath(rawPath)) {
                rawPath = scriptDir / rawPath;
            }
            selectedWavesScriptPath_ = rawPath.string();
        }
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "onLoad");
    if (lua_isfunction(L_, -1)) {
        lua_pushvalue(L_, -2);
        if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
            spdlog::error("PlayLevelScene: level definition onLoad() execution error {}: {}", scriptPath.string(),
                          lua_tostring(L_, -1));
            lua_pop(L_, 2);
            return false;
        }
    } else {
        lua_pop(L_, 1);
    }

    lua_getfield(L_, -1, "inheritActiveSelection");
    const bool inheritActiveSelection = lua_isboolean(L_, -1) ? lua_toboolean(L_, -1) != 0 : true;
    lua_pop(L_, 1);

    worldAssetSpec_ = {};
    lua_getfield(L_, -1, "worldAssets");
    if (lua_istable(L_, -1)) {
        const int worldAssetsIdx = lua_gettop(L_);

        lua_getfield(L_, worldAssetsIdx, "startModelPath");
        if (lua_isstring(L_, -1)) {
            const std::string path = lua_tostring(L_, -1);
            if (!path.empty()) {
                worldAssetSpec_.startModelPath = path;
            }
        }
        lua_pop(L_, 1);

        lua_getfield(L_, worldAssetsIdx, "endModelPath");
        if (lua_isstring(L_, -1)) {
            const std::string path = lua_tostring(L_, -1);
            if (!path.empty()) {
                worldAssetSpec_.endModelPath = path;
            }
        }
        lua_pop(L_, 1);

        lua_getfield(L_, worldAssetsIdx, "animatedTemplateModelPaths");
        if (lua_istable(L_, -1)) {
            worldAssetSpec_.animatedTemplateModelPaths.clear();
            const int templateTable = lua_gettop(L_);
            const int templateCount = static_cast<int>(lua_rawlen(L_, templateTable));
            for (int i = 1; i <= templateCount; ++i) {
                lua_geti(L_, templateTable, i);
                if (lua_isstring(L_, -1)) {
                    const std::string path = lua_tostring(L_, -1);
                    if (!path.empty()) {
                        worldAssetSpec_.animatedTemplateModelPaths.emplace_back(path);
                    }
                }
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);

        if (worldAssetSpec_.animatedTemplateModelPaths.empty()) {
            lua_getfield(L_, worldAssetsIdx, "animatedTemplateModelPath");
            if (lua_isstring(L_, -1)) {
                const std::string path = lua_tostring(L_, -1);
                if (!path.empty()) {
                    worldAssetSpec_.animatedTemplateModelPaths.emplace_back(path);
                }
            }
            lua_pop(L_, 1);
        }

        lua_getfield(L_, worldAssetsIdx, "extraWorldModels");
        if (lua_istable(L_, -1)) {
            const int modelsIdx = lua_gettop(L_);
            const int modelCount = static_cast<int>(lua_rawlen(L_, modelsIdx));
            for (int i = 1; i <= modelCount; ++i) {
                lua_geti(L_, modelsIdx, i);
                if (!lua_istable(L_, -1)) {
                    lua_pop(L_, 1);
                    continue;
                }

                WorldModelPlacementSpec placement;

                lua_getfield(L_, -1, "modelPath");
                if (lua_isstring(L_, -1)) {
                    placement.modelPath = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "debugGroup");
                if (lua_isstring(L_, -1)) {
                    placement.debugGroup = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "debugLabel");
                if (lua_isstring(L_, -1)) {
                    placement.debugLabel = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "anchor");
                if (lua_isstring(L_, -1)) {
                    const std::string anchor = lua_tostring(L_, -1);
                    if (anchor == "Start") {
                        placement.anchor = WorldMarkerAnchor::Start;
                    } else if (anchor == "End") {
                        placement.anchor = WorldMarkerAnchor::End;
                    }
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "facePath");
                if (lua_isboolean(L_, -1)) {
                    placement.facePath = lua_toboolean(L_, -1) != 0;
                }
                lua_pop(L_, 1);

                placement.positionOffset = luaReadVec3Field(L_, -1, "positionOffset", placement.positionOffset);
                placement.eulerDegrees = luaReadVec3Field(L_, -1, "eulerDegrees", placement.eulerDegrees);
                placement.scale = luaReadVec3Field(L_, -1, "scale", placement.scale);

                if (!placement.modelPath.empty()) {
                    if (placement.debugLabel.empty()) {
                        placement.debugLabel = placement.modelPath.filename().string();
                    }
                    worldAssetSpec_.extraWorldModels.push_back(std::move(placement));
                }
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);

        lua_getfield(L_, worldAssetsIdx, "uiTextures");
        if (lua_istable(L_, -1)) {
            const int texturesIdx = lua_gettop(L_);
            const int textureCount = static_cast<int>(lua_rawlen(L_, texturesIdx));
            for (int i = 1; i <= textureCount; ++i) {
                lua_geti(L_, texturesIdx, i);

                WorldUiTextureSpec texSpec;
                if (lua_isstring(L_, -1)) {
                    texSpec.texturePath = lua_tostring(L_, -1);
                } else if (lua_istable(L_, -1)) {
                    lua_getfield(L_, -1, "id");
                    if (lua_isstring(L_, -1)) {
                        texSpec.id = lua_tostring(L_, -1);
                    }
                    lua_pop(L_, 1);

                    lua_getfield(L_, -1, "path");
                    if (lua_isstring(L_, -1)) {
                        texSpec.texturePath = lua_tostring(L_, -1);
                    }
                    lua_pop(L_, 1);
                }

                if (!texSpec.texturePath.empty()) {
                    if (texSpec.id.empty()) {
                        texSpec.id = texSpec.texturePath.stem().string();
                    }
                    worldAssetSpec_.uiTextures.push_back(std::move(texSpec));
                }

                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    if (inheritActiveSelection) {
        selectedMapAssetPath_ = state.activeLevelAssetPath;
    }

    if (waveDefinitions_.empty() && !selectedWavesScriptPath_.empty()) {
        loadWaveDefinitions(selectedWavesScriptPath_);
    }

    publishLevelUiTextures(L_, worldAssetSpec_);

    lua_pop(L_, 1);
    return true;
}

bool PlayLevelScene::loadWaveDefinitions(const std::string& scriptPath) {
    waveDefinitions_.clear();
    if (!L_) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("PlayLevelScene: failed to load wave script {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }
    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("PlayLevelScene: wave script execution error {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }
    if (!lua_istable(L_, -1)) {
        spdlog::error("PlayLevelScene: wave script must return a table: {}", scriptPath);
        lua_pop(L_, 1);
        return false;
    }

    lua_getfield(L_, -1, "onLoad");
    if (lua_isfunction(L_, -1)) {
        lua_pushvalue(L_, -2);
        if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
            spdlog::error("PlayLevelScene: wave onLoad() execution error {}: {}", scriptPath, lua_tostring(L_, -1));
            lua_pop(L_, 2);
            return false;
        }
    } else {
        lua_pop(L_, 1);
    }

    auto parseWaveSpawnEntry = [&](int spawnTableIdx, WaveSpawnDefinition& outSpawn) {
        outSpawn.enemyId = defaultEnemyId_;
        if (const EnemyArchetype* archetype = findEnemyArchetype(outSpawn.enemyId)) {
            outSpawn.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
        }

        lua_getfield(L_, spawnTableIdx, "entity");
        if (lua_isstring(L_, -1)) {
            outSpawn.enemyId = lua_tostring(L_, -1);
        } else if (lua_istable(L_, -1)) {
            lua_getfield(L_, -1, "id");
            if (lua_isstring(L_, -1)) {
                outSpawn.enemyId = lua_tostring(L_, -1);
            }
            lua_pop(L_, 1);
        }
        lua_pop(L_, 1);

        lua_getfield(L_, spawnTableIdx, "enemyId");
        if (lua_isstring(L_, -1)) {
            outSpawn.enemyId = lua_tostring(L_, -1);
        }
        lua_pop(L_, 1);

        const EnemyArchetype* archetype = findEnemyArchetype(outSpawn.enemyId);
        if (!archetype) {
            spdlog::warn("PlayLevelScene: wave spawn references unknown enemy '{}', using '{}' fallback.",
                         outSpawn.enemyId, defaultEnemyId_);
            outSpawn.enemyId = defaultEnemyId_;
            archetype = findEnemyArchetype(outSpawn.enemyId);
        }

        lua_getfield(L_, spawnTableIdx, "count");
        if (lua_isinteger(L_, -1)) {
            outSpawn.count = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, spawnTableIdx, "spawnIntervalSeconds");
        if (lua_isnumber(L_, -1)) {
            outSpawn.spawnIntervalSeconds = static_cast<float>(lua_tonumber(L_, -1));
        } else if (archetype) {
            outSpawn.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
        }
        lua_pop(L_, 1);

        outSpawn.count = std::max(1, outSpawn.count);
        outSpawn.spawnIntervalSeconds = std::max(0.05f, outSpawn.spawnIntervalSeconds);
    };

    lua_getfield(L_, -1, "waves");
    if (waveDefinitions_.empty() && lua_istable(L_, -1)) {
        const int waveTable = lua_gettop(L_);
        const int waveCount = static_cast<int>(lua_rawlen(L_, waveTable));
        for (int i = 1; i <= waveCount; ++i) {
            lua_geti(L_, waveTable, i);
            if (!lua_istable(L_, -1)) {
                lua_pop(L_, 1);
                continue;
            }

            WaveDefinition waveDef;

            lua_getfield(L_, -1, "roundDurationSeconds");
            if (lua_isnumber(L_, -1)) {
                waveDef.roundDurationSeconds = static_cast<float>(lua_tonumber(L_, -1));
            }
            lua_pop(L_, 1);

            lua_getfield(L_, -1, "spawns");
            if (lua_istable(L_, -1)) {
                const int spawnsTable = lua_gettop(L_);
                const int spawnCount = static_cast<int>(lua_rawlen(L_, spawnsTable));
                for (int spawnIdx = 1; spawnIdx <= spawnCount; ++spawnIdx) {
                    lua_geti(L_, spawnsTable, spawnIdx);
                    if (!lua_istable(L_, -1)) {
                        lua_pop(L_, 1);
                        continue;
                    }
                    WaveSpawnDefinition spawnDef;
                    parseWaveSpawnEntry(lua_gettop(L_), spawnDef);
                    waveDef.spawns.push_back(std::move(spawnDef));
                    lua_pop(L_, 1);
                }
            }
            lua_pop(L_, 1);

            if (waveDef.spawns.empty()) {
                WaveSpawnDefinition legacySpawn;
                parseWaveSpawnEntry(lua_gettop(L_), legacySpawn);
                waveDef.spawns.push_back(std::move(legacySpawn));
            }

            waveDef.roundDurationSeconds = std::max(1.0f, waveDef.roundDurationSeconds);

            waveDefinitions_.push_back(std::move(waveDef));
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
    lua_pop(L_, 1);

    if (waveDefinitions_.empty()) {
        WaveDefinition fallback;
        fallback.spawns.push_back(WaveSpawnDefinition{});
        waveDefinitions_.push_back(std::move(fallback));
    }

    return true;
}

bool PlayLevelScene::parseEnemyArchetypeScript(const std::string& scriptPath, EnemyArchetype& outArchetype) {
    if (!L_) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("PlayLevelScene: failed to load enemy archetype {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("PlayLevelScene: enemy archetype script error {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (!lua_istable(L_, -1)) {
        spdlog::error("PlayLevelScene: enemy archetype script must return a table: {}", scriptPath);
        lua_pop(L_, 1);
        return false;
    }

    auto readStringField = [&](const char* key, std::string& out) {
        lua_getfield(L_, -1, key);
        if (lua_isstring(L_, -1)) {
            out = lua_tostring(L_, -1);
        }
        lua_pop(L_, 1);
    };

    readStringField("id", outArchetype.id);
    readStringField("displayName", outArchetype.displayName);
    readStringField("model", outArchetype.modelPath);

    lua_getfield(L_, -1, "stats");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "health");
        if (lua_isinteger(L_, -1)) {
            outArchetype.health = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "moveSpeed");
        if (lua_isnumber(L_, -1)) {
            outArchetype.moveSpeed = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "rewardMoney");
        if (lua_isinteger(L_, -1)) {
            outArchetype.rewardMoney = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "baseDamage");
        if (lua_isinteger(L_, -1)) {
            outArchetype.baseDamage = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "render");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "renderScale");
        if (lua_isnumber(L_, -1)) {
            outArchetype.renderScale = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "facingYawOffsetDegrees");
        if (lua_isnumber(L_, -1)) {
            outArchetype.facingYawOffsetDegrees = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_pop(L_, 1); // archetype root table

    if (outArchetype.id.empty()) {
        outArchetype.id = std::filesystem::path(scriptPath).stem().string();
    }

    if (outArchetype.health <= 0) {
        outArchetype.health = 1;
    }
    if (outArchetype.moveSpeed <= 0.0f) {
        outArchetype.moveSpeed = 0.1f;
    }
    if (outArchetype.spawnIntervalSeconds <= 0.05f) {
        outArchetype.spawnIntervalSeconds = 0.05f;
    }
    if (outArchetype.defeatIntervalSeconds <= 0.05f) {
        outArchetype.defeatIntervalSeconds = 0.05f;
    }
    if (outArchetype.baseDamage <= 0) {
        outArchetype.baseDamage = 1;
    }
    if (outArchetype.renderScale <= 0.01f) {
        outArchetype.renderScale = 1.0f;
    }

    return true;
}

bool PlayLevelScene::loadEnemyArchetype(const std::string& scriptPath) {
    EnemyArchetype archetype;
    if (!parseEnemyArchetypeScript(scriptPath, archetype)) {
        return false;
    }

    if (defaultEnemyId_.empty()) {
        defaultEnemyId_ = archetype.id;
    }
    if (enemyArchetypes_.empty()) {
        defaultEnemyId_ = archetype.id;
    }
    enemyArchetypes_[archetype.id] = std::move(archetype);
    return true;
}

bool PlayLevelScene::parseTowerArchetypeScript(const std::string& scriptPath, TowerArchetype& outArchetype) {
    if (!L_) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("PlayLevelScene: failed to load tower archetype {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("PlayLevelScene: tower archetype script error {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (!lua_istable(L_, -1)) {
        spdlog::error("PlayLevelScene: tower archetype script must return a table: {}", scriptPath);
        lua_pop(L_, 1);
        return false;
    }

    auto readStringField = [&](const char* key, std::string& out) {
        lua_getfield(L_, -1, key);
        if (lua_isstring(L_, -1)) {
            out = lua_tostring(L_, -1);
        }
        lua_pop(L_, 1);
    };

    readStringField("id", outArchetype.id);
    readStringField("displayName", outArchetype.displayName);
    readStringField("model", outArchetype.modelPath);
    readStringField("previewImage", outArchetype.previewImagePath);

    lua_getfield(L_, -1, "stats");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "cost");
        if (lua_isinteger(L_, -1)) {
            outArchetype.cost = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "attackRange");
        if (lua_isnumber(L_, -1)) {
            outArchetype.attackRange = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "render");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "renderScale");
        if (lua_isnumber(L_, -1)) {
            outArchetype.renderScale = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "facingYawOffsetDegrees");
        if (lua_isnumber(L_, -1)) {
            outArchetype.facingYawOffsetDegrees = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_pop(L_, 1);

    if (outArchetype.id.empty()) {
        outArchetype.id = std::filesystem::path(scriptPath).stem().string();
    }
    if (outArchetype.displayName.empty()) {
        outArchetype.displayName = outArchetype.id;
    }
    if (outArchetype.cost <= 0) {
        outArchetype.cost = 1;
    }
    if (outArchetype.attackRange <= 0.1f) {
        outArchetype.attackRange = 1.0f;
    }
    if (outArchetype.renderScale <= 0.01f) {
        outArchetype.renderScale = 1.0f;
    }

    return true;
}

bool PlayLevelScene::loadTowerArchetype(const std::string& scriptPath) {
    TowerArchetype archetype;
    if (!parseTowerArchetypeScript(scriptPath, archetype)) {
        return false;
    }

    if (archetype.previewImagePath.empty() && !archetype.modelPath.empty()) {
        std::filesystem::path candidate = std::filesystem::path(archetype.modelPath).replace_extension(".png");
        if (std::filesystem::exists(candidate)) {
            archetype.previewImagePath = candidate.string();
        }
    }

    towerArchetypes_[archetype.id] = archetype;
    if (towerLoadoutIds_.size() < 5) {
        towerLoadoutIds_.push_back(archetype.id);
    }
    return true;
}

void PlayLevelScene::discoverTowerArchetypes() {
    const std::filesystem::path towersDir = "assets/models/towers";
    if (!std::filesystem::exists(towersDir)) {
        return;
    }

    std::vector<std::filesystem::path> scripts;
    for (const auto& entry : std::filesystem::directory_iterator(towersDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.size() >= 10 && name.rfind(".tower.lua") == (name.size() - 10)) {
            scripts.push_back(entry.path());
        }
    }
    std::sort(scripts.begin(), scripts.end());

    for (const auto& path : scripts) {
        loadTowerArchetype(path.string());
    }

    if (!towerLoadoutIds_.empty()) {
        selectedTowerLoadoutIndex_ = 0;
    }
}

const PlayLevelScene::TowerArchetype* PlayLevelScene::findTowerArchetype(const std::string& towerId) const {
    auto it = towerArchetypes_.find(towerId);
    if (it == towerArchetypes_.end()) {
        return nullptr;
    }
    return &it->second;
}

const PlayLevelScene::TowerArchetype* PlayLevelScene::selectedTowerArchetype() const {
    if (selectedTowerLoadoutIndex_ < 0 || selectedTowerLoadoutIndex_ >= static_cast<int>(towerLoadoutIds_.size())) {
        return nullptr;
    }
    return findTowerArchetype(towerLoadoutIds_[selectedTowerLoadoutIndex_]);
}

bool PlayLevelScene::raycastGroundAtCursor(glm::vec3& outHit) const {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return false;
    }

    const ImVec2 mousePos = io.MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return false;
    }

    const float ndcX = (2.0f * mousePos.x) / displaySize.x - 1.0f;
    const float ndcY = 1.0f - (2.0f * mousePos.y) / displaySize.y;
    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;

    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    constexpr float kFovYRadians = glm::radians(60.0f);
    const float tanHalfFovY = std::tan(kFovYRadians * 0.5f);

    glm::vec3 rayDir = fwd + right * (ndcX * aspect * tanHalfFovY) + up * (ndcY * tanHalfFovY);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return false;
    }
    rayDir = glm::normalize(rayDir);

    if (std::abs(rayDir.y) <= 1e-5f) {
        return false;
    }

    const float t = -camPos_.y / rayDir.y;
    if (t <= 0.0f) {
        return false;
    }

    outHit = camPos_ + rayDir * t;
    return std::isfinite(outHit.x) && std::isfinite(outHit.y) && std::isfinite(outHit.z);
}

std::string PlayLevelScene::validateTowerPlacement(const TowerArchetype& archetype, const glm::vec3& worldPos) const {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return "world is still loading";
    }
    if (gameplayState_.playerMoney < static_cast<float>(archetype.cost)) {
        return "insufficient funds";
    }

    constexpr float kMinTowerSpacing = 1.7f;
    for (const PlacedTower& tower : placedTowers_) {
        const glm::vec3 delta = worldPos - tower.position;
        const float dist2 = glm::dot(delta, delta);
        if (dist2 < (kMinTowerSpacing * kMinTowerSpacing)) {
            return "too close to another tower";
        }
    }

    return {};
}

void PlayLevelScene::updateTowerPlacementFromInput() {
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
        selectedTowerLoadoutIndex_ = (towerLoadoutIds_.size() >= 1) ? 0 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_2, false)) {
        selectedTowerLoadoutIndex_ = (towerLoadoutIds_.size() >= 2) ? 1 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
        selectedTowerLoadoutIndex_ = (towerLoadoutIds_.size() >= 3) ? 2 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_4, false)) {
        selectedTowerLoadoutIndex_ = (towerLoadoutIds_.size() >= 4) ? 3 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_5, false)) {
        selectedTowerLoadoutIndex_ = (towerLoadoutIds_.size() >= 5) ? 4 : -1;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        selectedTowerLoadoutIndex_ = -1;
        towerPlacementHasHit_ = false;
        towerPlacementCanPlace_ = false;
        return;
    }

    const TowerArchetype* selected = selectedTowerArchetype();
    if (!selected) {
        towerPlacementHasHit_ = false;
        towerPlacementCanPlace_ = false;
        return;
    }

    glm::vec3 hitPos{0.0f};
    towerPlacementHasHit_ = raycastGroundAtCursor(hitPos);
    if (!towerPlacementHasHit_) {
        towerPlacementCanPlace_ = false;
        return;
    }

    towerPlacementWorldPos_ = hitPos;
    towerPlacementWorldPos_.y = 0.0f;
    towerPlacementCanPlace_ = validateTowerPlacement(*selected, towerPlacementWorldPos_).empty();

    if (io.WantCaptureMouse) {
        return;
    }

    if (towerPlacementCanPlace_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (requestSpendMoney(static_cast<float>(selected->cost))) {
            placedTowers_.push_back(PlacedTower{selected->id, towerPlacementWorldPos_, selected->attackRange, selected->cost});
        }
    }
}

glm::mat4 PlayLevelScene::buildTowerModelTransform(const TowerArchetype& archetype, const glm::vec3& worldPos) const {
    return glm::translate(glm::mat4{1.0f}, worldPos) *
           glm::rotate(glm::mat4{1.0f}, glm::radians(archetype.facingYawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, archetype.renderScale)));
}

void PlayLevelScene::syncPlacedTowerModels() {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    std::unordered_map<std::string, int> usedPerTower;
    for (const PlacedTower& placed : placedTowers_) {
        const TowerArchetype* tower = findTowerArchetype(placed.towerId);
        if (!tower) {
            continue;
        }

        const auto poolsIt = towerPoolGroupsById_.find(placed.towerId);
        if (poolsIt == towerPoolGroupsById_.end()) {
            continue;
        }

        const int poolIndex = usedPerTower[placed.towerId]++;
        if (poolIndex < 0 || poolIndex >= static_cast<int>(poolsIt->second.size())) {
            continue;
        }

        const glm::mat4 model = buildTowerModelTransform(*tower, placed.position);
        worldRenderer_->setWorldModelTransformByDebugGroup(poolsIt->second[poolIndex], model);
    }

    for (const auto& [towerId, groups] : towerPoolGroupsById_) {
        const int usedCount = usedPerTower[towerId];
        for (int i = usedCount; i < static_cast<int>(groups.size()); ++i) {
            const glm::mat4 hidden = glm::translate(glm::mat4{1.0f}, glm::vec3(0.0f, kTowerHiddenY, 0.0f));
            worldRenderer_->setWorldModelTransformByDebugGroup(groups[i], hidden);
        }
    }

    for (const auto& [towerId, ghostGroup] : towerGhostGroupById_) {
        const TowerArchetype* tower = findTowerArchetype(towerId);
        if (!tower) {
            continue;
        }

        glm::mat4 ghost = glm::translate(glm::mat4{1.0f}, glm::vec3(0.0f, kTowerHiddenY, 0.0f));
        const TowerArchetype* selected = selectedTowerArchetype();
        if (selected && selected->id == towerId && towerPlacementHasHit_) {
            ghost = buildTowerModelTransform(*tower, towerPlacementWorldPos_ + glm::vec3(0.0f, 0.02f, 0.0f));
        }
        worldRenderer_->setWorldModelTransformByDebugGroup(ghostGroup, ghost);
    }
}

void PlayLevelScene::syncTowerInstanceTransforms() {
    if (!worldRenderer_) {
        return;
    }

    std::vector<glm::mat4> transforms;
    transforms.reserve(activeEnemies_.size());

    for (const ActiveEnemy& enemy : activeEnemies_) {
        const glm::vec3 pos = sampleRoutePosition(enemy.distanceAlongPath);
        const float yaw =
            sampleRouteYaw(enemy.distanceAlongPath) + (enemy.facingYawOffsetDegrees * 0.01745329251994329577f);
        const glm::mat4 model = glm::translate(glm::mat4{1.0f}, pos) *
                                glm::rotate(glm::mat4{1.0f}, yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::scale(glm::mat4{1.0f}, glm::vec3(enemy.renderScale));
        transforms.push_back(model);
    }

    worldRenderer_->setAnimatedEntityInstanceTransforms(transforms);
}

void PlayLevelScene::drawTowerPlacementOverlay() const {
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return;
    }

    const ImVec2 renderSize(
        (lastRenderExtent_.width > 0) ? static_cast<float>(lastRenderExtent_.width) : displaySize.x,
        (lastRenderExtent_.height > 0) ? static_cast<float>(lastRenderExtent_.height) : displaySize.y);

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(kDebugOverlayFovRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;
    const glm::mat4 view = buildViewMatrix();

    auto drawWorldRing = [&](const glm::vec3& center, float radius, ImU32 color, int segments, float thickness) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImVec2 prev{};
        bool prevValid = false;
        for (int i = 0; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float a = t * 6.2831853071795864769f;
            const glm::vec3 worldPoint = center + glm::vec3(std::cos(a) * radius, 0.02f, std::sin(a) * radius);

            ImVec2 screenPoint{};
            float depthAbs = 0.0f;
            if (!projectWorldToScreen(worldPoint, view, proj, displaySize, renderSize, screenPoint, depthAbs, nullptr)) {
                prevValid = false;
                continue;
            }
            if (prevValid) {
                drawList->AddLine(prev, screenPoint, color, thickness);
            }
            prev = screenPoint;
            prevValid = true;
        }
    };

    std::string selectedTowerId;
    int selectedTowerPoolIndex = -1;
    if (debugSelection_.valid) {
        parseTowerPoolGroup(debugSelection_.group, selectedTowerId, selectedTowerPoolIndex);
    }

    if (!selectedTowerId.empty() && selectedTowerPoolIndex >= 0) {
        int perTypeIndex = 0;
        for (const PlacedTower& tower : placedTowers_) {
            if (tower.towerId != selectedTowerId) {
                continue;
            }
            if (perTypeIndex == selectedTowerPoolIndex) {
                drawWorldRing(tower.position, std::max(0.5f, tower.attackRange), IM_COL32(75, 175, 255, 165), 64, 2.0f);
                break;
            }
            ++perTypeIndex;
        }
    }

    const TowerArchetype* selected = selectedTowerArchetype();
    if (!selected || !towerPlacementHasHit_) {
        return;
    }

    const ImU32 col = towerPlacementCanPlace_ ? IM_COL32(90, 255, 120, 210) : IM_COL32(255, 90, 90, 210);
    drawWorldRing(towerPlacementWorldPos_, std::max(0.5f, selected->attackRange), col, 64, 2.0f);

    ImVec2 centerScreen{};
    float depthAbs = 0.0f;
    if (projectWorldToScreen(towerPlacementWorldPos_ + glm::vec3(0.0f, 0.05f, 0.0f),
                             view,
                             proj,
                             displaySize,
                             renderSize,
                             centerScreen,
                             depthAbs,
                             nullptr)) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImU32 fill = towerPlacementCanPlace_ ? IM_COL32(90, 255, 120, 85) : IM_COL32(255, 90, 90, 85);
        drawList->AddCircleFilled(centerScreen, 8.0f, fill, 24);
        drawList->AddCircle(centerScreen, 8.0f, col, 24, 2.0f);
    }
}

const PlayLevelScene::EnemyArchetype* PlayLevelScene::findEnemyArchetype(const std::string& enemyId) const {
    auto it = enemyArchetypes_.find(enemyId);
    if (it != enemyArchetypes_.end()) {
        return &it->second;
    }

    auto fallbackIt = enemyArchetypes_.find(defaultEnemyId_);
    if (fallbackIt != enemyArchetypes_.end()) {
        return &fallbackIt->second;
    }

    return nullptr;
}

void PlayLevelScene::applyPendingGameplayCommands() {
    for (const GameplayCommand& cmd : pendingCommands_) {
        switch (cmd.type) {
        case GameplayCommandType::SpendMoney:
            requestSpendMoney(cmd.amount);
            break;
        case GameplayCommandType::DamageBase:
            requestDamageBase(cmd.amount);
            break;
        case GameplayCommandType::StartWave:
            requestStartWave();
            break;
        }
    }
    pendingCommands_.clear();
}

bool PlayLevelScene::updateRouteFromWorld() {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return false;
    }

    const std::vector<glm::vec3>& rendererRoute = worldRenderer_->routePoints();
    if (rendererRoute.size() < 2) {
        return false;
    }

    if (rendererRoute == routePoints_ && routeTotalLength_ > 0.0f) {
        return true;
    }

    routePoints_ = rendererRoute;
    routeSegmentLengths_.clear();
    routeTotalLength_ = 0.0f;
    routeSegmentLengths_.reserve(routePoints_.size() - 1);

    for (std::size_t i = 0; i + 1 < routePoints_.size(); ++i) {
        const float segmentLen = glm::distance(routePoints_[i], routePoints_[i + 1]);
        routeSegmentLengths_.push_back(segmentLen);
        routeTotalLength_ += segmentLen;
    }

    return routeTotalLength_ > 0.0f;
}

glm::vec3 PlayLevelScene::sampleRoutePosition(float distanceAlongPath) const {
    if (routePoints_.empty()) {
        return glm::vec3(0.0f);
    }
    if (routePoints_.size() == 1 || routeTotalLength_ <= 0.0f) {
        return routePoints_.front();
    }

    float remaining = std::clamp(distanceAlongPath, 0.0f, routeTotalLength_);
    for (std::size_t i = 0; i + 1 < routePoints_.size(); ++i) {
        const float segmentLen = routeSegmentLengths_[i];
        if (remaining <= segmentLen || i + 2 == routePoints_.size()) {
            const float t = segmentLen > 1e-5f ? (remaining / segmentLen) : 0.0f;
            return glm::mix(routePoints_[i], routePoints_[i + 1], t);
        }
        remaining -= segmentLen;
    }

    return routePoints_.back();
}

float PlayLevelScene::sampleRouteYaw(float distanceAlongPath) const {
    const float probeAhead = 0.2f;
    const glm::vec3 a = sampleRoutePosition(distanceAlongPath);
    const glm::vec3 b = sampleRoutePosition(std::min(routeTotalLength_, distanceAlongPath + probeAhead));
    glm::vec3 dir = b - a;
    dir.y = 0.0f;
    if (glm::dot(dir, dir) < 1e-6f) {
        return 0.0f;
    }
    dir = glm::normalize(dir);
    return std::atan2(dir.x, dir.z);
}

void PlayLevelScene::syncEnemyInstanceTransforms() {
    syncTowerInstanceTransforms();
}

bool PlayLevelScene::pickModelAtScreen(float screenX, float screenY, DebugSelection& outSelection) const {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return false;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return false;
    }

    const float ndcX = (2.0f * screenX) / displaySize.x - 1.0f;
    const float ndcY = (2.0f * screenY) / displaySize.y - 1.0f;

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;

    const glm::mat4 view = buildViewMatrix();
    const glm::mat4 invVP = glm::inverse(proj * view);

    const glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec4 nearWorld = invVP * nearClip;
    glm::vec4 farWorld = invVP * farClip;
    if (std::abs(nearWorld.w) < 1e-6f || std::abs(farWorld.w) < 1e-6f) {
        return false;
    }
    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;

    const glm::vec3 rayOrigin = camPos_;
    glm::vec3 rayDir = glm::vec3(farWorld - nearWorld);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return false;
    }
    rayDir = glm::normalize(rayDir);

    WorldPickOptions pickOptions{};
    pickOptions.staticRadiusScale = kPickStaticRadiusScale;
    pickOptions.staticRadiusPadding = kPickStaticRadiusPadding;
    pickOptions.staticMinRadius = kPickStaticMinRadius;
    pickOptions.dynamicRadiusScale = kPickDynamicRadiusScale;
    pickOptions.dynamicRadiusPadding = kPickDynamicRadiusPadding;
    pickOptions.dynamicMinRadius = kPickDynamicMinRadius;

    WorldPickHit hit{};
    if (!worldRenderer_->pickModel(rayOrigin, rayDir, hit, pickOptions)) {
        return false;
    }

    outSelection.valid = true;
    outSelection.group = hit.group;
    outSelection.label = hit.label;
    outSelection.meshIndex = hit.meshIndex;
    outSelection.nodeIndex = hit.nodeIndex;
    outSelection.skinIndex = hit.skinIndex;
    outSelection.instanceIndex = hit.instanceIndex;
    outSelection.distance = hit.distance;
    outSelection.hitPosition = hit.worldPosition;
    outSelection.hitNormal = hit.worldNormal;
    return true;
}

bool PlayLevelScene::pickModelAtCursor(DebugSelection& outSelection) const {
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return false;
    }
    return pickModelAtScreen(mousePos.x, mousePos.y, outSelection);
}

void PlayLevelScene::updateDebugPickFromMouse() {
    if (selectedTowerLoadoutIndex_ >= 0) {
        return;
    }

    if (!debugPickEnabled_) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }

    DebugSelection selection{};
    const auto isSelectableSelection = [](const DebugSelection& s) {
        if (s.instanceIndex >= 0) {
            return true;
        }
        return s.group.rfind("tower", 0) == 0;
    };

    // Hover path already applies dynamic proxy fallback; prefer it for click selection.
    if (hoverSelection_.valid && isSelectableSelection(hoverSelection_)) {
        debugSelection_ = hoverSelection_;
        selectedInstanceIndex_ = hoverSelection_.instanceIndex;
        debugPickStatus_ = "picked " + hoverSelection_.group + " / " + hoverSelection_.label;
        return;
    }

    if (pickModelAtCursor(selection) && isSelectableSelection(selection)) {
        debugSelection_ = selection;
        selectedInstanceIndex_ = selection.instanceIndex;
        debugPickStatus_ = "picked " + selection.group + " / " + selection.label;
    } else if (selection.valid) {
        debugSelection_ = {};
        selectedInstanceIndex_ = -1;
        debugPickStatus_ = "hit non-selectable model";
    } else {
        selectedInstanceIndex_ = -1;
        debugPickStatus_ = "no model hit at cursor";
    }
}

bool PlayLevelScene::updateDebugHoverFromMouse() {
    const int previousHoveredInstanceIndex = hoveredInstanceIndex_;
    const bool previousHoverValid = hoverSelection_.valid;

    hoveredInstanceIndex_ = -1;
    hoverSelection_ = {};

    if (!debugPickEnabled_) {
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    const ImVec2 mousePos = io.MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    DebugSelection hover{};
    if (pickModelAtCursor(hover) && hover.instanceIndex >= 0) {
        hoverSelection_ = hover;
        hoveredInstanceIndex_ = hover.instanceIndex;
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    if (worldRenderer_ && worldRenderer_->isLoaded()) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 renderSize(
            (lastRenderExtent_.width > 0) ? static_cast<float>(lastRenderExtent_.width) : displaySize.x,
            (lastRenderExtent_.height > 0) ? static_cast<float>(lastRenderExtent_.height) : displaySize.y);
        const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
        glm::mat4 proj = glm::perspective(kDebugOverlayFovRadians, aspect, 0.05f, 2000.0f);
        proj[1][1] *= -1.0f;
        const glm::mat4 view = buildViewMatrix();

        WorldPickOptions pickOptions{};
        pickOptions.staticRadiusScale = kPickStaticRadiusScale;
        pickOptions.staticRadiusPadding = kPickStaticRadiusPadding;
        pickOptions.staticMinRadius = kPickStaticMinRadius;
        pickOptions.dynamicRadiusScale = kPickDynamicRadiusScale;
        pickOptions.dynamicRadiusPadding = kPickDynamicRadiusPadding;
        pickOptions.dynamicMinRadius = kPickDynamicMinRadius;

        const std::vector<WorldPickDebugSphere> spheres = worldRenderer_->buildDynamicPickDebugSpheres(pickOptions);
        const float focalPixels = displaySize.y / (2.0f * std::tan(kDebugOverlayFovRadians * 0.5f));

        int bestInstance = -1;
        float bestNormalizedDist2 = std::numeric_limits<float>::max();
        glm::vec3 bestCenter{0.0f};
        std::string bestGroup;
        std::string bestLabel;

        for (const WorldPickDebugSphere& sphere : spheres) {
            ImVec2 screenPos{};
            float depthAbs = 0.0f;
            if (!projectWorldToScreen(sphere.center, view, proj, displaySize, renderSize, screenPos, depthAbs, nullptr)) {
                continue;
            }

            const float radiusPixels = sphere.radius * focalPixels / std::max(0.1f, depthAbs);
            if (!std::isfinite(radiusPixels) || radiusPixels < 1.0f) {
                continue;
            }

            const float dx = mousePos.x - screenPos.x;
            const float dy = mousePos.y - screenPos.y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 > radiusPixels * radiusPixels) {
                continue;
            }

            const float normalizedDist2 = dist2 / std::max(1.0f, radiusPixels * radiusPixels);
            if (normalizedDist2 < bestNormalizedDist2) {
                bestNormalizedDist2 = normalizedDist2;
                bestInstance = sphere.instanceIndex;
                bestCenter = sphere.center;
                bestGroup = sphere.group;
                bestLabel = sphere.label;
            }
        }

        if (bestInstance >= 0) {
            hoverSelection_.valid = true;
            hoverSelection_.instanceIndex = bestInstance;
            hoverSelection_.group = bestGroup;
            hoverSelection_.label = bestLabel;
            hoverSelection_.hitPosition = bestCenter;
            hoveredInstanceIndex_ = bestInstance;
        }
    }

    return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
}

void PlayLevelScene::drawDebugPickSpheresOverlay() const {
    debugOverlayStats_ = {};

    if (!debugDrawPickSpheres_ || !worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return;
    }
    const ImVec2 renderSize(
        (lastRenderExtent_.width > 0) ? static_cast<float>(lastRenderExtent_.width) : displaySize.x,
        (lastRenderExtent_.height > 0) ? static_cast<float>(lastRenderExtent_.height) : displaySize.y);
    debugOverlayStats_.displayWidth = displaySize.x;
    debugOverlayStats_.displayHeight = displaySize.y;
    debugOverlayStats_.renderWidth = renderSize.x;
    debugOverlayStats_.renderHeight = renderSize.y;

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(kDebugOverlayFovRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;
    const glm::mat4 view = buildViewMatrix();

    WorldPickOptions pickOptions{};
    pickOptions.staticRadiusScale = kPickStaticRadiusScale;
    pickOptions.staticRadiusPadding = kPickStaticRadiusPadding;
    pickOptions.staticMinRadius = kPickStaticMinRadius;
    pickOptions.dynamicRadiusScale = kPickDynamicRadiusScale;
    pickOptions.dynamicRadiusPadding = kPickDynamicRadiusPadding;
    pickOptions.dynamicMinRadius = kPickDynamicMinRadius;

    const std::vector<WorldPickDebugSphere> spheres = worldRenderer_->buildDynamicPickDebugSpheres(pickOptions);
    debugOverlayStats_.sphereTotal = static_cast<int>(spheres.size());
    if (spheres.empty()) {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const float focalPixels = displaySize.y / (2.0f * std::tan(kDebugOverlayFovRadians * 0.5f));

    for (const WorldPickDebugSphere& sphere : spheres) {
        const glm::vec3 center = sphere.center;
        const float sphereRadius = sphere.radius;

        ImVec2 screenPos{};
        float depthAbs = 0.0f;
        ProjectionRejectReason rejectReason = ProjectionRejectReason::None;
        if (!projectWorldToScreen(center, view, proj, displaySize, renderSize, screenPos, depthAbs, &rejectReason)) {
            if (rejectReason == ProjectionRejectReason::BehindCamera) {
                ++debugOverlayStats_.rejectBehindCamera;
            } else if (rejectReason == ProjectionRejectReason::ClipW) {
                ++debugOverlayStats_.rejectClipW;
            } else if (rejectReason == ProjectionRejectReason::NdcZ) {
                ++debugOverlayStats_.rejectNdcZ;
            }
            continue;
        }

        const float radiusPixels = sphereRadius * focalPixels / std::max(0.1f, depthAbs);
        if (!std::isfinite(radiusPixels) || radiusPixels < 1.0f) {
            ++debugOverlayStats_.rejectRadius;
            continue;
        }

        const bool hovered = (sphere.instanceIndex == hoveredInstanceIndex_);
        const ImU32 col = hovered ? IM_COL32(255, 220, 64, 235) : IM_COL32(64, 200, 255, 180);
        drawList->AddCircle(screenPos, radiusPixels, col, 42, hovered ? 2.5f : 1.5f);
        ++debugOverlayStats_.sphereDrawn;
    }
}

void PlayLevelScene::drawHoverHighlightOverlay() const {
    if (!debugDrawHoverHighlight_) {
        return;
    }
    if (hoveredInstanceIndex_ < 0 || !worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return;
    }

    const ImVec2 renderSize(
        (lastRenderExtent_.width > 0) ? static_cast<float>(lastRenderExtent_.width) : displaySize.x,
        (lastRenderExtent_.height > 0) ? static_cast<float>(lastRenderExtent_.height) : displaySize.y);

    WorldPickOptions pickOptions{};
    pickOptions.staticRadiusScale = kPickStaticRadiusScale;
    pickOptions.staticRadiusPadding = kPickStaticRadiusPadding;
    pickOptions.staticMinRadius = kPickStaticMinRadius;
    pickOptions.dynamicRadiusScale = kPickDynamicRadiusScale;
    pickOptions.dynamicRadiusPadding = kPickDynamicRadiusPadding;
    pickOptions.dynamicMinRadius = kPickDynamicMinRadius;

    const std::vector<WorldPickDebugSphere> spheres = worldRenderer_->buildDynamicPickDebugSpheres(pickOptions);
    const WorldPickDebugSphere* hoveredSphere = nullptr;
    for (const WorldPickDebugSphere& sphere : spheres) {
        if (sphere.instanceIndex == hoveredInstanceIndex_) {
            hoveredSphere = &sphere;
            break;
        }
    }
    if (!hoveredSphere) {
        debugOverlayStats_.hoveredSphereFound = 0;
        return;
    }
    debugOverlayStats_.hoveredSphereFound = 1;

    const glm::vec3 center = hoveredSphere->center;
    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(kDebugOverlayFovRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;
    const glm::mat4 view = buildViewMatrix();

    ImVec2 screenPos{};
    float depthAbs = 0.0f;
    ProjectionRejectReason rejectReason = ProjectionRejectReason::None;
    if (!projectWorldToScreen(center, view, proj, displaySize, renderSize, screenPos, depthAbs, &rejectReason)) {
        debugOverlayStats_.hoveredRejectReason = static_cast<int>(rejectReason);
        return;
    }
    debugOverlayStats_.hoveredRejectReason = static_cast<int>(ProjectionRejectReason::None);
    debugOverlayStats_.hoveredDepth = depthAbs;

    const float focalPixels = displaySize.y / (2.0f * std::tan(kDebugOverlayFovRadians * 0.5f));
    const float baseRadius = hoveredSphere->radius;
    float radiusPixels = baseRadius * focalPixels / std::max(0.1f, depthAbs);
    if (!std::isfinite(radiusPixels) || radiusPixels < 4.0f) {
        debugOverlayStats_.hoveredRadiusPixels = radiusPixels;
        return;
    }
    radiusPixels = std::max(6.0f, radiusPixels);
    debugOverlayStats_.hoveredRadiusPixels = radiusPixels;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImU32 outerColor = IM_COL32(255, 228, 96, 230);
    const ImU32 innerColor = IM_COL32(255, 244, 170, 180);
    drawList->AddCircle(screenPos, radiusPixels * 1.08f, outerColor, 48, 3.0f);
    drawList->AddCircle(screenPos, radiusPixels * 0.92f, innerColor, 40, 1.6f);
}

std::string PlayLevelScene::validateStartWaveRequest() const {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }
    if (gameplayState_.waveCountdownActive) {
        return "countdown already active";
    }
    if (gameplayState_.waveInProgress) {
        return "wave is still spawning";
    }
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return "world is still loading";
    }
    if (!worldRenderer_->hasAnimatedEntityTemplate()) {
        return "enemy model template not loaded";
    }
    if (routePoints_.size() < 2 || routeTotalLength_ <= 0.0f) {
        return "path markers not found (need Start/Waypoint_X/End)";
    }
    if (waveDefinitions_.empty()) {
        return "no wave definitions loaded";
    }
    if (gameplayState_.currentWave < 1 || gameplayState_.currentWave > static_cast<int>(waveDefinitions_.size())) {
        return "no definition for current wave";
    }
    if (waveDefinitions_[gameplayState_.currentWave - 1].spawns.empty()) {
        return "current wave has no spawn entries";
    }
    return {};
}

void PlayLevelScene::updateWaveSimulation(float dt) {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }

    if (gameplayState_.waveCountdownActive) {
        gameplayState_.waveCountdownRemainingSeconds = std::max(0.0f, gameplayState_.waveCountdownRemainingSeconds - dt);
        if (gameplayState_.waveCountdownRemainingSeconds <= 0.0f) {
            beginWaveSpawning();
        }
    }

    if (gameplayState_.waveInProgress) {
        if (activeWaveIndex_ < 0 || activeWaveIndex_ >= static_cast<int>(waveDefinitions_.size())) {
            completeWaveAndAdvance();
        } else {
            const WaveDefinition& waveDef = waveDefinitions_[activeWaveIndex_];
            if (waveDef.spawns.empty()) {
                completeWaveAndAdvance();
            } else {
                gameplayState_.waveRoundRemainingSeconds =
                    std::max(0.0f, gameplayState_.waveRoundRemainingSeconds - dt);
                gameplayState_.spawnAccumulatorSeconds += dt;

                while (gameplayState_.enemiesToSpawn > 0 && activeWaveSpawnIndex_ < static_cast<int>(waveDef.spawns.size())) {
                    const WaveSpawnDefinition& spawnDef = waveDef.spawns[activeWaveSpawnIndex_];
                    if (gameplayState_.spawnAccumulatorSeconds < spawnDef.spawnIntervalSeconds) {
                        break;
                    }

                    gameplayState_.spawnAccumulatorSeconds -= spawnDef.spawnIntervalSeconds;
                    gameplayState_.enemiesToSpawn -= 1;

                    const EnemyArchetype* archetype = findEnemyArchetype(spawnDef.enemyId);
                    const float moveSpeed = archetype ? archetype->moveSpeed : 1.0f;
                    const float renderScale = archetype ? archetype->renderScale : 1.0f;
                    const float baseDamage = archetype ? archetype->baseDamage : 5.0f;
                    const float facingYawOffsetDegrees = archetype ? archetype->facingYawOffsetDegrees : 0.0f;

                    activeEnemies_.push_back(ActiveEnemy{
                        spawnDef.enemyId,
                        0.0f,
                        std::max(0.05f, moveSpeed),
                        std::max(1.0f, baseDamage),
                        std::max(0.01f, renderScale),
                        facingYawOffsetDegrees,
                    });

                    ++activeWaveSpawnedFromCurrent_;
                    if (activeWaveSpawnedFromCurrent_ >= std::max(1, spawnDef.count)) {
                        activeWaveSpawnedFromCurrent_ = 0;
                        ++activeWaveSpawnIndex_;
                    }
                }

                if (gameplayState_.enemiesToSpawn == 0 || gameplayState_.waveRoundRemainingSeconds <= 0.0f) {
                    completeWaveAndAdvance();
                }
            }
        }
    }

    std::size_t writeIndex = 0;
    for (std::size_t i = 0; i < activeEnemies_.size(); ++i) {
        ActiveEnemy enemy = activeEnemies_[i];
        enemy.distanceAlongPath += std::max(0.05f, enemy.moveSpeed) * dt;

        if (enemy.distanceAlongPath >= routeTotalLength_) {
            requestDamageBase(std::max(1.0f, enemy.baseDamage));
            continue;
        }

        activeEnemies_[writeIndex++] = enemy;
    }
    activeEnemies_.resize(writeIndex);

    gameplayState_.enemiesAlive = static_cast<int>(activeEnemies_.size());

    if (gameplayState_.matchStatus != MatchStatus::Running) {
        gameplayState_.waveInProgress = false;
        gameplayState_.waveCountdownActive = false;
        activeWaveIndex_ = -1;
        activeEnemies_.clear();
        return;
    }

    if (!gameplayState_.waveInProgress && !gameplayState_.waveCountdownActive) {
        const int waveCount = static_cast<int>(waveDefinitions_.size());
        if (gameplayState_.currentWave > waveCount) {
            if (activeEnemies_.empty()) {
                gameplayState_.matchStatus = MatchStatus::Victory;
            }
            return;
        }

        if (activeEnemies_.empty()) {
            beginWaveCountdown();
        }
    }
}

void PlayLevelScene::registerLuaGameplayApi() {
    if (!L_) {
        return;
    }

    lua_newtable(L_);
    const int entityTable = lua_gettop(L_);

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const char* scriptPath = luaL_checkstring(L, 1);

            PlayLevelScene::EnemyArchetype archetype;
            if (!self->parseEnemyArchetypeScript(scriptPath, archetype)) {
                lua_pushnil(L);
                lua_pushfstring(L, "Entity.Load failed for '%s'", scriptPath);
                return 2;
            }

            if (self->enemyArchetypes_.empty()) {
                self->defaultEnemyId_ = archetype.id;
            }
            self->enemyArchetypes_[archetype.id] = archetype;

            lua_newtable(L);
            lua_pushstring(L, archetype.id.c_str());
            lua_setfield(L, -2, "id");
            lua_pushstring(L, archetype.displayName.c_str());
            lua_setfield(L, -2, "displayName");
            lua_pushstring(L, archetype.modelPath.c_str());
            lua_setfield(L, -2, "modelPath");
            return 1;
        },
        1);
    lua_setfield(L_, entityTable, "Load");
    lua_setglobal(L_, "Entity");

    lua_newtable(L_);
    const int waveApiTable = lua_gettop(L_);

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->waveDefinitions_.clear();
            lua_pushboolean(L, 1);
            return 1;
        },
        1);
    lua_setfield(L_, waveApiTable, "Reset");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            luaL_checktype(L, 1, LUA_TTABLE);

            PlayLevelScene::WaveDefinition waveDef;
            int spawnEntriesTable = 1;

            lua_getfield(L, 1, "spawns");
            if (lua_istable(L, -1)) {
                spawnEntriesTable = lua_gettop(L);
            } else {
                lua_pop(L, 1);
            }

            lua_getfield(L, 1, "roundDurationSeconds");
            if (lua_isnumber(L, -1)) {
                waveDef.roundDurationSeconds = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            if (lua_gettop(L) >= 2 && lua_isnumber(L, 2)) {
                waveDef.roundDurationSeconds = static_cast<float>(lua_tonumber(L, 2));
            }

            const int entryCount = static_cast<int>(lua_rawlen(L, spawnEntriesTable));
            for (int i = 1; i <= entryCount; ++i) {
                lua_geti(L, spawnEntriesTable, i);
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    continue;
                }

                PlayLevelScene::WaveSpawnDefinition spawn;
                spawn.enemyId = self->defaultEnemyId_;
                if (const PlayLevelScene::EnemyArchetype* defaultArchetype = self->findEnemyArchetype(spawn.enemyId)) {
                    spawn.spawnIntervalSeconds = defaultArchetype->spawnIntervalSeconds;
                }

                lua_getfield(L, -1, "entity");
                if (lua_isstring(L, -1)) {
                    spawn.enemyId = lua_tostring(L, -1);
                } else if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "id");
                    if (lua_isstring(L, -1)) {
                        spawn.enemyId = lua_tostring(L, -1);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "enemyId");
                if (lua_isstring(L, -1)) {
                    spawn.enemyId = lua_tostring(L, -1);
                }
                lua_pop(L, 1);

                const PlayLevelScene::EnemyArchetype* archetype = self->findEnemyArchetype(spawn.enemyId);
                if (!archetype) {
                    spawn.enemyId = self->defaultEnemyId_;
                    archetype = self->findEnemyArchetype(spawn.enemyId);
                }

                lua_getfield(L, -1, "count");
                if (lua_isinteger(L, -1)) {
                    spawn.count = static_cast<int>(lua_tointeger(L, -1));
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "spawnIntervalSeconds");
                if (lua_isnumber(L, -1)) {
                    spawn.spawnIntervalSeconds = static_cast<float>(lua_tonumber(L, -1));
                } else if (archetype) {
                    spawn.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
                }
                lua_pop(L, 1);

                spawn.count = std::max(1, spawn.count);
                spawn.spawnIntervalSeconds = std::max(0.05f, spawn.spawnIntervalSeconds);

                waveDef.spawns.push_back(std::move(spawn));
                lua_pop(L, 1);
            }

            if (spawnEntriesTable != 1) {
                lua_pop(L, 1);
            }

            if (!waveDef.spawns.empty()) {
                waveDef.roundDurationSeconds = std::max(1.0f, waveDef.roundDurationSeconds);
                self->waveDefinitions_.push_back(std::move(waveDef));
                lua_pushboolean(L, 1);
                return 1;
            }

            lua_pushboolean(L, 0);
            lua_pushstring(L, "Wave.Register requires at least one spawn entry");
            return 2;
        },
        1);
    lua_setfield(L_, waveApiTable, "Register");
    lua_setglobal(L_, "Wave");

    lua_newtable(L_);
    const int gameplayTable = lua_gettop(L_);

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);

            lua_pushinteger(L, self->gameplayState_.baseHealth);
            lua_setfield(L, -2, "baseHealth");
            lua_pushinteger(L, self->gameplayState_.playerMoney);
            lua_setfield(L, -2, "playerMoney");
            lua_pushinteger(L, self->gameplayState_.currentWave);
            lua_setfield(L, -2, "currentWave");
            lua_pushboolean(L, self->gameplayState_.waveInProgress);
            lua_setfield(L, -2, "waveInProgress");
            lua_pushboolean(L, self->gameplayState_.waveCountdownActive);
            lua_setfield(L, -2, "waveCountdownActive");
            lua_pushinteger(L, self->gameplayState_.enemiesToSpawn);
            lua_setfield(L, -2, "enemiesToSpawn");
            lua_pushinteger(L, self->gameplayState_.enemiesAlive);
            lua_setfield(L, -2, "enemiesAlive");
            lua_pushinteger(L, self->gameplayState_.enemiesDefeated);
            lua_setfield(L, -2, "enemiesDefeated");
            lua_pushnumber(L, self->gameplayState_.waveCountdownRemainingSeconds);
            lua_setfield(L, -2, "waveCountdownRemainingSeconds");
            lua_pushnumber(L, self->gameplayState_.waveRoundDurationSeconds);
            lua_setfield(L, -2, "waveRoundDurationSeconds");
            lua_pushnumber(L, self->gameplayState_.waveRoundRemainingSeconds);
            lua_setfield(L, -2, "waveRoundRemainingSeconds");
            lua_pushinteger(L, static_cast<lua_Integer>(self->waveDefinitions_.size()));
            lua_setfield(L, -2, "waveCount");
            lua_pushinteger(L, static_cast<lua_Integer>(self->routePoints_.size()));
            lua_setfield(L, -2, "routePointCount");

            const bool hasRenderer = self->worldRenderer_ != nullptr;
            const bool worldLoaded = hasRenderer && self->worldRenderer_->isLoaded();
            const bool worldFailed = !hasRenderer || self->worldRenderer_->loadFailed();
            const bool worldLoading = hasRenderer && !worldLoaded && !worldFailed;

            lua_pushboolean(L, worldLoaded);
            lua_setfield(L, -2, "worldLoaded");
            lua_pushboolean(L, worldFailed);
            lua_setfield(L, -2, "worldFailed");
            lua_pushboolean(L, worldLoading);
            lua_setfield(L, -2, "worldLoading");

            lua_pushnumber(L, hasRenderer ? self->worldRenderer_->loadProgress() : 0.0f);
            lua_setfield(L, -2, "loadProgress");
            const std::string loadActivity = hasRenderer ? self->worldRenderer_->loadActivity() : std::string();
            lua_pushstring(L, loadActivity.c_str());
            lua_setfield(L, -2, "loadActivity");

            const std::string worldStatus = hasRenderer ? self->worldRenderer_->statusMessage() : self->loadStatus_;
            lua_pushstring(L, worldStatus.c_str());
            lua_setfield(L, -2, "worldStatus");

            lua_pushinteger(L, worldLoaded ? self->worldRenderer_->meshCount() : 0);
            lua_setfield(L, -2, "meshCount");
            lua_pushinteger(L, worldLoaded ? self->worldRenderer_->totalVertices() : 0);
            lua_setfield(L, -2, "vertexCount");
            lua_pushinteger(L, worldLoaded ? (self->worldRenderer_->totalIndices() / 3) : 0);
            lua_setfield(L, -2, "triCount");

            lua_pushboolean(L, worldLoaded && self->worldRenderer_->hasTemplateAnimation());
            lua_setfield(L, -2, "enemyAnimationLoaded");
            lua_pushstring(L, (worldLoaded && self->worldRenderer_->hasTemplateAnimation())
                                  ? self->worldRenderer_->templateAnimationName().c_str()
                                  : "none");
            lua_setfield(L, -2, "enemyAnimationName");

            lua_newtable(L);
            lua_pushnumber(L, self->camPos_.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->camPos_.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->camPos_.z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "cameraPosition");

            const char* status = "Running";
            switch (self->gameplayState_.matchStatus) {
            case MatchStatus::Running:
                status = "Running";
                break;
            case MatchStatus::Paused:
                status = "Paused";
                break;
            case MatchStatus::Victory:
                status = "Victory";
                break;
            case MatchStatus::Defeat:
                status = "Defeat";
                break;
            }
            lua_pushstring(L, status);
            lua_setfield(L, -2, "matchStatus");
            lua_pushinteger(L, self->selectedTowerLoadoutIndex_ + 1);
            lua_setfield(L, -2, "selectedTowerSlot");
            lua_pushinteger(L, static_cast<lua_Integer>(self->placedTowers_.size()));
            lua_setfield(L, -2, "placedTowerCount");
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getState");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);
            for (std::size_t i = 0; i < 5; ++i) {
                lua_newtable(L);
                const int slotIdx = static_cast<int>(i);
                const bool hasTower = slotIdx < static_cast<int>(self->towerLoadoutIds_.size());
                const PlayLevelScene::TowerArchetype* archetype =
                    hasTower ? self->findTowerArchetype(self->towerLoadoutIds_[slotIdx]) : nullptr;

                lua_pushinteger(L, static_cast<lua_Integer>(slotIdx + 1));
                lua_setfield(L, -2, "slot");
                lua_pushboolean(L, archetype != nullptr);
                lua_setfield(L, -2, "available");
                lua_pushboolean(L, self->selectedTowerLoadoutIndex_ == slotIdx);
                lua_setfield(L, -2, "selected");

                if (archetype) {
                    lua_pushstring(L, archetype->id.c_str());
                    lua_setfield(L, -2, "id");
                    lua_pushstring(L, archetype->displayName.c_str());
                    lua_setfield(L, -2, "displayName");
                    lua_pushinteger(L, archetype->cost);
                    lua_setfield(L, -2, "cost");
                    lua_pushnumber(L, archetype->attackRange);
                    lua_setfield(L, -2, "attackRange");
                    lua_pushstring(L, archetype->modelPath.c_str());
                    lua_setfield(L, -2, "modelPath");
                    lua_pushstring(L, archetype->previewImagePath.c_str());
                    lua_setfield(L, -2, "previewImagePath");
                    const std::string textureId = makeTowerIconTextureId(archetype->id);
                    lua_pushstring(L, textureId.c_str());
                    lua_setfield(L, -2, "previewTextureId");
                }

                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getTowerLoadout");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const int requestedSlot = static_cast<int>(luaL_checkinteger(L, 1));
            if (requestedSlot < 1 || requestedSlot > 5) {
                return pushCommandResult(L, false, "slot must be in range 1..5");
            }

            const int slotIdx = requestedSlot - 1;
            if (slotIdx >= static_cast<int>(self->towerLoadoutIds_.size())) {
                return pushCommandResult(L, false, "loadout slot is empty");
            }

            const PlayLevelScene::TowerArchetype* tower = self->findTowerArchetype(self->towerLoadoutIds_[slotIdx]);
            if (!tower) {
                return pushCommandResult(L, false, "tower definition not found");
            }

            self->selectedTowerLoadoutIndex_ = slotIdx;
            return pushCommandResult(L, true, "selected");
        },
        1);
    lua_setfield(L_, gameplayTable, "selectTowerSlot");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->selectedTowerLoadoutIndex_ = -1;
            self->towerPlacementHasHit_ = false;
            self->towerPlacementCanPlace_ = false;
            return pushCommandResult(L, true, "cancelled");
        },
        1);
    lua_setfield(L_, gameplayTable, "cancelTowerPlacement");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);

            const PlayLevelScene::TowerArchetype* tower = self->selectedTowerArchetype();
            const bool active = tower != nullptr;
            lua_pushboolean(L, active);
            lua_setfield(L, -2, "active");
            lua_pushinteger(L, self->selectedTowerLoadoutIndex_ + 1);
            lua_setfield(L, -2, "selectedSlot");
            lua_pushboolean(L, self->towerPlacementHasHit_);
            lua_setfield(L, -2, "hasHit");
            lua_pushboolean(L, self->towerPlacementCanPlace_);
            lua_setfield(L, -2, "canPlace");

            lua_newtable(L);
            lua_pushnumber(L, self->towerPlacementWorldPos_.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->towerPlacementWorldPos_.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->towerPlacementWorldPos_.z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "worldPos");

            if (tower) {
                lua_pushstring(L, tower->id.c_str());
                lua_setfield(L, -2, "towerId");
                lua_pushstring(L, tower->displayName.c_str());
                lua_setfield(L, -2, "displayName");
                lua_pushinteger(L, tower->cost);
                lua_setfield(L, -2, "cost");
                lua_pushnumber(L, tower->attackRange);
                lua_setfield(L, -2, "attackRange");

                std::string reason;
                if (!self->towerPlacementHasHit_) {
                    reason = "cursor is not over ground";
                } else {
                    reason = self->validateTowerPlacement(*tower, self->towerPlacementWorldPos_);
                }
                lua_pushstring(L, reason.c_str());
                lua_setfield(L, -2, "reason");
            }

            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getTowerPlacementState");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const float amount = static_cast<float>(luaL_checknumber(L, 1));
            if (amount <= 0) {
                return pushCommandResult(L, false, "amount must be > 0");
            }
            if (self->gameplayState_.matchStatus != MatchStatus::Running) {
                return pushCommandResult(L, false, "match is not running");
            }
            if (self->gameplayState_.playerMoney < amount) {
                return pushCommandResult(L, false, "insufficient funds");
            }

            self->pendingCommands_.push_back({GameplayCommandType::SpendMoney, amount});
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestSpendMoney");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const float amount = static_cast<float>(luaL_checknumber(L, 1));
            if (amount <= 0) {
                return pushCommandResult(L, false, "amount must be > 0");
            }
            if (self->gameplayState_.matchStatus != MatchStatus::Running) {
                return pushCommandResult(L, false, "match is not running");
            }

            self->pendingCommands_.push_back({GameplayCommandType::DamageBase, amount});
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestDamageBase");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const std::string reason = self->validateStartWaveRequest();
            if (!reason.empty()) {
                return pushCommandResult(L, false, reason.c_str());
            }

            self->pendingCommands_.push_back({GameplayCommandType::StartWave, 0});
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestStartWave");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->debugPickEnabled_ = lua_toboolean(L, 1) != 0;
            lua_pushboolean(L, self->debugPickEnabled_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "setDebugPickEnabled");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->debugPickEnabled_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getDebugPickEnabled");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->debugDrawPickSpheres_ = lua_toboolean(L, 1) != 0;
            lua_pushboolean(L, self->debugDrawPickSpheres_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "setDebugPickSpheresVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->debugDrawPickSpheres_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getDebugPickSpheresVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->debugDrawHoverHighlight_ = lua_toboolean(L, 1) != 0;
            lua_pushboolean(L, self->debugDrawHoverHighlight_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "setDebugHoverHighlightVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->debugDrawHoverHighlight_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getDebugHoverHighlightVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            PlayLevelScene::DebugSelection selection{};
            const bool hit = self->pickModelAtCursor(selection);
            const bool selectableHit = hit && (selection.instanceIndex >= 0 || selection.group.rfind("tower", 0) == 0);
            lua_newtable(L);
            lua_pushboolean(L, selectableHit);
            lua_setfield(L, -2, "hit");
            if (selectableHit) {
                self->debugSelection_ = selection;
                self->selectedInstanceIndex_ = selection.instanceIndex;
                self->debugPickStatus_ = "picked " + selection.group + " / " + selection.label;

                lua_pushstring(L, selection.group.c_str());
                lua_setfield(L, -2, "group");
                lua_pushstring(L, selection.label.c_str());
                lua_setfield(L, -2, "label");
                lua_pushinteger(L, selection.meshIndex);
                lua_setfield(L, -2, "meshIndex");
                lua_pushinteger(L, selection.nodeIndex);
                lua_setfield(L, -2, "nodeIndex");
                lua_pushinteger(L, selection.skinIndex);
                lua_setfield(L, -2, "skinIndex");
                lua_pushinteger(L, selection.instanceIndex);
                lua_setfield(L, -2, "instanceIndex");
                lua_pushnumber(L, selection.distance);
                lua_setfield(L, -2, "distance");

                lua_newtable(L);
                lua_pushnumber(L, selection.hitPosition.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, selection.hitPosition.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, selection.hitPosition.z);
                lua_setfield(L, -2, "z");
                lua_setfield(L, -2, "hitPosition");

                lua_newtable(L);
                lua_pushnumber(L, selection.hitNormal.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, selection.hitNormal.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, selection.hitNormal.z);
                lua_setfield(L, -2, "z");
                lua_setfield(L, -2, "hitNormal");
            } else if (hit) {
                self->debugSelection_ = {};
                self->selectedInstanceIndex_ = -1;
                self->debugPickStatus_ = "hit non-selectable model";
            } else {
                self->selectedInstanceIndex_ = -1;
                self->debugPickStatus_ = "no model hit at cursor";
            }
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "pickAtCursor");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);
            lua_pushboolean(L, self->debugSelection_.valid);
            lua_setfield(L, -2, "valid");
            lua_pushstring(L, self->debugPickStatus_.c_str());
            lua_setfield(L, -2, "status");
            lua_pushboolean(L, self->debugDrawPickSpheres_);
            lua_setfield(L, -2, "debugPickSpheresVisible");
            lua_pushboolean(L, self->debugDrawHoverHighlight_);
            lua_setfield(L, -2, "debugHoverHighlightVisible");

            lua_newtable(L);
            lua_pushinteger(L, self->debugOverlayStats_.sphereTotal);
            lua_setfield(L, -2, "sphereTotal");
            lua_pushinteger(L, self->debugOverlayStats_.sphereDrawn);
            lua_setfield(L, -2, "sphereDrawn");
            lua_pushinteger(L, self->debugOverlayStats_.rejectBehindCamera);
            lua_setfield(L, -2, "rejectBehindCamera");
            lua_pushinteger(L, self->debugOverlayStats_.rejectClipW);
            lua_setfield(L, -2, "rejectClipW");
            lua_pushinteger(L, self->debugOverlayStats_.rejectNdcZ);
            lua_setfield(L, -2, "rejectNdcZ");
            lua_pushinteger(L, self->debugOverlayStats_.rejectRadius);
            lua_setfield(L, -2, "rejectRadius");
            lua_pushinteger(L, self->debugOverlayStats_.hoveredSphereFound);
            lua_setfield(L, -2, "hoveredSphereFound");
            lua_pushinteger(L, self->debugOverlayStats_.hoveredRejectReason);
            lua_setfield(L, -2, "hoveredRejectReason");
            lua_pushnumber(L, self->debugOverlayStats_.hoveredDepth);
            lua_setfield(L, -2, "hoveredDepth");
            lua_pushnumber(L, self->debugOverlayStats_.hoveredRadiusPixels);
            lua_setfield(L, -2, "hoveredRadiusPixels");
            lua_pushnumber(L, self->debugOverlayStats_.displayWidth);
            lua_setfield(L, -2, "displayWidth");
            lua_pushnumber(L, self->debugOverlayStats_.displayHeight);
            lua_setfield(L, -2, "displayHeight");
            lua_pushnumber(L, self->debugOverlayStats_.renderWidth);
            lua_setfield(L, -2, "renderWidth");
            lua_pushnumber(L, self->debugOverlayStats_.renderHeight);
            lua_setfield(L, -2, "renderHeight");
            lua_pushnumber(L, self->camYaw_);
            lua_setfield(L, -2, "cameraYaw");
            lua_pushnumber(L, self->camPitch_);
            lua_setfield(L, -2, "cameraPitch");
            lua_setfield(L, -2, "overlayDebug");

            lua_newtable(L);
            lua_pushboolean(L, self->hoverSelection_.valid);
            lua_setfield(L, -2, "valid");
            lua_pushinteger(L, self->hoveredInstanceIndex_);
            lua_setfield(L, -2, "instanceIndex");
            if (self->hoverSelection_.valid) {
                lua_pushstring(L, self->hoverSelection_.group.c_str());
                lua_setfield(L, -2, "group");
                lua_pushstring(L, self->hoverSelection_.label.c_str());
                lua_setfield(L, -2, "label");
            }
            lua_setfield(L, -2, "hover");

            if (self->debugSelection_.valid) {
                const auto& s = self->debugSelection_;
                lua_pushstring(L, s.group.c_str());
                lua_setfield(L, -2, "group");
                lua_pushstring(L, s.label.c_str());
                lua_setfield(L, -2, "label");
                lua_pushinteger(L, s.meshIndex);
                lua_setfield(L, -2, "meshIndex");
                lua_pushinteger(L, s.nodeIndex);
                lua_setfield(L, -2, "nodeIndex");
                lua_pushinteger(L, s.skinIndex);
                lua_setfield(L, -2, "skinIndex");
                lua_pushinteger(L, s.instanceIndex);
                lua_setfield(L, -2, "instanceIndex");
                lua_pushnumber(L, s.distance);
                lua_setfield(L, -2, "distance");

                lua_newtable(L);
                lua_pushnumber(L, s.hitPosition.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, s.hitPosition.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, s.hitPosition.z);
                lua_setfield(L, -2, "z");
                lua_setfield(L, -2, "hitPosition");

                lua_newtable(L);
                lua_pushnumber(L, s.hitNormal.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, s.hitNormal.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, s.hitNormal.z);
                lua_setfield(L, -2, "z");
                lua_setfield(L, -2, "hitNormal");
            }

            lua_pushboolean(L, self->worldRenderer_ && self->worldRenderer_->hasTemplateAnimation());
            lua_setfield(L, -2, "enemyAnimationLoaded");
            if (self->worldRenderer_ && self->worldRenderer_->hasTemplateAnimation()) {
                lua_pushstring(L, self->worldRenderer_->templateAnimationName().c_str());
                lua_setfield(L, -2, "enemyAnimationName");

                const EnemyAnimationDebugInfo& dbg = self->worldRenderer_->templateAnimationDebugInfo();
                lua_newtable(L);
                lua_pushboolean(L, dbg.enabled);
                lua_setfield(L, -2, "enabled");
                lua_pushstring(L, dbg.clipName.c_str());
                lua_setfield(L, -2, "clipName");
                lua_pushinteger(L, dbg.selectedClipIndex);
                lua_setfield(L, -2, "selectedClipIndex");
                lua_pushinteger(L, dbg.clipCount);
                lua_setfield(L, -2, "clipCount");
                lua_pushboolean(L, dbg.compositeMode);
                lua_setfield(L, -2, "compositeMode");
                lua_pushinteger(L, dbg.compositeAppliedClips);
                lua_setfield(L, -2, "compositeAppliedClips");
                lua_pushnumber(L, dbg.timeSeconds);
                lua_setfield(L, -2, "timeSeconds");
                lua_pushnumber(L, dbg.durationSeconds);
                lua_setfield(L, -2, "durationSeconds");
                lua_pushinteger(L, dbg.trackCount);
                lua_setfield(L, -2, "trackCount");
                lua_pushinteger(L, dbg.keyCount);
                lua_setfield(L, -2, "keyCount");
                lua_pushinteger(L, dbg.keyIndex);
                lua_setfield(L, -2, "keyIndex");
                lua_pushinteger(L, dbg.nextKeyIndex);
                lua_setfield(L, -2, "nextKeyIndex");
                lua_pushnumber(L, dbg.keyTimeSeconds);
                lua_setfield(L, -2, "keyTimeSeconds");
                lua_pushnumber(L, dbg.nextKeyTimeSeconds);
                lua_setfield(L, -2, "nextKeyTimeSeconds");
                lua_pushnumber(L, dbg.segmentAlpha);
                lua_setfield(L, -2, "segmentAlpha");
                lua_pushboolean(L, dbg.stepInterpolation);
                lua_setfield(L, -2, "stepInterpolation");
                lua_setfield(L, -2, "animationDebug");
            }
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getDebugSelection");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->debugSelection_ = {};
            self->selectedInstanceIndex_ = -1;
            self->debugPickStatus_ = "selection cleared";
            return 0;
        },
        1);
    lua_setfield(L_, gameplayTable, "clearDebugSelection");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);
            if (!self->worldRenderer_ || !self->worldRenderer_->hasTemplateAnimation()) {
                lua_pushinteger(L, 0);
                lua_setfield(L, -2, "count");
                lua_pushinteger(L, -1);
                lua_setfield(L, -2, "activeIndex");
                return 1;
            }

            const std::vector<std::string> names = self->worldRenderer_->templateAnimationClipNames();
            lua_pushinteger(L, static_cast<lua_Integer>(names.size()));
            lua_setfield(L, -2, "count");
            lua_pushinteger(L, self->worldRenderer_->activeTemplateAnimationClipIndex());
            lua_setfield(L, -2, "activeIndex");

            lua_newtable(L);
            for (std::size_t i = 0; i < names.size(); ++i) {
                lua_pushstring(L, names[i].c_str());
                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(L, -2, "names");
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getAnimationClips");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            if (!self->worldRenderer_) {
                return pushCommandResult(L, false, "renderer not ready");
            }

            bool ok = false;
            if (lua_isinteger(L, 1)) {
                const int idx = static_cast<int>(lua_tointeger(L, 1));
                ok = self->worldRenderer_->setActiveTemplateAnimationClipByIndex(idx);
            } else if (lua_isstring(L, 1)) {
                const std::string name = lua_tostring(L, 1);
                ok = self->worldRenderer_->setActiveTemplateAnimationClipByName(name);
            } else {
                return pushCommandResult(L, false, "expected clip index or clip name");
            }

            if (!ok) {
                return pushCommandResult(L, false, "clip not found");
            }
            return pushCommandResult(L, true, "selected");
        },
        1);
    lua_setfield(L_, gameplayTable, "setAnimationClip");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            if (!self->worldRenderer_) {
                return pushCommandResult(L, false, "renderer not ready");
            }

            const bool enabled = lua_toboolean(L, 1) != 0;
            self->worldRenderer_->setCompositeTemplateAnimationMode(enabled);
            return pushCommandResult(L, true, enabled ? "composite on" : "composite off");
        },
        1);
    lua_setfield(L_, gameplayTable, "setCompositeAnimationMode");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->worldRenderer_ && self->worldRenderer_->compositeTemplateAnimationMode());
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getCompositeAnimationMode");

    lua_setglobal(L_, "Gameplay");
}
