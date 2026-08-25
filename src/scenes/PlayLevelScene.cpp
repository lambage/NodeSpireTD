#include "scenes/PlayLevelScene.hpp"

#include "LuaStateBootstrap.hpp"
#include "VulkanContext.hpp"
#include "scenes/SceneSharedState.hpp"
#include "utility/WorldRenderer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

PlayLevelScene::PlayLevelScene() : GameScene(), towerLoadController_(L_), enemyLoadController_(L_) {}
PlayLevelScene::~PlayLevelScene() = default;

namespace {

constexpr float kDebugOverlayFovRadians = glm::radians(60.0f);
constexpr float kTowerHiddenY = -10000.0f;
constexpr float kTowerGhostAlpha = 0.45f;

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

// Vertical tolerance (world units) added above/below a route segment's endpoints when testing
// whether a point falls inside its no-build corridor. The route can climb/descend with the
// terrain, so a plain XZ-plane test would incorrectly ignore path segments on slopes; this
// keeps the corridor test forgiving enough to still catch those without checking full 3D
// distance-to-segment (which would reject valid points where the tower's ground height differs
// slightly from the interpolated path height). Tune this down if towers too far above/below the
// path are being incorrectly blocked, or up if sloped path segments are leaking through.
constexpr float kPathCorridorVerticalMargin = 1.25f;

// Extra length (world units) the no-build box extends past each segment's two endpoints along
// the direction of travel. Kept independent from the lateral half-width so tightening/loosening
// one doesn't affect the other; only needs to be large enough that consecutive segments'
// corridors overlap at turns instead of leaving an uncovered gap in the corner.
constexpr float kPathCorridorEndExtension = 1.25f;

// Tests whether `point` falls inside the rectangular no-build corridor swept along route
// segment [a,b] in the XZ plane, extended by `halfWidth` on both sides and by
// kPathCorridorEndExtension at both ends (so consecutive segments' corridors overlap cleanly at
// turns instead of leaving gaps at corners).
bool pointInRouteSegmentCorridor(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, float halfWidth) {
    const glm::vec2 a2(a.x, a.z);
    const glm::vec2 b2(b.x, b.z);
    const glm::vec2 p2(point.x, point.z);
    const glm::vec2 ab = b2 - a2;
    const float len = glm::length(ab);
    const float minY = std::min(a.y, b.y) - kPathCorridorVerticalMargin;
    const float maxY = std::max(a.y, b.y) + kPathCorridorVerticalMargin;
    if (point.y < minY || point.y > maxY) {
        return false;
    }

    if (len < 1e-4f) {
        return glm::length(p2 - a2) <= halfWidth;
    }

    const glm::vec2 dir = ab / len;
    const glm::vec2 rel = p2 - a2;
    const float along = rel.x * dir.x + rel.y * dir.y;
    if (along < -kPathCorridorEndExtension || along > len + kPathCorridorEndExtension) {
        return false;
    }
    const float across = rel.x * (-dir.y) + rel.y * dir.x;
    return std::abs(across) <= halfWidth;
}

// Builds the 8 world-space corners of the oriented no-build box for one route segment. Must
// stay in sync with pointInRouteSegmentCorridor() (same half-width/end-extension/vertical
// margin) so the debug overlay always matches the real placement rule exactly.
void buildRouteSegmentCorridorBoxCorners(const glm::vec3& a, const glm::vec3& b, float halfWidth,
                                         std::array<glm::vec3, 8>& outCorners) {
    glm::vec3 forward = b - a;
    forward.y = 0.0f;
    const float len = glm::length(forward);
    forward = (len > 1e-4f) ? (forward / len) : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right(forward.z, 0.0f, -forward.x);

    const glm::vec3 extendedA = a - forward * kPathCorridorEndExtension;
    const glm::vec3 extendedB = b + forward * kPathCorridorEndExtension;
    const float minY = std::min(a.y, b.y) - kPathCorridorVerticalMargin;
    const float maxY = std::max(a.y, b.y) + kPathCorridorVerticalMargin;

    const glm::vec3 base[4] = {
        extendedA - right * halfWidth,
        extendedA + right * halfWidth,
        extendedB + right * halfWidth,
        extendedB - right * halfWidth,
    };
    for (int i = 0; i < 4; ++i) {
        outCorners[i] = glm::vec3(base[i].x, minY, base[i].z);
        outCorners[static_cast<std::size_t>(i) + 4] = glm::vec3(base[i].x, maxY, base[i].z);
    }
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

bool projectWorldToScreen(const glm::vec3& worldPos, const glm::mat4& view, const glm::mat4& proj,
                          const ImVec2& displaySize, const ImVec2& renderSize, ImVec2& outScreen, float& outDepthAbs,
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

const playlevel::PlacedTower* findPlacedTowerByPoolKeyImpl(const std::vector<playlevel::PlacedTower>& placedTowers,
                                                            const std::string& towerId, int poolIndex) {
    if (towerId.empty() || poolIndex < 0) {
        return nullptr;
    }

    int perTypeIndex = 0;
    for (const playlevel::PlacedTower& tower : placedTowers) {
        if (tower.towerId != towerId) {
            continue;
        }
        if (perTypeIndex == poolIndex) {
            return &tower;
        }
        ++perTypeIndex;
    }
    return nullptr;
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

    LuaStateBootstrap::initializeEngineState(L_, state.vulkanContext, state.audioEngine);
    registerLuaGameplayApi();

    gameplayState_.resetForNewRun();
    towerLoadController_.reset();
    enemyLoadController_.reset();
    towerPlacementController_.reset();
    placedTowers_.clear();
    activeProjectiles_.clear();
    nextEnemyRuntimeId_ = 1;
    pendingCommands_.clear();
    activeEnemies_.clear();
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

    if (enemyLoadController_.empty() && !enemyLoadController_.loadEnemyArchetype("assets/models/enemy/goblin1.enemy.lua")) {
        spdlog::warn("PlayLevelScene: using built-in enemy defaults because no archetype could be loaded.");
        enemyLoadController_.registerArchetype(EnemyArchetype{});
    }

    if (!waveController_.hasDefinitions() && !selectedWavesScriptPath_.empty() &&
        !loadWaveDefinitions(selectedWavesScriptPath_)) {
        spdlog::warn("PlayLevelScene: using fallback wave definition because {} failed to load.",
                     selectedWavesScriptPath_);
    }

    if (!waveController_.hasDefinitions()) {
        PlayLevelWaveController::WaveDefinition fallback;
        fallback.spawns.push_back(PlayLevelWaveController::WaveSpawnDefinition{});
        waveController_.definitionsMutable().push_back(std::move(fallback));
    }

    towerLoadController_.discoverTowerArchetypesInDirectory("assets/models/towers");
    towerLoadController_.populateWorldAssets(worldAssetSpec_);

    publishLevelUiTextures(L_, worldAssetSpec_);

    {
        std::vector<std::filesystem::path> templateModels;
        templateModels.reserve(enemyLoadController_.archetypes().size());
        for (const auto& [id, archetype] : enemyLoadController_.archetypes()) {
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
        if (!towerPreviewPanels_.empty()) {
            worldRenderer_->renderTowerPreviewPanels(cmd, extent, towerPreviewPanels_, towerPreviewSpinRadians_);
        }
    }
}

// ─── per-frame ────────────────────────────────────────────────────────────────

void PlayLevelScene::render(SceneSharedState& state, float dt) {
    towerPreviewPanels_.clear();
    towerPreviewSpinRadians_ = std::fmod(towerPreviewSpinRadians_ + dt * 0.55f, 6.2831853071795864769f);

    applyPendingGameplayCommands();
    updateWaveSimulation(dt);

    // Tick GPU uploads (one step per frame while loading)
    if (worldRenderer_ && !worldRenderer_->isLoaded() && !worldRenderer_->loadFailed()) {
        worldRenderer_->tickLoad();
    }

    const bool isLoaded = worldRenderer_ && worldRenderer_->isLoaded();

    if (isLoaded) {
        // Copy placement regions from WorldRenderer (one-time after load)
        if (placementRegions_.empty()) {
            placementRegions_ = worldRenderer_->placementRegions();
        }
        
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
        pickingController_.updateSelectionFromMouse(worldRenderer_.get(), buildViewMatrix(),
                                                    cameraController_.position(),
                                                    towerPlacementController_.hasActiveSelection());
        const int selectedInstanceIndex = pickingController_.selectedInstanceIndex();
        const bool selectedIsEnemy = pickingController_.selectedEntityKind() == WorldEntityKind::Enemy;
        selectedEnemyRuntimeId_ =
            (selectedIsEnemy && selectedInstanceIndex >= 0 && selectedInstanceIndex < static_cast<int>(activeEnemies_.size()))
                ? activeEnemies_[static_cast<std::size_t>(selectedInstanceIndex)].runtimeId
                : 0;
        worldRenderer_->setHighlightedInstances(pickingController_.hoveredEntityKind(), pickingController_.hoveredInstanceIndex(),
                                                pickingController_.selectedEntityKind(), pickingController_.selectedInstanceIndex());
    }

    luaOnRender(state, scriptRef_, dt);

    if (isLoaded) {
        drawTowerPlacementOverlay();
        drawPlacementBoundsOverlay();
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
    const MatchStatus originalStatus = gameplayState_.matchStatus;
    if (gameplayState_.matchStatus == MatchStatus::WaitingToStart) {
        gameplayState_.matchStatus = MatchStatus::Running;
    }

    const bool started = waveController_.beginWaveCountdown(
        gameplayState_, worldRenderer_ && worldRenderer_->isLoaded(),
        worldRenderer_ && worldRenderer_->hasAnimatedEntityTemplate(), routeController_.hasValidRoute());
    if (!started && originalStatus == MatchStatus::WaitingToStart) {
        gameplayState_.matchStatus = originalStatus;
    }
    return started;
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
        L_, scriptPath, enemyLoadController_.defaultId(),
        [this](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
            const EnemyArchetype* archetype = enemyLoadController_.findArchetype(enemyId);
            if (!archetype) {
                return std::nullopt;
            }
            PlayLevelWaveController::EnemyWaveDefaults defaults;
            defaults.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
            return defaults;
        });
}

const TowerArchetype* PlayLevelScene::selectedTowerArchetype() const {
    const int selectedSlot = towerPlacementController_.selectedLoadoutIndex();
    return towerLoadController_.archetypeAtLoadoutSlot(selectedSlot);
}

const PlayLevelScene::PlacedTower* PlayLevelScene::findPlacedTowerByPoolKey(const std::string& towerId,
                                                                             int poolIndex) const {
    return findPlacedTowerByPoolKeyImpl(placedTowers_, towerId, poolIndex);
}

PlayLevelScene::PlacedTower* PlayLevelScene::findPlacedTowerByPoolKey(const std::string& towerId, int poolIndex) {
    return const_cast<PlacedTower*>(findPlacedTowerByPoolKeyImpl(placedTowers_, towerId, poolIndex));
}

const TowerArchetype::UpgradeNode* PlayLevelScene::findUpgradeNodeById(const TowerArchetype& archetype,
                                                                        const std::string& nodeId) const {
    auto it = std::find_if(archetype.upgradeNodes.begin(), archetype.upgradeNodes.end(),
                           [&nodeId](const TowerArchetype::UpgradeNode& node) { return node.id == nodeId; });
    return (it != archetype.upgradeNodes.end()) ? &(*it) : nullptr;
}

void PlayLevelScene::applyTowerUpgradeEffects(const TowerArchetype& archetype, const PlacedTower& placedTower,
                                              float& outAttackDamage, float& outAttackRange, float& outAttackSpeed,
                                              float& outProjectileSpeed, float& outSplashRadius, float& outChainRange,
                                              float& outRicochetRange, int& outProjectileCount,
                                              int& outChainTargetCount, int& outRicochetCount) const {
    float attackDamage = archetype.attackDamage;
    float attackRange = archetype.attackRange;
    float attackSpeed = archetype.attackSpeed;
    float projectileSpeed = archetype.projectileSpeed;
    float splashRadius = archetype.splashRadius;
    float chainRange = archetype.chainRange;
    float ricochetRange = archetype.ricochetRange;
    int projectileCount = archetype.projectileCount;
    int chainTargetCount = archetype.chainTargetCount;
    int ricochetCount = archetype.ricochetCount;

    std::unordered_map<std::string, int> levelByNodeId;
    for (const std::string& unlockedId : placedTower.unlockedUpgradeNodeIds) {
        levelByNodeId[unlockedId] += 1;
    }

    for (const TowerArchetype::UpgradeNode& node : archetype.upgradeNodes) {
        const auto it = levelByNodeId.find(node.id);
        const int level = (it != levelByNodeId.end()) ? it->second : 0;
        if (level <= 0) {
            continue;
        }

        for (int lvl = 0; lvl < level; ++lvl) {
            attackDamage = (attackDamage + node.effects.attackDamageAdd) * node.effects.attackDamageMul;
            attackRange = (attackRange + node.effects.attackRangeAdd) * node.effects.attackRangeMul;
            attackSpeed = (attackSpeed + node.effects.attackSpeedAdd) * node.effects.attackSpeedMul;
            projectileSpeed = (projectileSpeed + node.effects.projectileSpeedAdd) * node.effects.projectileSpeedMul;
            splashRadius = (splashRadius + node.effects.splashRadiusAdd) * node.effects.splashRadiusMul;
            chainRange = (chainRange + node.effects.chainRangeAdd) * node.effects.chainRangeMul;
            ricochetRange = (ricochetRange + node.effects.ricochetRangeAdd) * node.effects.ricochetRangeMul;
            projectileCount += node.effects.projectileCountAdd;
            chainTargetCount += node.effects.chainTargetCountAdd;
            ricochetCount += node.effects.ricochetCountAdd;
        }
    }

    outAttackDamage = std::max(0.01f, attackDamage);
    outAttackRange = std::max(0.1f, attackRange);
    outAttackSpeed = std::max(0.01f, attackSpeed);
    outProjectileSpeed = std::max(0.1f, projectileSpeed);
    outSplashRadius = std::max(0.0f, splashRadius);
    outChainRange = std::max(0.1f, chainRange);
    outRicochetRange = std::max(0.1f, ricochetRange);
    outProjectileCount = std::max(1, projectileCount);
    outChainTargetCount = std::max(1, chainTargetCount);
    outRicochetCount = std::max(0, ricochetCount);
}

std::string PlayLevelScene::validateTowerUpgradeUnlock(const TowerArchetype& archetype, const PlacedTower& placedTower,
                                                       const std::string& nodeId) const {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }

    const TowerArchetype::UpgradeNode* node = findUpgradeNodeById(archetype, nodeId);
    if (!node) {
        return "upgrade node does not exist";
    }

    auto levelForNode = [&placedTower](const std::string& id) {
        return static_cast<int>(std::count(placedTower.unlockedUpgradeNodeIds.begin(),
                                           placedTower.unlockedUpgradeNodeIds.end(), id));
    };
    auto hasUnlocked = [&levelForNode](const std::string& id) { return levelForNode(id) > 0; };

    const int currentLevel = levelForNode(node->id);
    if (currentLevel >= std::max(1, node->maxLevel)) {
        return "upgrade is at max level";
    }

    if (!node->parentId.empty()) {
        const TowerArchetype::UpgradeNode* parent = findUpgradeNodeById(archetype, node->parentId);
        if (!parent) {
            return "configured parent node does not exist";
        }
        if (!hasUnlocked(parent->id)) {
            return "parent node is not unlocked";
        }
    }

    const int totalUpgradesPurchased = static_cast<int>(placedTower.unlockedUpgradeNodeIds.size());
    if (totalUpgradesPurchased < std::max(0, node->minUpgradesRequired)) {
        return "not enough total upgrades unlocked";
    }

    for (const std::string& req : node->requiredNodeIds) {
        if (!hasUnlocked(req)) {
            return "missing required prerequisite";
        }
    }

    for (const std::string& blockedId : node->excludes) {
        if (hasUnlocked(blockedId)) {
            return "blocked by an already unlocked upgrade";
        }
    }

    for (const TowerArchetype::UpgradeNode& existingNode : archetype.upgradeNodes) {
        if (!hasUnlocked(existingNode.id)) {
            continue;
        }
        if (std::find(existingNode.excludes.begin(), existingNode.excludes.end(), node->id) !=
            existingNode.excludes.end()) {
            return "blocked by an already unlocked upgrade";
        }
    }

    if (node->cost > 0 && gameplayState_.playerMoney < static_cast<float>(node->cost)) {
        return "insufficient funds";
    }

    return {};
}

bool PlayLevelScene::unlockTowerUpgrade(PlacedTower& placedTower, const std::string& nodeId, std::string& outReason) {
    const TowerArchetype* archetype = towerLoadController_.findArchetype(placedTower.towerId);
    if (!archetype) {
        outReason = "tower archetype not found";
        return false;
    }

    const std::string reason = validateTowerUpgradeUnlock(*archetype, placedTower, nodeId);
    if (!reason.empty()) {
        outReason = reason;
        return false;
    }

    const TowerArchetype::UpgradeNode* node = findUpgradeNodeById(*archetype, nodeId);
    if (!node) {
        outReason = "upgrade node does not exist";
        return false;
    }

    if (node->cost > 0 && !requestSpendMoney(static_cast<float>(node->cost))) {
        outReason = "insufficient funds";
        return false;
    }

    placedTower.unlockedUpgradeNodeIds.push_back(node->id);

    float attackDamage = placedTower.attackDamage;
    float attackRange = placedTower.attackRange;
    float attackSpeed = 1.0f / std::max(0.01f, placedTower.attackIntervalSeconds);
    float projectileSpeed = placedTower.projectileSpeed;
    float splashRadius = placedTower.splashRadius;
    float chainRange = placedTower.chainRange;
    float ricochetRange = placedTower.ricochetRange;
    int projectileCount = placedTower.projectileCount;
    int chainTargetCount = placedTower.chainTargetCount;
    int ricochetCount = placedTower.ricochetCount;
    applyTowerUpgradeEffects(*archetype, placedTower, attackDamage, attackRange, attackSpeed, projectileSpeed,
                             splashRadius, chainRange, ricochetRange, projectileCount, chainTargetCount,
                             ricochetCount);

    placedTower.attackDamage = attackDamage;
    placedTower.attackRange = attackRange;
    placedTower.attackIntervalSeconds = 1.0f / std::max(0.01f, attackSpeed);
    placedTower.projectileSpeed = projectileSpeed;
    placedTower.splashRadius = splashRadius;
    placedTower.chainRange = chainRange;
    placedTower.ricochetRange = ricochetRange;
    placedTower.projectileCount = projectileCount;
    placedTower.chainTargetCount = chainTargetCount;
    placedTower.ricochetCount = ricochetCount;

    int activeTowerPrototype = towerLoadController_.templatePrototypeIndex(placedTower.towerId);
    int activeProjectilePrototype = towerLoadController_.projectileTemplatePrototypeIndex(placedTower.towerId);
    for (const std::string& unlockedId : placedTower.unlockedUpgradeNodeIds) {
        const TowerArchetype::UpgradeNode* unlockedNode = findUpgradeNodeById(*archetype, unlockedId);
        if (!unlockedNode) {
            continue;
        }
        if (unlockedNode->towerPrototypeOverrideIndex >= 0) {
            activeTowerPrototype = unlockedNode->towerPrototypeOverrideIndex;
        }
        if (unlockedNode->projectilePrototypeOverrideIndex >= 0) {
            activeProjectilePrototype = unlockedNode->projectilePrototypeOverrideIndex;
        }
    }
    placedTower.towerPrototypeIndex = activeTowerPrototype;
    placedTower.projectilePrototypeIndex = activeProjectilePrototype;

    outReason = "unlocked";
    return true;
}

bool PlayLevelScene::raycastGroundAtCursor(glm::vec3& outHit) const {
    return cameraController_.raycastGroundAtCursor(outHit);
}

std::string PlayLevelScene::validateTowerPlacement(const TowerArchetype& archetype, const glm::vec3& worldPos,
                                                   int footprintSampleCount) const {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return "world is still loading";
    }
    if (gameplayState_.playerMoney < static_cast<float>(archetype.cost)) {
        return "insufficient funds";
    }

    // Check tower spacing
    constexpr float kMinTowerSpacing = 1.7f;
    for (const PlacedTower& tower : placedTowers_) {
        const glm::vec3 delta = worldPos - tower.position;
        const float dist2 = glm::dot(delta, delta);
        if (dist2 < (kMinTowerSpacing * kMinTowerSpacing)) {
            return "too close to another tower";
        }
    }

    // Check if placement is on the path
    if (isPointOnPath(worldPos)) {
        return "cannot place towers on the path";
    }

    // Check slope: reject placement on surfaces steeper than the configured maximum.
    // lastTerrainSample_ is refreshed once per frame by updateTowerPlacementFromInput() and
    // corresponds to the same raycast that produced worldPos.
    constexpr float kSlopeMatchEpsilon = 0.01f;
    if (glm::distance(lastTerrainSample_.worldPosition, worldPos) <= kSlopeMatchEpsilon &&
        lastTerrainSample_.slope > maxTowerPlacementSlopeDegrees_) {
        return "surface is too steep to place a tower";
    }

    // Footprint check: sample the terrain around the tower's approximate base radius (not just
    // the single cursor point) so a tower can't be placed with part of its base clipping into a
    // steep cliff face or hanging off an edge, even if the exact cursor point is flat ground.
    if (worldRenderer_ && !isFootprintClearForPlacement(worldPos, archetype, footprintSampleCount)) {
        return "tower base does not fit on this surface";
    }

    // Terrain defaults to ground; region markers can override to cliff/water
    // Currently allow placement on any terrain type

    return {};
}

PlayLevelScene::TerrainSample PlayLevelScene::sampleTerrainAtCursor() const {
    TerrainSample sample;
    
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return sample;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return sample;
    }

    const ImVec2 mousePos = io.MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return sample;
    }

