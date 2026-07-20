#include "scenes/PlayLevelScene.hpp"

#include "LuaStateBootstrap.hpp"
#include "VulkanContext.hpp"
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

constexpr float kDebugOverlayFovRadians = glm::radians(60.0f);
constexpr int kTowerPoolPlacementsPerType = 32;
constexpr float kTowerHiddenY = -10000.0f;
constexpr float kProjectileHitRadius = 0.45f;
constexpr float kProjectileLifetimeSeconds = 2.0f;

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

glm::mat4 PlayLevelScene::buildViewMatrix() const {
    return cameraController_.buildViewMatrix();
}

void PlayLevelScene::updateCamera(float dt) {
    cameraController_.update(dt);
}

// ─── scene lifecycle ──────────────────────────────────────────────────────────

void PlayLevelScene::onEnter(SceneSharedState& state) {
    // Reset camera
    cameraController_.reset();

    LuaStateBootstrap::initializeEngineState(L_, state.vulkanContext);
    registerLuaGameplayApi();

    gameplayState_.resetForNewRun();
    towerArchetypes_.clear();
    towerTemplatePrototypeById_.clear();
    projectileTemplatePrototypeByTowerId_.clear();
    towerLoadoutIds_.clear();
    towerPoolGroupsById_.clear();
    towerGhostGroupById_.clear();
    towerPlacementController_.reset();
    placedTowers_.clear();
    activeProjectiles_.clear();
    nextEnemyRuntimeId_ = 1;
    pendingCommands_.clear();
    activeEnemies_.clear();
    enemyArchetypes_.clear();
    waveController_.clearAll();
    routeController_.clear();
    selectedEnemyRuntimeId_ = 0;
    pickingController_.reset();
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

    if (!waveController_.hasDefinitions() && !selectedWavesScriptPath_.empty() &&
        !loadWaveDefinitions(selectedWavesScriptPath_)) {
        spdlog::warn("PlayLevelScene: using fallback wave definition because {} failed to load.", selectedWavesScriptPath_);
    }

    if (!waveController_.hasDefinitions()) {
        PlayLevelWaveController::WaveDefinition fallback;
        fallback.spawns.push_back(PlayLevelWaveController::WaveSpawnDefinition{});
        waveController_.definitionsMutable().push_back(std::move(fallback));
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

        auto& poolGroups = towerPoolGroupsById_[towerId];
        poolGroups.reserve(kTowerPoolPlacementsPerType);
        for (int i = 0; i < kTowerPoolPlacementsPerType; ++i) {
            const std::string poolGroup = "tower_pool:" + towerId + ":" + std::to_string(i);
            poolGroups.push_back(poolGroup);
        }

        WorldTemplateModelSpec towerTemplate;
        towerTemplate.id = towerId;
        towerTemplate.modelPath = tower.modelPath;
        towerTemplatePrototypeById_[towerId] = static_cast<int>(worldAssetSpec_.towerTemplateModels.size());
        worldAssetSpec_.towerTemplateModels.push_back(std::move(towerTemplate));

        if (!tower.projectileModelPath.empty()) {
            WorldTemplateModelSpec projectileTemplate;
            projectileTemplate.id = "projectile:" + towerId;
            projectileTemplate.modelPath = tower.projectileModelPath;
            projectileTemplatePrototypeByTowerId_[towerId] = static_cast<int>(worldAssetSpec_.towerTemplateModels.size());
            worldAssetSpec_.towerTemplateModels.push_back(std::move(projectileTemplate));
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

void PlayLevelScene::render(SceneSharedState& state, float dt) {

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
        const bool hoverChanged = pickingController_.updateHoverFromMouse(
            worldRenderer_.get(), buildViewMatrix(), cameraController_.position(), lastRenderExtent_);
        if (hoverChanged) {
            // Re-apply instance transforms so hover visual feedback is in the same frame as hover detection.
            syncTowerInstanceTransforms();
            syncPlacedTowerModels();
        }
        pickingController_.updateSelectionFromMouse(
            worldRenderer_.get(), buildViewMatrix(), cameraController_.position(), towerPlacementController_.hasActiveSelection());
        const int selectedInstanceIndex = pickingController_.selectedInstanceIndex();
        selectedEnemyRuntimeId_ =
            (selectedInstanceIndex >= 0 && selectedInstanceIndex < static_cast<int>(activeEnemies_.size()))
                ? activeEnemies_[static_cast<std::size_t>(selectedInstanceIndex)].runtimeId
                : 0;
        worldRenderer_->setHighlightedInstances(
            pickingController_.hoveredInstanceIndex(), pickingController_.selectedInstanceIndex());
    }

    luaOnRender(state, scriptRef_, dt);

    if (isLoaded) {
        drawTowerPlacementOverlay();
        pickingController_.drawPickSpheresOverlay(worldRenderer_.get(), buildViewMatrix(), lastRenderExtent_);
    }
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
    return waveController_.beginWaveCountdown(gameplayState_,
                                              worldRenderer_ && worldRenderer_->isLoaded(),
                                              worldRenderer_ && worldRenderer_->hasAnimatedEntityTemplate(),
                                              routeController_.hasValidRoute());
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

    if (!waveController_.hasDefinitions() && !selectedWavesScriptPath_.empty()) {
        loadWaveDefinitions(selectedWavesScriptPath_);
    }

    publishLevelUiTextures(L_, worldAssetSpec_);

    lua_pop(L_, 1);
    return true;
}

bool PlayLevelScene::loadWaveDefinitions(const std::string& scriptPath) {
    return waveController_.loadWaveDefinitions(
        L_, scriptPath, defaultEnemyId_, [this](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
            const EnemyArchetype* archetype = findEnemyArchetype(enemyId);
            if (!archetype) {
                return std::nullopt;
            }
            PlayLevelWaveController::EnemyWaveDefaults defaults;
            defaults.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
            return defaults;
        });
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
    readStringField("projectileModel", outArchetype.projectileModelPath);
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

        lua_getfield(L_, -1, "attackDamage");
        if (lua_isnumber(L_, -1)) {
            outArchetype.attackDamage = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "attackSpeed");
        if (lua_isnumber(L_, -1)) {
            outArchetype.attackSpeed = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "projectileSpeed");
        if (lua_isnumber(L_, -1)) {
            outArchetype.projectileSpeed = static_cast<float>(lua_tonumber(L_, -1));
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
    if (outArchetype.attackDamage <= 0.01f) {
        outArchetype.attackDamage = 1.0f;
    }
    if (outArchetype.attackSpeed <= 0.01f) {
        outArchetype.attackSpeed = 1.0f;
    }
    if (outArchetype.projectileSpeed <= 0.1f) {
        outArchetype.projectileSpeed = 16.0f;
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
        towerPlacementController_.setSelectedLoadoutIndex(0);
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
    const int selectedSlot = towerPlacementController_.selectedLoadoutIndex();
    if (selectedSlot < 0 || selectedSlot >= static_cast<int>(towerLoadoutIds_.size())) {
        return nullptr;
    }
    return findTowerArchetype(towerLoadoutIds_[selectedSlot]);
}

bool PlayLevelScene::raycastGroundAtCursor(glm::vec3& outHit) const {
    return cameraController_.raycastGroundAtCursor(outHit);
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
    towerPlacementController_.updateSelectionHotkeys(towerLoadoutIds_.size());
    const TowerArchetype* selected = selectedTowerArchetype();
    towerPlacementController_.updatePlacementFromInput(
        selected != nullptr,
        [this](glm::vec3& outHit) { return raycastGroundAtCursor(outHit); },
        [this, selected](const glm::vec3& worldPos) {
            return selected ? validateTowerPlacement(*selected, worldPos).empty() : false;
        },
        [this, selected](const glm::vec3& worldPos) {
            if (!selected) {
                return;
            }
            if (requestSpendMoney(static_cast<float>(selected->cost))) {
                const float attackIntervalSeconds = 1.0f / std::max(0.01f, selected->attackSpeed);
                placedTowers_.push_back(PlacedTower{selected->id,
                                                    worldPos,
                                                    selected->attackDamage,
                                                    selected->attackRange,
                                                    attackIntervalSeconds,
                                                    0.0f,
                                                    selected->projectileSpeed,
                                                    selected->cost});
            }
        });
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

    std::vector<AnimatedEntityInstanceSet::Instance> instances;
    instances.reserve(towerArchetypes_.size() * (kTowerPoolPlacementsPerType + 1));

    auto pushTowerInstance = [&](const TowerArchetype& tower,
                                 int prototypeIndex,
                                 const glm::mat4& transform,
                                 const std::string& debugGroup,
                                 const std::string& debugLabel) {
        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = transform;
        instance.prototypeIndex = prototypeIndex;
        instance.debugGroup = debugGroup;
        instance.debugLabel = debugLabel;
        instances.push_back(std::move(instance));
    };

    const glm::mat4 hidden = glm::translate(glm::mat4{1.0f}, glm::vec3(0.0f, kTowerHiddenY, 0.0f));
    std::unordered_map<std::string, int> usedPerTower;
    for (const PlacedTower& placed : placedTowers_) {
        const TowerArchetype* tower = findTowerArchetype(placed.towerId);
        if (!tower) {
            continue;
        }

        const auto protoIt = towerTemplatePrototypeById_.find(placed.towerId);
        if (protoIt == towerTemplatePrototypeById_.end()) {
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
        const std::string& group = poolsIt->second[poolIndex];
        pushTowerInstance(*tower, protoIt->second, model, group, group);
    }

    for (const auto& [towerId, groups] : towerPoolGroupsById_) {
        const TowerArchetype* tower = findTowerArchetype(towerId);
        if (!tower) {
            continue;
        }
        const auto protoIt = towerTemplatePrototypeById_.find(towerId);
        if (protoIt == towerTemplatePrototypeById_.end()) {
            continue;
        }

        const int usedCount = usedPerTower[towerId];
        for (int i = usedCount; i < static_cast<int>(groups.size()); ++i) {
            pushTowerInstance(*tower, protoIt->second, hidden, groups[i], groups[i]);
        }
    }

    for (const auto& [towerId, ghostGroup] : towerGhostGroupById_) {
        const TowerArchetype* tower = findTowerArchetype(towerId);
        if (!tower) {
            continue;
        }

        const auto protoIt = towerTemplatePrototypeById_.find(towerId);
        if (protoIt == towerTemplatePrototypeById_.end()) {
            continue;
        }

        glm::mat4 ghost = hidden;
        const TowerArchetype* selected = selectedTowerArchetype();
        const auto& placementState = towerPlacementController_.state();
        if (selected && selected->id == towerId && placementState.hasHit) {
            ghost = buildTowerModelTransform(*tower, placementState.worldPos + glm::vec3(0.0f, 0.02f, 0.0f));
        }
        pushTowerInstance(*tower, protoIt->second, ghost, ghostGroup, ghostGroup);
    }

    for (std::size_t i = 0; i < activeProjectiles_.size(); ++i) {
        const ActiveProjectile& projectile = activeProjectiles_[i];
        const auto protoIt = projectileTemplatePrototypeByTowerId_.find(projectile.towerId);
        if (protoIt == projectileTemplatePrototypeByTowerId_.end()) {
            continue;
        }

        const TowerArchetype* tower = findTowerArchetype(projectile.towerId);
        if (!tower) {
            continue;
        }

        glm::vec3 flatVelocity = projectile.velocity;
        flatVelocity.y = 0.0f;
        float yaw = 0.0f;
        if (glm::dot(flatVelocity, flatVelocity) > 1e-6f) {
            yaw = std::atan2(flatVelocity.x, flatVelocity.z);
        }

        const glm::mat4 model = glm::translate(glm::mat4{1.0f}, projectile.position) *
                                glm::rotate(glm::mat4{1.0f},
                                            yaw + glm::radians(tower->facingYawOffsetDegrees),
                                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, tower->renderScale)));

        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = model;
        instance.prototypeIndex = protoIt->second;
        instance.debugGroup = "tower_projectile:" + projectile.towerId;
        instance.debugLabel = "tower_projectile:" + projectile.towerId + ":" + std::to_string(i);
        instances.push_back(std::move(instance));
    }

    worldRenderer_->setTowerInstanceTransforms(instances);
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
    if (pickingController_.selectedSelection().valid) {
        parseTowerPoolGroup(pickingController_.selectedSelection().group, selectedTowerId, selectedTowerPoolIndex);
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
    const auto& placementState = towerPlacementController_.state();
    if (!selected || !placementState.hasHit) {
        return;
    }

    const ImU32 col = placementState.canPlace ? IM_COL32(90, 255, 120, 210) : IM_COL32(255, 90, 90, 210);
    drawWorldRing(placementState.worldPos, std::max(0.5f, selected->attackRange), col, 64, 2.0f);

    ImVec2 centerScreen{};
    float depthAbs = 0.0f;
    if (projectWorldToScreen(placementState.worldPos + glm::vec3(0.0f, 0.05f, 0.0f),
                             view,
                             proj,
                             displaySize,
                             renderSize,
                             centerScreen,
                             depthAbs,
                             nullptr)) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImU32 fill = placementState.canPlace ? IM_COL32(90, 255, 120, 85) : IM_COL32(255, 90, 90, 85);
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

    return routeController_.updateFromRoutePoints(worldRenderer_->routePoints());
}

glm::vec3 PlayLevelScene::sampleRoutePosition(float distanceAlongPath) const {
    return routeController_.samplePosition(distanceAlongPath);
}

float PlayLevelScene::sampleRouteYaw(float distanceAlongPath) const {
    return routeController_.sampleYaw(distanceAlongPath);
}

void PlayLevelScene::syncEnemyInstanceTransforms() {
    syncTowerInstanceTransforms();
}

void PlayLevelScene::reconcileSelectedEnemyAfterSimulation() {
    if (selectedEnemyRuntimeId_ == 0) {
        return;
    }

    const auto it = std::find_if(activeEnemies_.begin(),
                                 activeEnemies_.end(),
                                 [this](const ActiveEnemy& enemy) { return enemy.runtimeId == selectedEnemyRuntimeId_; });
    if (it != activeEnemies_.end()) {
        const int nextIndex = static_cast<int>(std::distance(activeEnemies_.begin(), it));
        pickingController_.setSelectedInstanceIndex(nextIndex);
        return;
    }

    selectedEnemyRuntimeId_ = 0;
    if (pickingController_.selectedInstanceIndex() >= 0) {
        pickingController_.clearSelection("selected enemy died");
    }
}

std::string PlayLevelScene::validateStartWaveRequest() const {
    return waveController_.validateStartWaveRequest(gameplayState_,
                                                    worldRenderer_ && worldRenderer_->isLoaded(),
                                                    worldRenderer_ && worldRenderer_->hasAnimatedEntityTemplate(),
                                                    routeController_.hasValidRoute());
}

void PlayLevelScene::updateWaveSimulation(float dt) {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }

    waveController_.updateWaveSpawning(
        gameplayState_, dt, static_cast<int>(activeEnemies_.size()), [this](const std::string& enemyId) {
            const EnemyArchetype* archetype = findEnemyArchetype(enemyId);
            const float health = archetype ? archetype->health : 1.0f;
            const float moveSpeed = archetype ? archetype->moveSpeed : 1.0f;
            const float rewardMoney = archetype ? archetype->rewardMoney : 0.0f;
            const float renderScale = archetype ? archetype->renderScale : 1.0f;
            const float baseDamage = archetype ? archetype->baseDamage : 5.0f;
            const float facingYawOffsetDegrees = archetype ? archetype->facingYawOffsetDegrees : 0.0f;

            activeEnemies_.push_back(ActiveEnemy{enemyId,
                                                 nextEnemyRuntimeId_++,
                                                 0.0f,
                                                 std::max(1.0f, health),
                                                 std::max(0.05f, moveSpeed),
                                                 std::max(0.0f, rewardMoney),
                                                 std::max(1.0f, baseDamage),
                                                 std::max(0.01f, renderScale),
                                                 facingYawOffsetDegrees});
        });

    std::size_t writeIndex = 0;
    for (std::size_t i = 0; i < activeEnemies_.size(); ++i) {
        ActiveEnemy enemy = activeEnemies_[i];
        enemy.distanceAlongPath += std::max(0.05f, enemy.moveSpeed) * dt;

        if (enemy.distanceAlongPath >= routeController_.totalLength()) {
            requestDamageBase(std::max(1.0f, enemy.baseDamage));
            continue;
        }

        activeEnemies_[writeIndex++] = enemy;
    }
    activeEnemies_.resize(writeIndex);
    reconcileSelectedEnemyAfterSimulation();

    for (PlacedTower& tower : placedTowers_) {
        tower.attackCooldownRemainingSeconds = std::max(0.0f, tower.attackCooldownRemainingSeconds - dt);
        if (tower.attackCooldownRemainingSeconds > 0.0f) {
            continue;
        }

        const float attackRangeSq = tower.attackRange * tower.attackRange;
        int targetEnemyIndex = -1;
        float bestDistSq = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(activeEnemies_.size()); ++i) {
            const ActiveEnemy& enemy = activeEnemies_[i];
            const glm::vec3 enemyPos = sampleRoutePosition(enemy.distanceAlongPath);
            const glm::vec3 delta = enemyPos - tower.position;
            const float distSq = glm::dot(delta, delta);
            if (distSq <= attackRangeSq && distSq < bestDistSq) {
                bestDistSq = distSq;
                targetEnemyIndex = i;
            }
        }

        if (targetEnemyIndex < 0) {
            continue;
        }

        const ActiveEnemy& targetEnemy = activeEnemies_[targetEnemyIndex];
        const glm::vec3 launchPosition = tower.position + glm::vec3(0.0f, 0.7f, 0.0f);
        const glm::vec3 targetPosition = sampleRoutePosition(targetEnemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);

        glm::vec3 direction = targetPosition - launchPosition;
        const float dirLenSq = glm::dot(direction, direction);
        if (dirLenSq <= 1e-8f) {
            direction = glm::vec3(0.0f, 0.0f, 1.0f);
        } else {
            direction = glm::normalize(direction);
        }

        ActiveProjectile projectile;
        projectile.towerId = tower.towerId;
        projectile.position = launchPosition;
        projectile.velocity = direction * std::max(0.1f, tower.projectileSpeed);
        projectile.damage = std::max(0.01f, tower.attackDamage);
        projectile.remainingLifeSeconds = kProjectileLifetimeSeconds;
        projectile.targetEnemyRuntimeId = targetEnemy.runtimeId;
        activeProjectiles_.push_back(std::move(projectile));

        tower.attackCooldownRemainingSeconds = std::max(0.01f, tower.attackIntervalSeconds);
    }

    std::size_t projectileWriteIndex = 0;
    for (std::size_t i = 0; i < activeProjectiles_.size(); ++i) {
        ActiveProjectile projectile = activeProjectiles_[i];

        ActiveEnemy* targetEnemy = nullptr;
        for (ActiveEnemy& enemy : activeEnemies_) {
            if (enemy.runtimeId == projectile.targetEnemyRuntimeId) {
                targetEnemy = &enemy;
                break;
            }
        }

        // Target already died or left simulation; retire this projectile.
        if (!targetEnemy) {
            continue;
        }

        const glm::vec3 enemyPos = sampleRoutePosition(targetEnemy->distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);
        glm::vec3 toEnemy = enemyPos - projectile.position;
        const float distanceToEnemy = glm::length(toEnemy);

        if (distanceToEnemy > 1e-6f) {
            toEnemy /= distanceToEnemy;
        } else {
            toEnemy = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        const float projectileSpeed = glm::length(projectile.velocity);
        projectile.velocity = toEnemy * std::max(0.1f, projectileSpeed);
        projectile.position += projectile.velocity * dt;
        projectile.remainingLifeSeconds -= dt;

        // TD lock-on behavior: once a target is chosen, the shot should land as long as target remains valid.
        const float travelThisFrame = std::max(0.0f, glm::length(projectile.velocity) * dt);
        const glm::vec3 postMoveToEnemy = enemyPos - projectile.position;
        const float postMoveDistanceSq = glm::dot(postMoveToEnemy, postMoveToEnemy);
        const bool reachedTarget = postMoveDistanceSq <= (kProjectileHitRadius * kProjectileHitRadius) ||
                                   distanceToEnemy <= (travelThisFrame + kProjectileHitRadius);

        if (reachedTarget || projectile.remainingLifeSeconds <= 0.0f) {
            targetEnemy->health -= projectile.damage;
            continue;
        }

        activeProjectiles_[projectileWriteIndex++] = std::move(projectile);
    }
    activeProjectiles_.resize(projectileWriteIndex);

    std::size_t enemyWriteIndex = 0;
    for (std::size_t i = 0; i < activeEnemies_.size(); ++i) {
        ActiveEnemy enemy = activeEnemies_[i];
        if (enemy.health <= 0.0f) {
            gameplayState_.playerMoney += std::max(0.0f, enemy.rewardMoney);
            gameplayState_.enemiesDefeated += 1;
            continue;
        }
        activeEnemies_[enemyWriteIndex++] = std::move(enemy);
    }
    activeEnemies_.resize(enemyWriteIndex);
    reconcileSelectedEnemyAfterSimulation();

    gameplayState_.enemiesAlive = static_cast<int>(activeEnemies_.size());

    if (gameplayState_.matchStatus != MatchStatus::Running) {
        gameplayState_.waveInProgress = false;
        gameplayState_.waveCountdownActive = false;
        waveController_.resetRuntimeState();
        activeEnemies_.clear();
        activeProjectiles_.clear();
        selectedEnemyRuntimeId_ = 0;
        if (pickingController_.selectedInstanceIndex() >= 0) {
            pickingController_.clearSelection("selection cleared");
        }
        return;
    }

    if (!gameplayState_.waveInProgress && !gameplayState_.waveCountdownActive) {
        const int waveCount = static_cast<int>(waveController_.waveCount());
        if (gameplayState_.currentWave > waveCount) {
            if (activeEnemies_.empty()) {
                gameplayState_.matchStatus = MatchStatus::Victory;
            }
            return;
        }

        if (activeEnemies_.empty()) {
            requestStartWave();
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
            self->waveController_.clearDefinitions();
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
            const float overrideRoundDurationSeconds =
                (lua_gettop(L) >= 2 && lua_isnumber(L, 2)) ? static_cast<float>(lua_tonumber(L, 2)) : -1.0f;

            std::string error;
            const bool ok = self->waveController_.registerWaveFromLua(
                L,
                1,
                self->defaultEnemyId_,
                [self](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
                    const PlayLevelScene::EnemyArchetype* archetype = self->findEnemyArchetype(enemyId);
                    if (!archetype) {
                        return std::nullopt;
                    }
                    PlayLevelWaveController::EnemyWaveDefaults defaults;
                    defaults.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
                    return defaults;
                },
                overrideRoundDurationSeconds,
                error);

            lua_pushboolean(L, ok ? 1 : 0);
            if (!ok) {
                lua_pushstring(L, error.c_str());
                return 2;
            }
            return 1;
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
            lua_pushinteger(L, static_cast<lua_Integer>(self->waveController_.waveCount()));
            lua_setfield(L, -2, "waveCount");
            lua_pushinteger(L, static_cast<lua_Integer>(self->routeController_.pointCount()));
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
            lua_pushnumber(L, self->cameraController_.position().x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->cameraController_.position().y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->cameraController_.position().z);
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
            lua_pushinteger(L, self->towerPlacementController_.selectedLoadoutIndex() + 1);
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
                lua_pushboolean(L, self->towerPlacementController_.selectedLoadoutIndex() == slotIdx);
                lua_setfield(L, -2, "selected");

                if (archetype) {
                    lua_pushstring(L, archetype->id.c_str());
                    lua_setfield(L, -2, "id");
                    lua_pushstring(L, archetype->displayName.c_str());
                    lua_setfield(L, -2, "displayName");
                    lua_pushinteger(L, archetype->cost);
                    lua_setfield(L, -2, "cost");
                    lua_pushnumber(L, archetype->attackDamage);
                    lua_setfield(L, -2, "attackDamage");
                    lua_pushnumber(L, archetype->attackRange);
                    lua_setfield(L, -2, "attackRange");
                    lua_pushnumber(L, archetype->attackSpeed);
                    lua_setfield(L, -2, "attackSpeed");
                    lua_pushstring(L, archetype->modelPath.c_str());
                    lua_setfield(L, -2, "modelPath");
                    lua_pushstring(L, archetype->projectileModelPath.c_str());
                    lua_setfield(L, -2, "projectileModelPath");
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

            self->towerPlacementController_.setSelectedLoadoutIndex(slotIdx);
            return pushCommandResult(L, true, "selected");
        },
        1);
    lua_setfield(L_, gameplayTable, "selectTowerSlot");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->towerPlacementController_.cancelPlacement();
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
            const auto& placementState = self->towerPlacementController_.state();
            lua_pushboolean(L, active);
            lua_setfield(L, -2, "active");
            lua_pushinteger(L, self->towerPlacementController_.selectedLoadoutIndex() + 1);
            lua_setfield(L, -2, "selectedSlot");
            lua_pushboolean(L, placementState.hasHit);
            lua_setfield(L, -2, "hasHit");
            lua_pushboolean(L, placementState.canPlace);
            lua_setfield(L, -2, "canPlace");

            lua_newtable(L);
            lua_pushnumber(L, placementState.worldPos.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, placementState.worldPos.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, placementState.worldPos.z);
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
                if (!placementState.hasHit) {
                    reason = "cursor is not over ground";
                } else {
                    reason = self->validateTowerPlacement(*tower, placementState.worldPos);
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
            self->pickingController_.setPickingEnabled(lua_toboolean(L, 1) != 0);
            lua_pushboolean(L, self->pickingController_.pickingEnabled());
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "setPickEnabled");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->pickingController_.pickingEnabled());
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getPickEnabled");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->pickingController_.setPickSpheresVisible(lua_toboolean(L, 1) != 0);
            lua_pushboolean(L, self->pickingController_.pickSpheresVisible());
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "setDebugPickSpheresVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->pickingController_.pickSpheresVisible());
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getDebugPickSpheresVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const auto& selected = self->pickingController_.selectedSelection();
            const auto& overlayStats = self->pickingController_.overlayStats();
            const auto& hover = self->pickingController_.hoverSelection();
            lua_newtable(L);
            lua_pushboolean(L, selected.valid);
            lua_setfield(L, -2, "valid");
            lua_pushstring(L, self->pickingController_.status().c_str());
            lua_setfield(L, -2, "status");
            lua_pushboolean(L, self->pickingController_.pickSpheresVisible());
            lua_setfield(L, -2, "debugPickSpheresVisible");

            lua_newtable(L);
            lua_pushinteger(L, overlayStats.sphereTotal);
            lua_setfield(L, -2, "sphereTotal");
            lua_pushinteger(L, overlayStats.sphereDrawn);
            lua_setfield(L, -2, "sphereDrawn");
            lua_pushinteger(L, overlayStats.rejectBehindCamera);
            lua_setfield(L, -2, "rejectBehindCamera");
            lua_pushinteger(L, overlayStats.rejectClipW);
            lua_setfield(L, -2, "rejectClipW");
            lua_pushinteger(L, overlayStats.rejectNdcZ);
            lua_setfield(L, -2, "rejectNdcZ");
            lua_pushinteger(L, overlayStats.rejectRadius);
            lua_setfield(L, -2, "rejectRadius");
            lua_pushinteger(L, overlayStats.hoveredSphereFound);
            lua_setfield(L, -2, "hoveredSphereFound");
            lua_pushinteger(L, overlayStats.hoveredRejectReason);
            lua_setfield(L, -2, "hoveredRejectReason");
            lua_pushnumber(L, overlayStats.hoveredDepth);
            lua_setfield(L, -2, "hoveredDepth");
            lua_pushnumber(L, overlayStats.hoveredRadiusPixels);
            lua_setfield(L, -2, "hoveredRadiusPixels");
            lua_pushnumber(L, overlayStats.displayWidth);
            lua_setfield(L, -2, "displayWidth");
            lua_pushnumber(L, overlayStats.displayHeight);
            lua_setfield(L, -2, "displayHeight");
            lua_pushnumber(L, overlayStats.renderWidth);
            lua_setfield(L, -2, "renderWidth");
            lua_pushnumber(L, overlayStats.renderHeight);
            lua_setfield(L, -2, "renderHeight");
            lua_pushnumber(L, self->cameraController_.yaw());
            lua_setfield(L, -2, "cameraYaw");
            lua_pushnumber(L, self->cameraController_.pitch());
            lua_setfield(L, -2, "cameraPitch");
            lua_setfield(L, -2, "overlayDebug");

            lua_newtable(L);
            lua_pushboolean(L, hover.valid);
            lua_setfield(L, -2, "valid");
            lua_pushinteger(L, self->pickingController_.hoveredInstanceIndex());
            lua_setfield(L, -2, "instanceIndex");
            if (hover.valid) {
                lua_pushstring(L, hover.group.c_str());
                lua_setfield(L, -2, "group");
                lua_pushstring(L, hover.label.c_str());
                lua_setfield(L, -2, "label");
            }
            lua_setfield(L, -2, "hover");

            if (selected.valid) {
                const auto& s = selected;
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
            self->pickingController_.clearSelection("selection cleared");
            self->selectedEnemyRuntimeId_ = 0;
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
