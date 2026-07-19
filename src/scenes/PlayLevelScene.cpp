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
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>


PlayLevelScene::PlayLevelScene() = default;
PlayLevelScene::~PlayLevelScene() = default;

namespace {

constexpr float kPreWaveCountdownSeconds = 5.0f;
constexpr float kDefaultWaveRoundDurationSeconds = 30.0f;

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
    debugPickEnabled_ = true;
    debugPickStatus_ = "click in world to inspect";
    selectedMapAssetPath_ = state.activeLevelAssetPath;
    selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
    worldAssetSpec_ = {};

    loadLevelDefinition(state);

    if (!loadEnemyArchetype("assets/models/enemy/goblin1.enemy.lua")) {
        spdlog::warn("PlayLevelScene: using built-in enemy defaults because goblin archetype failed to load.");
        EnemyArchetype fallback{};
        enemyArchetypes_[fallback.id] = fallback;
        defaultEnemyId_ = fallback.id;
    }
    if (!loadWaveDefinitions(selectedWavesScriptPath_)) {
        spdlog::warn("PlayLevelScene: using fallback wave definition because {} failed to load.",
                     selectedWavesScriptPath_);
    }

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
        syncEnemyInstanceTransforms();
        updateCamera(dt);
        updateDebugPickFromMouse();
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

    const std::filesystem::path scriptPath = "assets/scenes/PlayLevel.level.lua";
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

    lua_getfield(L_, -1, "mapAssetPath");
    if (lua_isstring(L_, -1)) {
        const std::string rawPath = lua_tostring(L_, -1);
        if (!rawPath.empty()) {
            selectedMapAssetPath_ = rawPath;
        }
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "wavesScriptPath");
    if (lua_isstring(L_, -1)) {
        const std::string rawPath = lua_tostring(L_, -1);
        if (!rawPath.empty()) {
            selectedWavesScriptPath_ = rawPath;
        }
    }
    lua_pop(L_, 1);

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

bool PlayLevelScene::pickModelAtScreen(float screenX, float screenY, DebugSelection& outSelection) const {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return false;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return false;
    }

    const float ndcX = (2.0f * screenX) / displaySize.x - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenY) / displaySize.y;

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

    const glm::vec3 rayOrigin = glm::vec3(nearWorld);
    glm::vec3 rayDir = glm::vec3(farWorld - nearWorld);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return false;
    }
    rayDir = glm::normalize(rayDir);

    WorldPickHit hit{};
    if (!worldRenderer_->pickModel(rayOrigin, rayDir, hit)) {
        return false;
    }

    outSelection.valid = true;
    outSelection.group = hit.group;
    outSelection.label = hit.label;
    outSelection.meshIndex = hit.meshIndex;
    outSelection.nodeIndex = hit.nodeIndex;
    outSelection.skinIndex = hit.skinIndex;
    outSelection.enemyInstanceIndex = hit.enemyInstanceIndex;
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
    if (pickModelAtCursor(selection)) {
        debugSelection_ = selection;
        debugPickStatus_ = "picked " + selection.group + " / " + selection.label;
    } else {
        debugPickStatus_ = "no model hit at cursor";
    }
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
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getState");

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
            PlayLevelScene::DebugSelection selection{};
            const bool hit = self->pickModelAtCursor(selection);
            lua_newtable(L);
            lua_pushboolean(L, hit);
            lua_setfield(L, -2, "hit");
            if (hit) {
                self->debugSelection_ = selection;
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
                lua_pushinteger(L, selection.enemyInstanceIndex);
                lua_setfield(L, -2, "enemyInstanceIndex");
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
            } else {
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
                lua_pushinteger(L, s.enemyInstanceIndex);
                lua_setfield(L, -2, "enemyInstanceIndex");
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