    // Mirrors PlayLevelPickingController::pickModelAtScreen, which is the proven-correct
    // screen-to-world raycasting logic used for tower/enemy picking.
    const float ndcX = (2.0f * mousePos.x) / displaySize.x - 1.0f;
    const float ndcY = (2.0f * mousePos.y) / displaySize.y - 1.0f;

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    constexpr float kFovYRadians = glm::radians(60.0f);
    glm::mat4 proj = glm::perspective(kFovYRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;

    const glm::mat4 view = buildViewMatrix();
    const glm::mat4 invVP = glm::inverse(proj * view);

    const glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec4 nearWorld = invVP * nearClip;
    glm::vec4 farWorld = invVP * farClip;
    if (std::abs(nearWorld.w) < 1e-6f || std::abs(farWorld.w) < 1e-6f) {
        return sample;
    }
    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;

    glm::vec3 rayDir = glm::vec3(farWorld - nearWorld);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return sample;
    }
    rayDir = glm::normalize(rayDir);

    const glm::vec3 camPos = cameraController_.position();

    // Use precise triangle-level raycast against static world geometry (terrain/cliffs),
    // NOT pickModel() which only uses bounding-sphere approximations for tower/enemy selection.
    WorldPickHit pickHit;
    if (!worldRenderer_->raycastStaticGeometry(camPos, rayDir, pickHit)) {
        return sample;
    }

    sample.hit = true;
    sample.worldPosition = pickHit.worldPosition;
    sample.surfaceNormal = pickHit.worldNormal;

    // Calculate slope: angle between surface normal and up vector
    // A perfectly horizontal surface has normal pointing up (0, 1, 0)
    const float dotProduct = glm::dot(sample.surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    const float slopeClamped = glm::clamp(dotProduct, -1.0f, 1.0f);
    const float slopeRadians = std::acos(slopeClamped);
    sample.slope = glm::degrees(slopeRadians);

    // Check if point is on path
    sample.onPath = isPointOnPath(sample.worldPosition);

    // Default all terrain to ground; check for cliff/water region overrides
    sample.towerPlacementType = TowerPlacementRegionType::Ground;
    for (const TowerPlacementRegion& region : placementRegions_) {
        if (sample.worldPosition.x >= region.boundsMin.x && sample.worldPosition.x <= region.boundsMax.x &&
            sample.worldPosition.y >= region.boundsMin.y && sample.worldPosition.y <= region.boundsMax.y &&
            sample.worldPosition.z >= region.boundsMin.z && sample.worldPosition.z <= region.boundsMax.z) {
            sample.towerPlacementType = region.type;
            break;  // Found the region, no need to check others
        }
    }

    return sample;
}

void PlayLevelScene::clearActiveSelectionForTowerPlacement(const char* reason) {
    selectedEnemyRuntimeId_ = 0;
    if (pickingController_.selectedSelection().valid || pickingController_.selectedInstanceIndex() >= 0) {
        pickingController_.clearSelection(reason);
    }
}

bool PlayLevelScene::isPointInPlacementRegion(const glm::vec3& worldPos, const TowerPlacementRegion*& outRegion) const {
    outRegion = nullptr;
    
    for (const TowerPlacementRegion& region : placementRegions_) {
        if (worldPos.x >= region.boundsMin.x && worldPos.x <= region.boundsMax.x &&
            worldPos.y >= region.boundsMin.y && worldPos.y <= region.boundsMax.y &&
            worldPos.z >= region.boundsMin.z && worldPos.z <= region.boundsMax.z) {
            outRegion = &region;
            return true;
        }
    }
    
    return false;
}

bool PlayLevelScene::isPointOnPath(const glm::vec3& worldPos) const {
    // The no-build corridor is auto-generated from the required waypoint route (Start ->
    // Waypoint_N -> End) rather than manually-placed "PathPoint"/"path_zone" terrain markers --
    // map makers only need to define the waypoints the enemies already follow.
    if (!worldRenderer_) {
        return false;
    }

    const std::vector<glm::vec3>& route = worldRenderer_->routePoints();
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        if (pointInRouteSegmentCorridor(worldPos, route[i], route[i + 1], pathCorridorHalfWidth_)) {
            return true;
        }
    }

    return false;
}

bool PlayLevelScene::isFootprintClearForPlacement(const glm::vec3& worldPos, const TowerArchetype& archetype,
                                                  int footprintSampleCount) const {
    if (!worldRenderer_) {
        return true;
    }

    // Approximate tower base radius. TowerArchetype has no explicit footprint radius yet, so
    // derive one from renderScale; this can be replaced with a per-archetype value later.
    constexpr float kTowerFootprintBaseRadius = 0.6f;
    const int sampleCount = std::max(1, footprintSampleCount);
    constexpr float kFootprintProbeHeight = 50.0f;   // start each probe ray well above the surface
    constexpr float kFootprintMaxHeightDelta = 0.6f; // reject if edge height differs too much from center
    constexpr float kTwoPi = 6.2831853071795864769f;
    const float minUpDot = std::cos(glm::radians(maxTowerPlacementSlopeDegrees_));

    const float footprintRadius = kTowerFootprintBaseRadius * std::max(0.01f, archetype.renderScale);

    for (int i = 0; i < sampleCount; ++i) {
        const float angle = (kTwoPi * static_cast<float>(i)) / static_cast<float>(sampleCount);
        const glm::vec3 offset(std::cos(angle) * footprintRadius, 0.0f, std::sin(angle) * footprintRadius);
        const glm::vec3 samplePos = worldPos + offset;
        const glm::vec3 probeOrigin = samplePos + glm::vec3(0.0f, kFootprintProbeHeight, 0.0f);

        WorldPickHit hit;
        if (!worldRenderer_->raycastStaticGeometry(probeOrigin, glm::vec3(0.0f, -1.0f, 0.0f), hit)) {
            return false; // footprint extends past the edge of the terrain
        }

        const float dotProduct = glm::dot(hit.worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (dotProduct < minUpDot) {
            return false; // footprint overlaps a steep slope
        }

        if (std::abs(hit.worldPosition.y - worldPos.y) > kFootprintMaxHeightDelta) {
            return false; // footprint overlaps a ledge/step/cliff edge
        }
    }

    return true;
}

void PlayLevelScene::updateTowerPlacementFromInput() {
    const bool selectedFromHotkey =
        towerPlacementController_.updateSelectionHotkeys(towerLoadController_.loadoutIds().size());
    if (selectedFromHotkey) {
        clearActiveSelectionForTowerPlacement("loadout tower selected");
    }

    const TowerArchetype* selected = selectedTowerArchetype();
    lastPlacementValidationReason_.clear();
    if (!selected) {
        hasValidPlacementAnchor_ = false;
        bungeeInvalidActive_ = false;
        hasLastRawPlacementCandidate_ = false;
        hasLastPlacementValidation_ = false;
        lastPlacementValidationCachedReason_.clear();
    }
    towerPlacementController_.updatePlacementFromInput(
        selected != nullptr, [this, selected](glm::vec3& outHit) {
            // Use terrain-aware sampling instead of flat ground plane
            lastTerrainSample_ = sampleTerrainAtCursor();
            if (!lastTerrainSample_.hit) {
                hasLastRawPlacementCandidate_ = false;
                return false;
            }

            const glm::vec3 candidatePos = lastTerrainSample_.worldPosition;
            constexpr int kPreviewFootprintSampleCount = 4;
            bool candidateValid = false;
            std::string candidateReason;
            if (selected) {
                candidateReason = validateTowerPlacement(*selected, candidatePos, kPreviewFootprintSampleCount);
                candidateValid = candidateReason.empty();
            }
            hasLastRawPlacementCandidate_ = true;
            lastRawPlacementCandidatePos_ = candidatePos;
            lastRawPlacementCandidateValid_ = candidateValid;

            // "Bungee cord" snapping: once a valid spot has been found, small cursor movements into
            // invalid terrain (e.g. a cliff face) keep the preview pinned there instead of jittering
            // into the cliff. Instead of freezing to the last valid point, walk toward the cursor
            // and stop at the nearest still-valid point on that segment.
            constexpr float kSnapReleaseDistance = 1.5f;
            bool resolvedValid = candidateValid;
            std::string resolvedReason = candidateReason;
            if (candidateValid) {
                lastValidPlacementPos_ = candidatePos;
                hasValidPlacementAnchor_ = true;
                bungeeInvalidActive_ = false;
                bungeeAnchorPos_ = candidatePos;
                bungeeResolvedPos_ = candidatePos;
                outHit = candidatePos;
            } else if (hasValidPlacementAnchor_) {
                if (!bungeeInvalidActive_) {
                    bungeeInvalidActive_ = true;
                    bungeeAnchorPos_ = lastValidPlacementPos_;
                    bungeeResolvedPos_ = lastValidPlacementPos_;
                }

                if (glm::distance(candidatePos, bungeeAnchorPos_) >= kSnapReleaseDistance) {
                    hasValidPlacementAnchor_ = false;
                    bungeeInvalidActive_ = false;
                    outHit = candidatePos;
                    if (selected) {
                        resolvedValid = false;
                        resolvedReason = candidateReason;
                    }
                } else {
                // Try lateral "edge slide" first: search both tangent directions in XZ around
                // the cursor so placement can move along obstacle boundaries instead of pinning to
                // the previous valid anchor point.
                const glm::vec3 toCursor = candidatePos - bungeeResolvedPos_;
                glm::vec3 dirXZ(toCursor.x, 0.0f, toCursor.z);
                const float dirLen = glm::length(dirXZ);
                if (dirLen > 1e-4f) {
                    dirXZ /= dirLen;
                } else {
                    dirXZ = glm::vec3(1.0f, 0.0f, 0.0f);
                }
                const glm::vec3 tangent(-dirXZ.z, 0.0f, dirXZ.x);

                bool foundSlide = false;
                glm::vec3 bestSlide = lastValidPlacementPos_;
                float bestDistSq = std::numeric_limits<float>::max();
                constexpr float kSlideStep = 0.20f;
                constexpr int kSlideSteps = 6; // up to 1.2 world units lateral search
                for (int step = 1; step <= kSlideSteps; ++step) {
                    const float d = kSlideStep * static_cast<float>(step);
                    const glm::vec3 offsets[2] = { tangent * d, tangent * -d };
                    for (const glm::vec3& off : offsets) {
                        const glm::vec3 probe = candidatePos + off;
                        const std::string probeReason =
                            validateTowerPlacement(*selected, probe, kPreviewFootprintSampleCount);
                        if (!probeReason.empty()) {
                            continue;
                        }

                        const float distSq = glm::dot(probe - candidatePos, probe - candidatePos);
                        if (distSq < bestDistSq) {
                            bestDistSq = distSq;
                            bestSlide = probe;
                            foundSlide = true;
                        }
                    }
                    // First radius with at least one valid side is already the nearest lateral ring.
                    if (foundSlide) {
                        break;
                    }
                }

                if (foundSlide) {
                    const float prevDistSq = glm::dot(bungeeResolvedPos_ - candidatePos, bungeeResolvedPos_ - candidatePos);
                    const float newDistSq = glm::dot(bestSlide - candidatePos, bestSlide - candidatePos);
                    constexpr float kSlideSwitchHysteresis = 0.08f;
                    constexpr float kSlideSwitchHysteresisSq =
                        kSlideSwitchHysteresis * kSlideSwitchHysteresis;
                    const glm::vec3 chosenSlide =
                        ((newDistSq + kSlideSwitchHysteresisSq) < prevDistSq) ? bestSlide : bungeeResolvedPos_;

                    outHit = chosenSlide;
                    lastValidPlacementPos_ = chosenSlide;
                    bungeeResolvedPos_ = chosenSlide;
                    hasValidPlacementAnchor_ = true;
                    resolvedValid = true;
                    resolvedReason.clear();
                } else {
                    // Fallback: bounded bisection between (invalid) cursor point and
                    // (known-valid) anchor. This keeps behavior robust when lateral search fails.
                    glm::vec3 lowInvalid = candidatePos;
                    glm::vec3 highValid = bungeeResolvedPos_;
                    for (int i = 0; i < 7; ++i) {
                        const glm::vec3 mid = (lowInvalid + highValid) * 0.5f;
                        const std::string midReason =
                            validateTowerPlacement(*selected, mid, kPreviewFootprintSampleCount);
                        if (midReason.empty()) {
                            highValid = mid;
                        } else {
                            lowInvalid = mid;
                        }
                    }

                    const float prevDistSq = glm::dot(bungeeResolvedPos_ - candidatePos, bungeeResolvedPos_ - candidatePos);
                    const float newDistSq = glm::dot(highValid - candidatePos, highValid - candidatePos);
                    constexpr float kFallbackSwitchHysteresis = 0.05f;
                    constexpr float kFallbackSwitchHysteresisSq =
                        kFallbackSwitchHysteresis * kFallbackSwitchHysteresis;
                    const glm::vec3 chosenSlide =
                        ((newDistSq + kFallbackSwitchHysteresisSq) < prevDistSq) ? highValid : bungeeResolvedPos_;

                    outHit = chosenSlide;
                    lastValidPlacementPos_ = chosenSlide;
                    bungeeResolvedPos_ = chosenSlide;
                    hasValidPlacementAnchor_ = true;
                    resolvedValid = true;
                    resolvedReason.clear();
                }
                }
            } else {
                hasValidPlacementAnchor_ = false;
                bungeeInvalidActive_ = false;
                outHit = candidatePos;
            }

            if (selected) {
                // Prime per-frame validation cache so the immediate canPlace callback can reuse
                // this resolved point instead of re-running placement validation.
                hasLastPlacementValidation_ = true;
                lastPlacementValidationTowerId_ = selected->id;
                lastPlacementValidationPos_ = outHit;
                lastPlacementCanPlace_ = resolvedValid;
                lastPlacementValidationCachedReason_ = resolvedReason;
                lastPlacementValidationReason_ = resolvedReason;
            }
            return true;
        },
        [this, selected](const glm::vec3& worldPos) {
            if (!selected) {
                lastPlacementValidationReason_ = "no tower selected";
                return false;
            }

            constexpr int kPreviewFootprintSampleCount = 4;

            constexpr float kValidationReuseDistance = 0.06f;
            constexpr float kValidationReuseDistanceSq = kValidationReuseDistance * kValidationReuseDistance;
            if (hasLastPlacementValidation_ && lastPlacementValidationTowerId_ == selected->id &&
                glm::dot(worldPos - lastPlacementValidationPos_, worldPos - lastPlacementValidationPos_) <=
                    kValidationReuseDistanceSq) {
                lastPlacementValidationReason_ = lastPlacementValidationCachedReason_;
                return lastPlacementCanPlace_;
            }

            lastPlacementValidationReason_ = validateTowerPlacement(*selected, worldPos, kPreviewFootprintSampleCount);
            lastPlacementCanPlace_ = lastPlacementValidationReason_.empty();
            hasLastPlacementValidation_ = true;
            lastPlacementValidationTowerId_ = selected->id;
            lastPlacementValidationPos_ = worldPos;
            lastPlacementValidationCachedReason_ = lastPlacementValidationReason_;
            return lastPlacementCanPlace_;
        },
        [this, selected](const glm::vec3& worldPos) {
            if (!selected) {
                return;
            }

            // Re-validate with full footprint precision before committing the placement.
            constexpr int kConfirmFootprintSampleCount = 8;
            const std::string finalReason = validateTowerPlacement(*selected, worldPos, kConfirmFootprintSampleCount);
            if (!finalReason.empty()) {
                lastPlacementValidationReason_ = finalReason;
                lastPlacementValidationCachedReason_ = finalReason;
                lastPlacementCanPlace_ = false;
                hasLastPlacementValidation_ = true;
                lastPlacementValidationTowerId_ = selected->id;
                lastPlacementValidationPos_ = worldPos;
                return;
            }

            if (requestSpendMoney(static_cast<float>(selected->cost))) {
                const float attackIntervalSeconds = 1.0f / std::max(0.01f, selected->attackSpeed);
                const int towerPrototypeIndex = towerLoadController_.templatePrototypeIndex(selected->id);
                const int projectilePrototypeIndex = towerLoadController_.projectileTemplatePrototypeIndex(selected->id);
                placedTowers_.push_back(PlacedTower{selected->id, worldPos,
                                                    towerPrototypeIndex, projectilePrototypeIndex,
                                                    selected->attackDamage,
                                                    selected->armorPiercing,
                                                    selected->attackRange, attackIntervalSeconds, 0.0f,
                                                    selected->projectileSpeed, selected->splashRadius,
                                                    selected->chainRange, selected->ricochetRange,
                                                    std::max(1, selected->projectileCount),
                                                    std::max(1, selected->chainTargetCount),
                                                    std::max(0, selected->ricochetCount), selected->cost,
                                                    selected->damageType});
                towerPlacementController_.cancelPlacement();
                hasValidPlacementAnchor_ = false;
                bungeeInvalidActive_ = false;
                hasLastPlacementValidation_ = false;
                lastPlacementValidationCachedReason_.clear();
                lastPlacementValidationReason_.clear();
            }
        });

    // Keep reason in sync even when canPlace callback was not run this frame (e.g. no hit).
    const auto& placementState = towerPlacementController_.state();
    if (!selected) {
        lastPlacementValidationReason_ = "no tower selected";
    } else if (!placementState.hasHit) {
        lastPlacementValidationReason_ = "cursor is not over ground";
    } else if (placementState.canPlace) {
        lastPlacementValidationReason_.clear();
    } else if (lastPlacementValidationReason_.empty()) {
        constexpr int kPreviewFootprintSampleCount = 4;
        lastPlacementValidationReason_ =
            validateTowerPlacement(*selected, placementState.worldPos, kPreviewFootprintSampleCount);
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

    std::vector<AnimatedEntityInstanceSet::Instance> instances;
    instances.reserve(towerLoadController_.archetypes().size() * (TowerLoadController::kPoolPlacementsPerType + 1));

    auto pushTowerInstance = [&](const TowerArchetype& tower, int prototypeIndex, const glm::mat4& transform,
                                 const std::string& debugGroup, const std::string& debugLabel, float alpha) {
        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = transform;
        instance.prototypeIndex = prototypeIndex;
        instance.alpha = alpha;
        instance.debugGroup = debugGroup;
        instance.debugLabel = debugLabel;
        instances.push_back(std::move(instance));
    };

    const glm::mat4 hidden = glm::translate(glm::mat4{1.0f}, glm::vec3(0.0f, kTowerHiddenY, 0.0f));
    std::unordered_map<std::string, int> usedPerTower;
    for (const PlacedTower& placed : placedTowers_) {
        const TowerArchetype* tower = towerLoadController_.findArchetype(placed.towerId);
        if (!tower) {
            continue;
        }

        const int prototypeIndex = (placed.towerPrototypeIndex >= 0)
            ? placed.towerPrototypeIndex
            : towerLoadController_.templatePrototypeIndex(placed.towerId);
        if (prototypeIndex < 0) {
            continue;
        }

        const auto poolsIt = towerLoadController_.poolGroupsById().find(placed.towerId);
        if (poolsIt == towerLoadController_.poolGroupsById().end()) {
            continue;
        }

        const int poolIndex = usedPerTower[placed.towerId]++;
        if (poolIndex < 0 || poolIndex >= static_cast<int>(poolsIt->second.size())) {
            continue;
        }

        const glm::mat4 model = buildTowerModelTransform(*tower, placed.position);
        const std::string& group = poolsIt->second[poolIndex];
        pushTowerInstance(*tower, prototypeIndex, model, group, group, 1.0f);
    }

    for (const auto& [towerId, groups] : towerLoadController_.poolGroupsById()) {
        const TowerArchetype* tower = towerLoadController_.findArchetype(towerId);
        if (!tower) {
            continue;
        }
        const int prototypeIndex = towerLoadController_.templatePrototypeIndex(towerId);
        if (prototypeIndex < 0) {
            continue;
        }

        const int usedCount = usedPerTower[towerId];
        for (int i = usedCount; i < static_cast<int>(groups.size()); ++i) {
            pushTowerInstance(*tower, prototypeIndex, hidden, groups[i], groups[i], 1.0f);
        }
    }

    for (const auto& [towerId, ghostGroup] : towerLoadController_.ghostGroupById()) {
        const TowerArchetype* tower = towerLoadController_.findArchetype(towerId);
        if (!tower) {
            continue;
        }

        const int prototypeIndex = towerLoadController_.templatePrototypeIndex(towerId);
        if (prototypeIndex < 0) {
            continue;
        }

        glm::mat4 ghost = hidden;
        const TowerArchetype* selected = selectedTowerArchetype();
        const auto& placementState = towerPlacementController_.state();
        if (selected && selected->id == towerId && placementState.hasHit) {
            ghost = buildTowerModelTransform(*tower, placementState.worldPos + glm::vec3(0.0f, 0.02f, 0.0f));
        }
        pushTowerInstance(*tower, prototypeIndex, ghost, ghostGroup, ghostGroup, kTowerGhostAlpha);
    }

    for (std::size_t i = 0; i < activeProjectiles_.size(); ++i) {
        const ActiveProjectile& projectile = activeProjectiles_[i];
        const int prototypeIndex = (projectile.prototypeIndex >= 0)
            ? projectile.prototypeIndex
            : towerLoadController_.projectileTemplatePrototypeIndex(projectile.towerId);
        if (prototypeIndex < 0) {
            continue;
        }

        const TowerArchetype* tower = towerLoadController_.findArchetype(projectile.towerId);
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
                                glm::rotate(glm::mat4{1.0f}, yaw + glm::radians(tower->facingYawOffsetDegrees),
                                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, tower->renderScale)));

        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = model;
        instance.prototypeIndex = prototypeIndex;
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

    const ImVec2 renderSize((lastRenderExtent_.width > 0) ? static_cast<float>(lastRenderExtent_.width) : displaySize.x,
                            (lastRenderExtent_.height > 0) ? static_cast<float>(lastRenderExtent_.height)
                                                           : displaySize.y);

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
            if (!projectWorldToScreen(worldPoint, view, proj, displaySize, renderSize, screenPoint, depthAbs,
                                      nullptr)) {
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
    if (projectWorldToScreen(placementState.worldPos + glm::vec3(0.0f, 0.05f, 0.0f), view, proj, displaySize,
                             renderSize, centerScreen, depthAbs, nullptr)) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImU32 fill = placementState.canPlace ? IM_COL32(90, 255, 120, 85) : IM_COL32(255, 90, 90, 85);
        drawList->AddCircleFilled(centerScreen, 8.0f, fill, 24);
        drawList->AddCircle(centerScreen, 8.0f, col, 24, 2.0f);
    }
}

void PlayLevelScene::drawPlacementBoundsOverlay() const {
    if (!placementBoundsVisible_ || !worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return;
    }

    const ImVec2 renderSize((lastRenderExtent_.width > 0) ? static_cast<float>(lastRenderExtent_.width) : displaySize.x,
                            (lastRenderExtent_.height > 0) ? static_cast<float>(lastRenderExtent_.height)
                                                           : displaySize.y);

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(kDebugOverlayFovRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;
    const glm::mat4 view = buildViewMatrix();

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    auto drawBoxWireframe = [&](const std::array<glm::vec3, 8>& corners, ImU32 color) {
        ImVec2 screen[8];
        bool valid[8];
        for (int i = 0; i < 8; ++i) {
            float depthAbs = 0.0f;
            valid[i] = projectWorldToScreen(corners[i], view, proj, displaySize, renderSize, screen[i], depthAbs, nullptr);
        }
        constexpr int kEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},  // bottom face
            {4, 5}, {5, 6}, {6, 7}, {7, 4},  // top face
            {0, 4}, {1, 5}, {2, 6}, {3, 7},  // verticals
        };
        for (const auto& edge : kEdges) {
            if (valid[edge[0]] && valid[edge[1]]) {
                drawList->AddLine(screen[edge[0]], screen[edge[1]], color, 1.5f);
            }
        }
    };

    // No-build corridor auto-generated from the required waypoint route.
    const ImU32 pathColor = IM_COL32(255, 90, 90, 200);
    const std::vector<glm::vec3>& route = worldRenderer_->routePoints();
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        std::array<glm::vec3, 8> corners{};
        buildRouteSegmentCorridorBoxCorners(route[i], route[i + 1], pathCorridorHalfWidth_, corners);
        drawBoxWireframe(corners, pathColor);
    }

    // Manually-authored water/cliff placement regions.
    for (const TowerPlacementRegion& region : placementRegions_) {
        const glm::vec3& mn = region.boundsMin;
        const glm::vec3& mx = region.boundsMax;
        const std::array<glm::vec3, 8> corners = {
            glm::vec3(mn.x, mn.y, mn.z), glm::vec3(mx.x, mn.y, mn.z),
            glm::vec3(mx.x, mn.y, mx.z), glm::vec3(mn.x, mn.y, mx.z),
            glm::vec3(mn.x, mx.y, mn.z), glm::vec3(mx.x, mx.y, mn.z),
            glm::vec3(mx.x, mx.y, mx.z), glm::vec3(mn.x, mx.y, mx.z),
        };
        const ImU32 regionColor = (region.type == TowerPlacementRegionType::Water)
                                      ? IM_COL32(80, 160, 255, 200)
                                      : IM_COL32(255, 200, 60, 200);
        drawBoxWireframe(corners, regionColor);
    }
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

    const auto it = std::find_if(activeEnemies_.begin(), activeEnemies_.end(), [this](const ActiveEnemy& enemy) {
        return enemy.runtimeId == selectedEnemyRuntimeId_;
    });
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
    PlayLevelState validationState = gameplayState_;
    if (validationState.matchStatus == MatchStatus::WaitingToStart) {
        validationState.matchStatus = MatchStatus::Running;
    }

    return waveController_.validateStartWaveRequest(validationState, worldRenderer_ && worldRenderer_->isLoaded(),
                                                    worldRenderer_ && worldRenderer_->hasAnimatedEntityTemplate(),
                                                    routeController_.hasValidRoute());
}

void PlayLevelScene::updateWaveSimulation(float dt) {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }

    waveController_.updateWaveSpawning(
        gameplayState_, dt, static_cast<int>(activeEnemies_.size()), [this](const std::string& enemyId) {
            const EnemyArchetype* archetype = enemyLoadController_.findArchetype(enemyId);
            const float health = archetype ? archetype->health : 1.0f;
            const float shield = archetype ? archetype->shield : 0.0f;
            const float armor = archetype ? archetype->armor : 0.0f;
            const float moveSpeed = archetype ? archetype->moveSpeed : 1.0f;
            const float rewardMoney = archetype ? archetype->rewardMoney : 0.0f;
            const float renderScale = archetype ? archetype->renderScale : 1.0f;
            const float baseDamage = archetype ? archetype->baseDamage : 5.0f;
            const float facingYawOffsetDegrees = archetype ? archetype->facingYawOffsetDegrees : 0.0f;
            const auto resistances = archetype
                ? archetype->resistances
                : std::unordered_map<playlevel::DamageType, float, playlevel::DamageTypeHash>{};

            const float clampedHealth = std::max(1.0f, health);
            const float clampedShield = std::max(0.0f, shield);
            activeEnemies_.push_back(ActiveEnemy{enemyId,
                                                 nextEnemyRuntimeId_++,
                                                 0.0f,
                                                 clampedHealth,
                                                 clampedHealth,
                                                 clampedShield,
                                                 clampedShield,
                                                 std::max(0.0f, armor),
                                                 resistances,
                                                 std::max(0.05f, moveSpeed),
                                                 std::max(0.0f, rewardMoney),
                                                 std::max(1.0f, baseDamage),
                                                 std::max(0.01f, renderScale),
                                                 facingYawOffsetDegrees});
        });

    combatController_.advanceEnemies(dt, routeController_.totalLength(), activeEnemies_,
                                     [this](float baseDamage) { requestDamageBase(baseDamage); });
    reconcileSelectedEnemyAfterSimulation();

    combatController_.updateTowerAttacks(
        dt, [this](float distanceAlongPath) { return sampleRoutePosition(distanceAlongPath); }, placedTowers_,
        activeEnemies_, activeProjectiles_);

    combatController_.updateProjectiles(
        dt, [this](float distanceAlongPath) { return sampleRoutePosition(distanceAlongPath); }, activeEnemies_,
        activeProjectiles_);

    combatController_.collectDefeatedEnemies(
        activeEnemies_, [this](float rewardMoney) { gameplayState_.playerMoney += rewardMoney; },
        [this]() { gameplayState_.enemiesDefeated += 1; });
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

            EnemyArchetype archetype;
            if (!self->enemyLoadController_.parseEnemyArchetypeScript(scriptPath, archetype)) {
                lua_pushnil(L);
                lua_pushfstring(L, "Entity.Load failed for '%s'", scriptPath);
                return 2;
            }

            self->enemyLoadController_.registerArchetype(archetype);

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
                L, 1, self->enemyLoadController_.defaultId(),
                [self](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
                    const EnemyArchetype* archetype = self->enemyLoadController_.findArchetype(enemyId);
                    if (!archetype) {
                        return std::nullopt;
                    }
                    PlayLevelWaveController::EnemyWaveDefaults defaults;
                    defaults.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
                    return defaults;
                },
                overrideRoundDurationSeconds, error);

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

    lua_newtable(L_);
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Physical));
    lua_setfield(L_, -2, "Physical");
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Fire));
    lua_setfield(L_, -2, "Fire");
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Poison));
    lua_setfield(L_, -2, "Poison");
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Arcane));
    lua_setfield(L_, -2, "Arcane");
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Electric));
    lua_setfield(L_, -2, "Electric");
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Holy));
    lua_setfield(L_, -2, "Holy");
    lua_pushstring(L_, playlevel::damageTypeToString(playlevel::DamageType::Necrotic));
    lua_setfield(L_, -2, "Necrotic");
    lua_setfield(L_, gameplayTable, "DamageType");

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
            lua_pushnumber(L, self->gameplayState_.waveCountdownDurationSeconds);
            lua_setfield(L, -2, "waveCountdownDurationSeconds");
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
            case MatchStatus::WaitingToStart:
                status = "WaitingToStart";
                break;
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
                const TowerArchetype* archetype = self->towerLoadController_.archetypeAtLoadoutSlot(slotIdx);

                lua_pushinteger(L, static_cast<lua_Integer>(slotIdx + 1));
                lua_setfield(L, -2, "slot");
                lua_pushboolean(L, archetype != nullptr);
                lua_setfield(L, -2, "available");
                lua_pushboolean(L, self->towerPlacementController_.selectedLoadoutIndex() == slotIdx);
                lua_setfield(L, -2, "selected");

                if (archetype) {
                    lua_pushstring(L, archetype->id.c_str());
                    lua_setfield(L, -2, "id");
                    lua_pushinteger(L, self->towerLoadController_.templatePrototypeIndex(archetype->id));
                    lua_setfield(L, -2, "previewPrototypeIndex");
                    lua_pushstring(L, archetype->displayName.c_str());
                    lua_setfield(L, -2, "displayName");
                    lua_pushinteger(L, archetype->cost);
                    lua_setfield(L, -2, "cost");
                    lua_pushstring(L, playlevel::damageTypeToString(archetype->damageType));
                    lua_setfield(L, -2, "damageType");
                    lua_pushnumber(L, archetype->attackDamage);
                    lua_setfield(L, -2, "attackDamage");
                    lua_pushnumber(L, archetype->armorPiercing);
                    lua_setfield(L, -2, "armorPiercing");
                    lua_pushnumber(L, archetype->attackRange);
                    lua_setfield(L, -2, "attackRange");
                    lua_pushnumber(L, archetype->attackSpeed);
                    lua_setfield(L, -2, "attackSpeed");
                    lua_pushnumber(L, archetype->splashRadius);
                    lua_setfield(L, -2, "splashRadius");
                    lua_pushnumber(L, archetype->chainRange);
                    lua_setfield(L, -2, "chainRange");
                    lua_pushnumber(L, archetype->ricochetRange);
                    lua_setfield(L, -2, "ricochetRange");
                    lua_pushinteger(L, archetype->projectileCount);
                    lua_setfield(L, -2, "projectileCount");
                    lua_pushinteger(L, archetype->chainTargetCount);
                    lua_setfield(L, -2, "chainTargetCount");
                    lua_pushinteger(L, archetype->ricochetCount);
                    lua_setfield(L, -2, "ricochetCount");
                    lua_pushstring(L, archetype->modelPath.c_str());
                    lua_setfield(L, -2, "modelPath");
                    lua_pushstring(L, archetype->projectileModelPath.c_str());
                    lua_setfield(L, -2, "projectileModelPath");
                    lua_pushstring(L, archetype->previewImagePath.c_str());
                    lua_setfield(L, -2, "previewImagePath");
                    const std::string textureId = TowerLoadController::makeIconTextureId(archetype->id);
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
            const char* towerIdRaw = luaL_checkstring(L, 1);
            const std::string towerId = towerIdRaw ? towerIdRaw : "";

            const TowerArchetype* archetype = self->towerLoadController_.findArchetype(towerId);
            if (!archetype) {
                lua_pushnil(L);
                lua_pushstring(L, "tower not found");
                return 2;
            }

            lua_newtable(L);
            lua_pushstring(L, archetype->id.c_str());
            lua_setfield(L, -2, "towerId");
            lua_pushstring(L, archetype->displayName.c_str());
            lua_setfield(L, -2, "displayName");
            lua_newtable(L);
            lua_pushstring(L, archetype->upgradeUi.defaultNodeIconPath.c_str());
            lua_setfield(L, -2, "defaultNodeIcon");
            lua_setfield(L, -2, "ui");

            lua_newtable(L);
            for (std::size_t i = 0; i < archetype->upgradeNodes.size(); ++i) {
                const TowerArchetype::UpgradeNode& node = archetype->upgradeNodes[i];
                lua_newtable(L);

                lua_pushstring(L, node.id.c_str());
                lua_setfield(L, -2, "id");
                lua_pushstring(L, node.displayName.c_str());
                lua_setfield(L, -2, "displayName");
                lua_pushstring(L, node.description.c_str());
                lua_setfield(L, -2, "description");
                lua_pushstring(L, node.iconPath.c_str());
                lua_setfield(L, -2, "icon");
                lua_pushstring(L, node.parentId.c_str());
                lua_setfield(L, -2, "parent");
                lua_pushstring(L, node.towerModelPathOverride.c_str());
                lua_setfield(L, -2, "towerModel");
                lua_pushstring(L, node.projectileModelPathOverride.c_str());
                lua_setfield(L, -2, "projectileModel");
                lua_pushstring(L, node.branch.c_str());
                lua_setfield(L, -2, "branch");
                lua_pushinteger(L, node.cost);
                lua_setfield(L, -2, "cost");
                lua_pushinteger(L, node.tier);
                lua_setfield(L, -2, "tier");
                lua_pushinteger(L, node.column);
                lua_setfield(L, -2, "column");
                lua_pushinteger(L, std::max(1, node.maxLevel));
                lua_setfield(L, -2, "maxLevel");
                lua_pushinteger(L, std::max(0, node.minUpgradesRequired));
                lua_setfield(L, -2, "minUpgradesRequired");

                lua_newtable(L);
                for (std::size_t orderIdx = 0; orderIdx < node.childrenOrder.size(); ++orderIdx) {
                    lua_pushstring(L, node.childrenOrder[orderIdx].c_str());
                    lua_seti(L, -2, static_cast<lua_Integer>(orderIdx + 1));
                }
                lua_setfield(L, -2, "childrenOrder");

                lua_newtable(L);
                for (std::size_t reqIdx = 0; reqIdx < node.requiredNodeIds.size(); ++reqIdx) {
                    lua_pushstring(L, node.requiredNodeIds[reqIdx].c_str());
                    lua_seti(L, -2, static_cast<lua_Integer>(reqIdx + 1));
                }
                lua_setfield(L, -2, "requires");

                lua_newtable(L);
                for (std::size_t exIdx = 0; exIdx < node.excludes.size(); ++exIdx) {
                    lua_pushstring(L, node.excludes[exIdx].c_str());
                    lua_seti(L, -2, static_cast<lua_Integer>(exIdx + 1));
                }
                lua_setfield(L, -2, "excludes");

                lua_newtable(L);
                lua_pushnumber(L, node.effects.attackDamageAdd);
                lua_setfield(L, -2, "attackDamageAdd");
                lua_pushnumber(L, node.effects.attackDamageMul);
                lua_setfield(L, -2, "attackDamageMul");
                lua_pushnumber(L, node.effects.attackRangeAdd);
                lua_setfield(L, -2, "attackRangeAdd");
                lua_pushnumber(L, node.effects.attackRangeMul);
                lua_setfield(L, -2, "attackRangeMul");
                lua_pushnumber(L, node.effects.attackSpeedAdd);
                lua_setfield(L, -2, "attackSpeedAdd");
                lua_pushnumber(L, node.effects.attackSpeedMul);
                lua_setfield(L, -2, "attackSpeedMul");
                lua_pushnumber(L, node.effects.projectileSpeedAdd);
                lua_setfield(L, -2, "projectileSpeedAdd");
                lua_pushnumber(L, node.effects.projectileSpeedMul);
                lua_setfield(L, -2, "projectileSpeedMul");
                lua_pushnumber(L, node.effects.splashRadiusAdd);
                lua_setfield(L, -2, "splashRadiusAdd");
                lua_pushnumber(L, node.effects.splashRadiusMul);
                lua_setfield(L, -2, "splashRadiusMul");
                lua_pushnumber(L, node.effects.chainRangeAdd);
                lua_setfield(L, -2, "chainRangeAdd");
                lua_pushnumber(L, node.effects.chainRangeMul);
                lua_setfield(L, -2, "chainRangeMul");
                lua_pushnumber(L, node.effects.ricochetRangeAdd);
                lua_setfield(L, -2, "ricochetRangeAdd");
                lua_pushnumber(L, node.effects.ricochetRangeMul);
                lua_setfield(L, -2, "ricochetRangeMul");
                lua_pushinteger(L, node.effects.projectileCountAdd);
                lua_setfield(L, -2, "projectileCountAdd");
                lua_pushinteger(L, node.effects.chainTargetCountAdd);
                lua_setfield(L, -2, "chainTargetCountAdd");
                lua_pushinteger(L, node.effects.ricochetCountAdd);
                lua_setfield(L, -2, "ricochetCountAdd");
                lua_setfield(L, -2, "effects");

                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(L, -2, "nodes");

            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getTowerUpgradeTree");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);

            std::string selectedTowerId;
            int selectedPoolIndex = -1;
            const bool hasTowerSelection =
                self->pickingController_.selectedSelection().valid &&
                parseTowerPoolGroup(self->pickingController_.selectedSelection().group, selectedTowerId, selectedPoolIndex);

            if (!hasTowerSelection) {
                lua_pushboolean(L, 0);
                lua_setfield(L, -2, "valid");
                lua_pushstring(L, "select a placed tower");
                lua_setfield(L, -2, "reason");
                return 1;
            }

            const PlacedTower* placedTower = self->findPlacedTowerByPoolKey(selectedTowerId, selectedPoolIndex);
            const TowerArchetype* archetype = self->towerLoadController_.findArchetype(selectedTowerId);
            if (!placedTower || !archetype) {
                lua_pushboolean(L, 0);
                lua_setfield(L, -2, "valid");
                lua_pushstring(L, "tower state not found");
                lua_setfield(L, -2, "reason");
                return 1;
            }

            float effectiveAttackDamage = placedTower->attackDamage;
            float effectiveAttackRange = placedTower->attackRange;
            float effectiveAttackSpeed = 1.0f / std::max(0.01f, placedTower->attackIntervalSeconds);
            float effectiveProjectileSpeed = placedTower->projectileSpeed;
            float effectiveSplashRadius = placedTower->splashRadius;
            float effectiveChainRange = placedTower->chainRange;
            float effectiveRicochetRange = placedTower->ricochetRange;
            int effectiveProjectileCount = placedTower->projectileCount;
            int effectiveChainTargetCount = placedTower->chainTargetCount;
            int effectiveRicochetCount = placedTower->ricochetCount;

            self->applyTowerUpgradeEffects(*archetype, *placedTower, effectiveAttackDamage, effectiveAttackRange,
                                           effectiveAttackSpeed, effectiveProjectileSpeed, effectiveSplashRadius,
                                           effectiveChainRange, effectiveRicochetRange, effectiveProjectileCount,
                                           effectiveChainTargetCount, effectiveRicochetCount);

            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "valid");
            lua_pushstring(L, archetype->id.c_str());
            lua_setfield(L, -2, "towerId");
            lua_pushstring(L, archetype->displayName.c_str());
            lua_setfield(L, -2, "displayName");
            lua_pushinteger(L, selectedPoolIndex + 1);
            lua_setfield(L, -2, "towerInstanceOrdinal");

            lua_pushinteger(L, archetype->cost);
            lua_setfield(L, -2, "baseCost");
            lua_pushstring(L, playlevel::damageTypeToString(archetype->damageType));
            lua_setfield(L, -2, "damageType");
            lua_pushnumber(L, archetype->attackDamage);
            lua_setfield(L, -2, "baseAttackDamage");
            lua_pushnumber(L, archetype->armorPiercing);
            lua_setfield(L, -2, "baseArmorPiercing");
            lua_pushnumber(L, archetype->attackRange);
            lua_setfield(L, -2, "baseAttackRange");
            lua_pushnumber(L, archetype->attackSpeed);
            lua_setfield(L, -2, "baseAttackSpeed");
            lua_pushnumber(L, archetype->projectileSpeed);
            lua_setfield(L, -2, "baseProjectileSpeed");
            lua_pushnumber(L, archetype->splashRadius);
            lua_setfield(L, -2, "baseSplashRadius");
            lua_pushnumber(L, archetype->chainRange);
            lua_setfield(L, -2, "baseChainRange");
            lua_pushnumber(L, archetype->ricochetRange);
            lua_setfield(L, -2, "baseRicochetRange");
            lua_pushinteger(L, archetype->projectileCount);
            lua_setfield(L, -2, "baseProjectileCount");
            lua_pushinteger(L, archetype->chainTargetCount);
            lua_setfield(L, -2, "baseChainTargetCount");
            lua_pushinteger(L, archetype->ricochetCount);
            lua_setfield(L, -2, "baseRicochetCount");

            lua_pushnumber(L, effectiveAttackDamage);
            lua_setfield(L, -2, "attackDamage");
            lua_pushnumber(L, std::max(0.0f, placedTower->armorPiercing));
            lua_setfield(L, -2, "armorPiercing");
            lua_pushnumber(L, effectiveAttackRange);
            lua_setfield(L, -2, "attackRange");
            lua_pushnumber(L, effectiveAttackSpeed);
            lua_setfield(L, -2, "attackSpeed");
            lua_pushnumber(L, effectiveProjectileSpeed);
            lua_setfield(L, -2, "projectileSpeed");
            lua_pushnumber(L, effectiveSplashRadius);
            lua_setfield(L, -2, "splashRadius");
            lua_pushnumber(L, effectiveChainRange);
            lua_setfield(L, -2, "chainRange");
            lua_pushnumber(L, effectiveRicochetRange);
            lua_setfield(L, -2, "ricochetRange");
            lua_pushinteger(L, effectiveProjectileCount);
            lua_setfield(L, -2, "projectileCount");
            lua_pushinteger(L, effectiveChainTargetCount);
            lua_setfield(L, -2, "chainTargetCount");
            lua_pushinteger(L, effectiveRicochetCount);
            lua_setfield(L, -2, "ricochetCount");

            lua_newtable(L);
            lua_pushstring(L, archetype->upgradeUi.panelTitle.c_str());
            lua_setfield(L, -2, "panelTitle");
            lua_pushstring(L, archetype->upgradeUi.artPath.c_str());
            lua_setfield(L, -2, "artPath");
            lua_pushstring(L, archetype->upgradeUi.defaultNodeIconPath.c_str());
            lua_setfield(L, -2, "defaultNodeIcon");

            lua_newtable(L);
            lua_pushnumber(L, archetype->upgradeUi.accentR);
            lua_seti(L, -2, 1);
            lua_pushnumber(L, archetype->upgradeUi.accentG);
            lua_seti(L, -2, 2);
            lua_pushnumber(L, archetype->upgradeUi.accentB);
            lua_seti(L, -2, 3);
            lua_setfield(L, -2, "accent");

            lua_newtable(L);
            lua_pushnumber(L, archetype->upgradeUi.unlockedR);
            lua_seti(L, -2, 1);
            lua_pushnumber(L, archetype->upgradeUi.unlockedG);
            lua_seti(L, -2, 2);
            lua_pushnumber(L, archetype->upgradeUi.unlockedB);
            lua_seti(L, -2, 3);
            lua_setfield(L, -2, "unlocked");

            lua_newtable(L);
            lua_pushnumber(L, archetype->upgradeUi.lockedR);
            lua_seti(L, -2, 1);
            lua_pushnumber(L, archetype->upgradeUi.lockedG);
            lua_seti(L, -2, 2);
            lua_pushnumber(L, archetype->upgradeUi.lockedB);
            lua_seti(L, -2, 3);
            lua_setfield(L, -2, "locked");
            lua_setfield(L, -2, "ui");

            lua_newtable(L);
            for (std::size_t i = 0; i < placedTower->unlockedUpgradeNodeIds.size(); ++i) {
                lua_pushstring(L, placedTower->unlockedUpgradeNodeIds[i].c_str());
                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(L, -2, "unlockedNodes");

            lua_newtable(L);
            for (std::size_t i = 0; i < archetype->upgradeNodes.size(); ++i) {
                const TowerArchetype::UpgradeNode& node = archetype->upgradeNodes[i];
                lua_newtable(L);

                const int currentLevel = static_cast<int>(std::count(placedTower->unlockedUpgradeNodeIds.begin(),
                                                                      placedTower->unlockedUpgradeNodeIds.end(),
                                                                      node.id));
                const bool unlocked = currentLevel > 0;
                const int maxLevel = std::max(1, node.maxLevel);
                const bool canLevelUp = currentLevel < maxLevel;
                const std::string reason = canLevelUp
                    ? self->validateTowerUpgradeUnlock(*archetype, *placedTower, node.id)
                    : std::string("upgrade is at max level");
                const bool canUnlock = canLevelUp && reason.empty();

                lua_pushstring(L, node.id.c_str());
                lua_setfield(L, -2, "id");
                lua_pushstring(L, node.displayName.c_str());
                lua_setfield(L, -2, "displayName");
                lua_pushstring(L, node.description.c_str());
                lua_setfield(L, -2, "description");
                lua_pushstring(L, node.iconPath.c_str());
                lua_setfield(L, -2, "icon");
                lua_pushstring(L, node.parentId.c_str());
                lua_setfield(L, -2, "parent");
                lua_pushstring(L, node.towerModelPathOverride.c_str());
                lua_setfield(L, -2, "towerModel");
                lua_pushstring(L, node.projectileModelPathOverride.c_str());
                lua_setfield(L, -2, "projectileModel");
                lua_pushstring(L, node.branch.c_str());
                lua_setfield(L, -2, "branch");
                lua_pushinteger(L, node.cost);
                lua_setfield(L, -2, "cost");
                lua_pushinteger(L, node.tier);
                lua_setfield(L, -2, "tier");
                lua_pushinteger(L, node.column);
                lua_setfield(L, -2, "column");
                lua_pushinteger(L, maxLevel);
                lua_setfield(L, -2, "maxLevel");
                lua_pushinteger(L, currentLevel);
                lua_setfield(L, -2, "currentLevel");
                lua_pushinteger(L, std::max(0, node.minUpgradesRequired));
                lua_setfield(L, -2, "minUpgradesRequired");
                lua_pushboolean(L, unlocked ? 1 : 0);
                lua_setfield(L, -2, "unlocked");
                lua_pushboolean(L, canUnlock ? 1 : 0);
                lua_setfield(L, -2, "canUnlock");
                lua_pushstring(L, reason.c_str());
                lua_setfield(L, -2, "reason");

                lua_newtable(L);
                for (std::size_t orderIdx = 0; orderIdx < node.childrenOrder.size(); ++orderIdx) {
                    lua_pushstring(L, node.childrenOrder[orderIdx].c_str());
                    lua_seti(L, -2, static_cast<lua_Integer>(orderIdx + 1));
                }
                lua_setfield(L, -2, "childrenOrder");

                lua_newtable(L);
                for (std::size_t reqIdx = 0; reqIdx < node.requiredNodeIds.size(); ++reqIdx) {
                    lua_pushstring(L, node.requiredNodeIds[reqIdx].c_str());
                    lua_seti(L, -2, static_cast<lua_Integer>(reqIdx + 1));
                }
                lua_setfield(L, -2, "requires");

                lua_newtable(L);
                for (std::size_t exIdx = 0; exIdx < node.excludes.size(); ++exIdx) {
                    lua_pushstring(L, node.excludes[exIdx].c_str());
                    lua_seti(L, -2, static_cast<lua_Integer>(exIdx + 1));
                }
                lua_setfield(L, -2, "excludes");

                lua_newtable(L);
                lua_pushnumber(L, node.effects.attackDamageAdd);
                lua_setfield(L, -2, "attackDamageAdd");
                lua_pushnumber(L, node.effects.attackDamageMul);
                lua_setfield(L, -2, "attackDamageMul");
                lua_pushnumber(L, node.effects.attackRangeAdd);
                lua_setfield(L, -2, "attackRangeAdd");
                lua_pushnumber(L, node.effects.attackRangeMul);
                lua_setfield(L, -2, "attackRangeMul");
                lua_pushnumber(L, node.effects.attackSpeedAdd);
                lua_setfield(L, -2, "attackSpeedAdd");
                lua_pushnumber(L, node.effects.attackSpeedMul);
                lua_setfield(L, -2, "attackSpeedMul");
                lua_pushnumber(L, node.effects.projectileSpeedAdd);
                lua_setfield(L, -2, "projectileSpeedAdd");
                lua_pushnumber(L, node.effects.projectileSpeedMul);
                lua_setfield(L, -2, "projectileSpeedMul");
                lua_pushnumber(L, node.effects.splashRadiusAdd);
                lua_setfield(L, -2, "splashRadiusAdd");
                lua_pushnumber(L, node.effects.splashRadiusMul);
                lua_setfield(L, -2, "splashRadiusMul");
                lua_pushnumber(L, node.effects.chainRangeAdd);
                lua_setfield(L, -2, "chainRangeAdd");
                lua_pushnumber(L, node.effects.chainRangeMul);
                lua_setfield(L, -2, "chainRangeMul");
                lua_pushnumber(L, node.effects.ricochetRangeAdd);
                lua_setfield(L, -2, "ricochetRangeAdd");
                lua_pushnumber(L, node.effects.ricochetRangeMul);
                lua_setfield(L, -2, "ricochetRangeMul");
                lua_pushinteger(L, node.effects.projectileCountAdd);
                lua_setfield(L, -2, "projectileCountAdd");
                lua_pushinteger(L, node.effects.chainTargetCountAdd);
                lua_setfield(L, -2, "chainTargetCountAdd");
                lua_pushinteger(L, node.effects.ricochetCountAdd);
                lua_setfield(L, -2, "ricochetCountAdd");
                lua_setfield(L, -2, "effects");

                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(L, -2, "nodes");
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getSelectedTowerUpgradeState");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);

            const auto& selected = self->pickingController_.selectedSelection();
            if (!selected.valid || selected.instanceIndex < 0 ||
                selected.instanceIndex >= static_cast<int>(self->activeEnemies_.size())) {
                lua_pushboolean(L, 0);
                lua_setfield(L, -2, "valid");
                lua_pushstring(L, "select an enemy");
                lua_setfield(L, -2, "reason");
                return 1;
            }

            const ActiveEnemy& enemy = self->activeEnemies_[static_cast<std::size_t>(selected.instanceIndex)];
            const EnemyArchetype* archetype = self->enemyLoadController_.findArchetype(enemy.enemyId);

            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "valid");
            lua_pushstring(L, enemy.enemyId.c_str());
            lua_setfield(L, -2, "enemyId");
            lua_pushinteger(L, static_cast<lua_Integer>(selected.instanceIndex + 1));
            lua_setfield(L, -2, "instanceOrdinal");
            lua_pushinteger(L, static_cast<lua_Integer>(enemy.runtimeId));
            lua_setfield(L, -2, "runtimeId");

            const std::string displayName = archetype ? archetype->displayName : enemy.enemyId;
            const std::string description = archetype ? archetype->description : std::string();
            lua_pushstring(L, displayName.c_str());
            lua_setfield(L, -2, "displayName");
            lua_pushstring(L, description.c_str());
            lua_setfield(L, -2, "description");

            lua_pushnumber(L, enemy.health);
            lua_setfield(L, -2, "health");
            lua_pushnumber(L, enemy.maxHealth);
            lua_setfield(L, -2, "maxHealth");
            lua_pushnumber(L, enemy.shield);
            lua_setfield(L, -2, "shield");
            lua_pushnumber(L, enemy.maxShield);
            lua_setfield(L, -2, "maxShield");
            lua_pushnumber(L, enemy.moveSpeed);
            lua_setfield(L, -2, "moveSpeed");
            lua_pushnumber(L, enemy.baseDamage);
            lua_setfield(L, -2, "baseDamage");
            lua_pushnumber(L, enemy.armor);
            lua_setfield(L, -2, "armor");
            lua_pushnumber(L, enemy.rewardMoney);
            lua_setfield(L, -2, "rewardMoney");

            lua_newtable(L);
            int resistanceIndex = 1;
            for (const auto& [damageType, percent] : enemy.resistances) {
                lua_newtable(L);
                lua_pushstring(L, playlevel::damageTypeToString(damageType));
                lua_setfield(L, -2, "damageType");
                lua_pushnumber(L, percent);
                lua_setfield(L, -2, "percent");
                lua_seti(L, -2, resistanceIndex++);
            }
            lua_setfield(L, -2, "resistances");

            if (selected.distance > 0.0f) {
                lua_pushnumber(L, selected.distance);
                lua_setfield(L, -2, "selectionDistance");
            }

            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getSelectedEnemyInfo");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const std::string nodeId = luaL_checkstring(L, 1);
            if (nodeId.empty()) {
                return pushCommandResult(L, false, "node id is required");
            }

            std::string selectedTowerId;
            int selectedPoolIndex = -1;
            const bool hasTowerSelection =
                self->pickingController_.selectedSelection().valid &&
                parseTowerPoolGroup(self->pickingController_.selectedSelection().group, selectedTowerId, selectedPoolIndex);
            if (!hasTowerSelection) {
                return pushCommandResult(L, false, "select a placed tower");
            }

            PlacedTower* placedTower = self->findPlacedTowerByPoolKey(selectedTowerId, selectedPoolIndex);
            if (!placedTower) {
                return pushCommandResult(L, false, "tower state not found");
            }

            std::string reason;
            const bool unlocked = self->unlockTowerUpgrade(*placedTower, nodeId, reason);
            return pushCommandResult(L, unlocked, reason.c_str());
        },
        1);
    lua_setfield(L_, gameplayTable, "requestSelectedTowerUpgrade");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            self->towerPreviewPanels_.clear();

            if (!lua_istable(L, 1)) {
                return 0;
            }

            const int count = static_cast<int>(lua_rawlen(L, 1));
            self->towerPreviewPanels_.reserve(static_cast<std::size_t>(count));
            for (int i = 1; i <= count; ++i) {
                lua_geti(L, 1, i);
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    continue;
                }

                TowerPreviewPanel panel;

                lua_getfield(L, -1, "prototypeIndex");
                if (lua_isinteger(L, -1)) {
                    panel.prototypeIndex = static_cast<int>(lua_tointeger(L, -1));
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "x");
                if (lua_isnumber(L, -1)) {
                    panel.x = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "y");
                if (lua_isnumber(L, -1)) {
                    panel.y = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "w");
                if (lua_isnumber(L, -1)) {
                    panel.width = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "h");
                if (lua_isnumber(L, -1)) {
                    panel.height = static_cast<float>(lua_tonumber(L, -1));
                }
                lua_pop(L, 1);

                if (panel.prototypeIndex >= 0 && panel.width > 1.0f && panel.height > 1.0f) {
                    self->towerPreviewPanels_.push_back(panel);
                }

                lua_pop(L, 1);
            }
            return 0;
        },
        1);
    lua_setfield(L_, gameplayTable, "setTowerPreviewSlots");

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
            const TowerArchetype* tower = self->towerLoadController_.archetypeAtLoadoutSlot(slotIdx);
            if (!tower) {
                return pushCommandResult(L, false, "loadout slot is empty");
            }

            self->towerPlacementController_.setSelectedLoadoutIndex(slotIdx);
            self->clearActiveSelectionForTowerPlacement("loadout tower selected");
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

            const TowerArchetype* tower = self->selectedTowerArchetype();
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

            lua_newtable(L);
            lua_pushboolean(L, self->bungeeInvalidActive_);
            lua_setfield(L, -2, "bungeeActive");
            lua_pushboolean(L, self->hasValidPlacementAnchor_);
            lua_setfield(L, -2, "hasAnchor");

            lua_newtable(L);
            lua_pushnumber(L, self->bungeeAnchorPos_.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->bungeeAnchorPos_.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->bungeeAnchorPos_.z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "anchorPos");

            lua_newtable(L);
            lua_pushnumber(L, self->bungeeResolvedPos_.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->bungeeResolvedPos_.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->bungeeResolvedPos_.z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "resolvedPos");

            lua_pushboolean(L, self->hasLastRawPlacementCandidate_);
            lua_setfield(L, -2, "hasRawCandidate");
            lua_pushboolean(L, self->lastRawPlacementCandidateValid_);
            lua_setfield(L, -2, "rawCandidateValid");
            lua_newtable(L);
            lua_pushnumber(L, self->lastRawPlacementCandidatePos_.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->lastRawPlacementCandidatePos_.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->lastRawPlacementCandidatePos_.z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "rawCandidatePos");
            lua_pushnumber(L, glm::distance(self->bungeeAnchorPos_, self->lastRawPlacementCandidatePos_));
            lua_setfield(L, -2, "anchorToCandidateDistance");
            lua_pushnumber(L, glm::distance(self->bungeeResolvedPos_, self->lastRawPlacementCandidatePos_));
            lua_setfield(L, -2, "resolvedToCandidateDistance");
            lua_setfield(L, -2, "debug");

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
                } else if (placementState.canPlace) {
                    reason.clear();
                } else {
                    reason = self->lastPlacementValidationReason_;
                    if (reason.empty()) {
                        reason = self->validateTowerPlacement(*tower, placementState.worldPos);
                    }
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
            lua_pushnumber(L, self->maxTowerPlacementSlopeDegrees_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getMaxTowerPlacementSlopeDegrees");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const float degrees = static_cast<float>(luaL_checknumber(L, 1));
            if (degrees < 0.0f || degrees > 90.0f) {
                return pushCommandResult(L, false, "degrees must be between 0 and 90");
            }
            self->maxTowerPlacementSlopeDegrees_ = degrees;
            return pushCommandResult(L, true, "updated");
        },
        1);
    lua_setfield(L_, gameplayTable, "setMaxTowerPlacementSlopeDegrees");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushnumber(L, self->pathCorridorHalfWidth_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getPathCorridorHalfWidth");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const float halfWidth = static_cast<float>(luaL_checknumber(L, 1));
            if (halfWidth < 0.0f || halfWidth > 25.0f) {
                return pushCommandResult(L, false, "halfWidth must be between 0 and 25");
            }
            self->pathCorridorHalfWidth_ = halfWidth;
            return pushCommandResult(L, true, "updated");
        },
        1);
    lua_setfield(L_, gameplayTable, "setPathCorridorHalfWidth");

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

    auto registerPlayOggFn = [&](const char* fieldName, AudioChannel channel) {
        lua_pushlightuserdata(L_, this);
        lua_pushinteger(L_, static_cast<lua_Integer>(channel));
        lua_pushcclosure(
            L_,
            [](lua_State* L) -> int {
                auto* self = luaSceneSelf(L);
                const AudioChannel channel = static_cast<AudioChannel>(lua_tointeger(L, lua_upvalueindex(2)));
                const std::string path = luaL_checkstring(L, 1);
                if (path.empty()) {
                    return pushCommandResult(L, false, "expected a non-empty audio file path");
                }

                bool loop = false;
                float gain = 1.0f;

                if (lua_gettop(L) >= 2) {
                    if (lua_istable(L, 2)) {
                        lua_getfield(L, 2, "loop");
                        if (!lua_isnil(L, -1)) {
                            loop = lua_toboolean(L, -1) != 0;
                        }
                        lua_pop(L, 1);

                        lua_getfield(L, 2, "gain");
                        if (lua_isnumber(L, -1)) {
                            gain = static_cast<float>(lua_tonumber(L, -1));
                        }
                        lua_pop(L, 1);
                    } else if (lua_isboolean(L, 2)) {
                        loop = lua_toboolean(L, 2) != 0;
                        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) {
                            gain = static_cast<float>(lua_tonumber(L, 3));
                        }
                    } else if (lua_isnumber(L, 2)) {
                        gain = static_cast<float>(lua_tonumber(L, 2));
                    } else {
                        return pushCommandResult(L, false, "expected options table, loop flag, or gain");
                    }
                }

                self->requestPlayAudio(path, channel, loop, gain);
                return pushCommandResult(L, true, "queued");
            },
            2);
        lua_setfield(L_, gameplayTable, fieldName);
    };

    registerPlayOggFn("playMusic", AudioChannel::Music);
    registerPlayOggFn("playSfx", AudioChannel::Sfx);

    auto registerPreloadAudioFn = [&](const char* fieldName, AudioChannel channel) {
        lua_pushlightuserdata(L_, this);
        lua_pushinteger(L_, static_cast<lua_Integer>(channel));
        lua_pushcclosure(
            L_,
            [](lua_State* L) -> int {
                auto* self = luaSceneSelf(L);
                const AudioChannel channel = static_cast<AudioChannel>(lua_tointeger(L, lua_upvalueindex(2)));
                const std::string path = luaL_checkstring(L, 1);
                if (path.empty()) {
                    return pushCommandResult(L, false, "expected a non-empty audio file path");
                }

                self->requestPreloadAudio(path, channel);
                return pushCommandResult(L, true, "queued");
            },
            2);
        lua_setfield(L_, gameplayTable, fieldName);
    };

    registerPreloadAudioFn("preloadSfx", AudioChannel::Sfx);

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
            self->placementBoundsVisible_ = lua_toboolean(L, 1) != 0;
            lua_pushboolean(L, self->placementBoundsVisible_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "setDebugPlacementBoundsVisible");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->placementBoundsVisible_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getDebugPlacementBoundsVisible");

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
