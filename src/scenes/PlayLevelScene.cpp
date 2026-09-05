#include "scenes/PlayLevelScene.hpp"

#include "LuaStateBootstrap.hpp"
#include "VulkanContext.hpp"
#include "multiplayer/ContentHash.hpp"
#include "multiplayer/MatchSnapshotBuilder.hpp"
#include "scenes/SceneSharedState.hpp"
#include "scenes/TowerPlacementRules.hpp"
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
#include <type_traits>
#include <variant>

PlayLevelScene::PlayLevelScene()
        : GameScene(), gameplayState_(matchSimulation_.gameplayState()), placedTowers_(matchSimulation_.placedTowers()),
    combatController_(matchSimulation_.combatController()), activeProjectiles_(matchSimulation_.activeProjectiles()),
    nextTowerRuntimeId_(matchSimulation_.nextTowerRuntimeId()), nextEnemyRuntimeId_(matchSimulation_.nextEnemyRuntimeId()),
    nextProjectileRuntimeId_(matchSimulation_.nextProjectileRuntimeId()), waveController_(matchSimulation_.waveController()),
    activeEnemies_(matchSimulation_.activeEnemies()), towerLoadController_(L_), enemyLoadController_(L_) {}
PlayLevelScene::~PlayLevelScene() = default;

namespace {

constexpr float kDebugOverlayFovRadians = glm::radians(60.0f);
constexpr float kTowerHiddenY = -10000.0f;
constexpr float kTowerGhostAlpha = 0.45f;
constexpr multiplayer::PlayerId kLocalHostPlayerId = 1;
// How often (in fixed ticks) the host publishes a snapshot to connected remote peers.
constexpr multiplayer::SimulationTick kSnapshotIntervalTicks = 3;

multiplayer::TowerTargetingMode toMultiplayerTargetingMode(playlevel::TowerTargetingMode mode) {
    switch (mode) {
    case playlevel::TowerTargetingMode::First:
        return multiplayer::TowerTargetingMode::First;
    case playlevel::TowerTargetingMode::Last:
        return multiplayer::TowerTargetingMode::Last;
    case playlevel::TowerTargetingMode::Nearest:
        return multiplayer::TowerTargetingMode::Nearest;
    case playlevel::TowerTargetingMode::Random:
        return multiplayer::TowerTargetingMode::Random;
    case playlevel::TowerTargetingMode::HighestHp:
        return multiplayer::TowerTargetingMode::HighestHp;
    case playlevel::TowerTargetingMode::LowestHp:
        return multiplayer::TowerTargetingMode::LowestHp;
    }
    return multiplayer::TowerTargetingMode::First;
}

playlevel::TowerTargetingMode toGameplayTargetingMode(multiplayer::TowerTargetingMode mode) {
    switch (mode) {
    case multiplayer::TowerTargetingMode::First:
        return playlevel::TowerTargetingMode::First;
    case multiplayer::TowerTargetingMode::Last:
        return playlevel::TowerTargetingMode::Last;
    case multiplayer::TowerTargetingMode::Nearest:
        return playlevel::TowerTargetingMode::Nearest;
    case multiplayer::TowerTargetingMode::Random:
        return playlevel::TowerTargetingMode::Random;
    case multiplayer::TowerTargetingMode::HighestHp:
        return playlevel::TowerTargetingMode::HighestHp;
    case multiplayer::TowerTargetingMode::LowestHp:
        return playlevel::TowerTargetingMode::LowestHp;
    }
    return playlevel::TowerTargetingMode::First;
}

// Last-resort default clip name, used only for the one-time Idle-on-load initialization when no
// enemy archetype is registered yet to supply EnemyArchetype::idleClipName. Every other clip
// decision (Walking, Death) is sourced per-enemy from ActiveEnemy::walkingClipName/deathClipName
// (see EnemyLoadController.hpp), which default to "Idle"/"Walking"/"Death" -- the goblin_scout/
// goblin1 rig convention -- but a Lua archetype can override them. Every lookup already tolerates
// a clip name the model doesn't have (falls back to whatever TemplateAnimator auto-selected).
constexpr const char* kEnemyClipIdle = "Idle";

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

const char* joinRejectionReasonToString(multiplayer::JoinRejectionReason reason) {
    switch (reason) {
    case multiplayer::JoinRejectionReason::Unspecified:
        return "unspecified";
    case multiplayer::JoinRejectionReason::ProtocolVersionUnsupported:
        return "protocol version unsupported";
    case multiplayer::JoinRejectionReason::ContentManifestMismatch:
        return "content manifest mismatch";
    case multiplayer::JoinRejectionReason::MatchUnavailable:
        return "match unavailable";
    case multiplayer::JoinRejectionReason::MatchFull:
        return "match full";
    }
    return "unknown";
}

int findEnemyPrototypeIndex(const WorldAssetSpec& assetSpec, const EnemyLoadController& enemyLoadController,
                            const std::string& enemyId) {
    const EnemyArchetype* archetype = enemyLoadController.findArchetype(enemyId);
    if (!archetype || archetype->modelPath.empty()) {
        return 0;
    }

    const std::filesystem::path archetypeModelPath = std::filesystem::path(archetype->modelPath).lexically_normal();
    for (std::size_t i = 0; i < assetSpec.animatedTemplateModelPaths.size(); ++i) {
        if (assetSpec.animatedTemplateModelPaths[i].lexically_normal() == archetypeModelPath) {
            return static_cast<int>(i);
        }
    }

    return 0;
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

const TowerArchetype::UpgradeNode::UpgradeLevel* getUpgradeLevelData(const TowerArchetype::UpgradeNode& node,
                                                                     int levelIndex) {
    if (levelIndex < 0 || levelIndex >= static_cast<int>(node.upgradeLevels.size())) {
        return nullptr;
    }
    return &node.upgradeLevels[static_cast<std::size_t>(levelIndex)];
}

int getUpgradeMaxLevel(const TowerArchetype::UpgradeNode& node) {
    return std::max(1, static_cast<int>(node.upgradeLevels.size()));
}

void pushUpgradeEffectsTable(lua_State* L, const TowerArchetype::UpgradeEffects& effects) {
    lua_newtable(L);
    lua_pushnumber(L, effects.attackDamageAdd);
    lua_setfield(L, -2, "attackDamageAdd");
    lua_pushnumber(L, effects.attackDamageMul);
    lua_setfield(L, -2, "attackDamageMul");
    lua_pushnumber(L, effects.attackRangeAdd);
    lua_setfield(L, -2, "attackRangeAdd");
    lua_pushnumber(L, effects.attackRangeMul);
    lua_setfield(L, -2, "attackRangeMul");
    lua_pushnumber(L, effects.attackSpeedAdd);
    lua_setfield(L, -2, "attackSpeedAdd");
    lua_pushnumber(L, effects.attackSpeedMul);
    lua_setfield(L, -2, "attackSpeedMul");
    lua_pushnumber(L, effects.projectileSpeedAdd);
    lua_setfield(L, -2, "projectileSpeedAdd");
    lua_pushnumber(L, effects.projectileSpeedMul);
    lua_setfield(L, -2, "projectileSpeedMul");
    lua_pushnumber(L, effects.splashRadiusAdd);
    lua_setfield(L, -2, "splashRadiusAdd");
    lua_pushnumber(L, effects.splashRadiusMul);
    lua_setfield(L, -2, "splashRadiusMul");
    lua_pushnumber(L, effects.chainRangeAdd);
    lua_setfield(L, -2, "chainRangeAdd");
    lua_pushnumber(L, effects.chainRangeMul);
    lua_setfield(L, -2, "chainRangeMul");
    lua_pushnumber(L, effects.ricochetRangeAdd);
    lua_setfield(L, -2, "ricochetRangeAdd");
    lua_pushnumber(L, effects.ricochetRangeMul);
    lua_setfield(L, -2, "ricochetRangeMul");
    lua_pushinteger(L, effects.projectileCountAdd);
    lua_setfield(L, -2, "projectileCountAdd");
    lua_pushinteger(L, effects.chainTargetCountAdd);
    lua_setfield(L, -2, "chainTargetCountAdd");
    lua_pushinteger(L, effects.ricochetCountAdd);
    lua_setfield(L, -2, "ricochetCountAdd");
}

void pushUpgradeLevelsTable(lua_State* L, const TowerArchetype::UpgradeNode& node) {
    lua_newtable(L);
    for (std::size_t levelIdx = 0; levelIdx < node.upgradeLevels.size(); ++levelIdx) {
        const TowerArchetype::UpgradeNode::UpgradeLevel& level = node.upgradeLevels[levelIdx];
        lua_newtable(L);
        lua_pushinteger(L, level.cost);
        lua_setfield(L, -2, "cost");
        pushUpgradeEffectsTable(L, level.effects);
        lua_setfield(L, -2, "effects");
        lua_seti(L, -2, static_cast<lua_Integer>(levelIdx + 1));
    }
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

int computeTowerTotalSpent(const TowerArchetype& archetype, const playlevel::PlacedTower& placedTower) {
    int totalSpent = std::max(0, archetype.cost);
    for (const TowerArchetype::UpgradeNode& node : archetype.upgradeNodes) {
        const int currentLevel = static_cast<int>(
            std::count(placedTower.unlockedUpgradeNodeIds.begin(), placedTower.unlockedUpgradeNodeIds.end(), node.id));
        const int maxLevel = static_cast<int>(node.upgradeLevels.size());
        for (int levelIndex = 0; levelIndex < currentLevel && levelIndex < maxLevel; ++levelIndex) {
            totalSpent += std::max(0, node.upgradeLevels[static_cast<std::size_t>(levelIndex)].cost);
        }
    }
    return totalSpent;
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

    bootstrap_.resetRuntimeState(gameplayState_, towerLoadController_, enemyLoadController_, towerPlacementController_,
                                 placedTowers_, activeProjectiles_, nextTowerRuntimeId_, nextEnemyRuntimeId_, activeEnemies_,
                                 waveController_, routeController_, selectedEnemyRuntimeId_, pickingController_, state,
                                 selectedMapAssetPath_, selectedLevelScriptPath_, selectedWavesScriptPath_,
                                 worldAssetSpec_);
    pendingCommands_.clear();
    localMatchHost_ = {};
    localMatchHost_.registerPlayer(kLocalHostPlayerId);
    matchSimulation_.reset();
    matchSimulation_.registerPlayer(kLocalHostPlayerId, gameplayState_.playerMoney);
    syncLocalPlayerMoney();
    nextLocalCommandSequence_ = 1;
    session_ = state.multiplayerSession;
    remotePlayerByPeer_.clear();
    isRemoteClient_ = false;
    remoteJoinPending_ = false;
    remoteJoinFailureReason_.clear();
    localPlayerId_ = kLocalHostPlayerId;
    loadedReadySignaled_ = false;
    placementRegions_.clear();
    towerPlacementPreviewResolver_.reset();
    lastPlacementValidationReason_.clear();
    enemyAnimationInitialized_ = false;
    bootstrap_.configureLevel(L_, state, selectedMapAssetPath_, selectedLevelScriptPath_, selectedWavesScriptPath_,
                              worldAssetSpec_, towerLoadController_, enemyLoadController_, waveController_);
    {
        std::vector<std::string> contentIds;
        contentIds.reserve(towerLoadController_.archetypes().size() + enemyLoadController_.archetypes().size());
        for (const auto& [id, archetype] : towerLoadController_.archetypes()) {
            (void)archetype;
            contentIds.push_back(id);
        }
        for (const auto& [id, archetype] : enemyLoadController_.archetypes()) {
            (void)archetype;
            contentIds.push_back(id);
        }
        gameplayContentSha256_ = multiplayer::computeContentDigest(std::move(contentIds));
    }

    // Reuse the persistent party session established in LobbyScene (see MultiplayerSession) for
    // match-level traffic: a client sends a JoinMatchRequest over the already-open connection,
    // and a host performs match-level join validation over its already-listening transport. A
    // session that is not currently in a party (solo play) skips networking entirely.
    if (session_ && session_->isInParty() && session_->isClient()) {
        startJoiningHost(std::string{}, 0, "Player");
    } else if (session_ && session_->isInParty() && session_->isHost()) {
        startHostingOnPort(0);
    }

    scriptRef_ = loadLuaScript(state, "assets/scenes/PlayLevel.lua");
    luaOnEnter(scriptRef_);

    worldRenderer_ = bootstrap_.beginWorldLoad(L_, state, selectedMapAssetPath_, worldAssetSpec_, loadStatus_);
}

void PlayLevelScene::onExit(SceneSharedState& state) {
    luaOnExit(state, scriptRef_);

    // Deliberately does not stop hosting or disconnect the client: the session (and its
    // connection) is persistent and outlives this scene -- see MultiplayerSession.hpp.
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

    advanceAuthoritativeSimulation(dt);
    PlayLevelFrameCoordinator::Context frameContext{worldRenderer_.get(),
                                                    placementRegions_,
                                                    pickingController_,
                                                    cameraController_,
                                                    activeEnemies_,
                                                    selectedEnemyRuntimeId_,
                                                    towerPlacementController_.hasActiveSelection(),
                                                    lastRenderExtent_};
    const bool isLoaded = frameCoordinator_.run(
        frameContext,
        {[this]() { updateRouteFromWorld(); }, [this]() { syncTowerInstanceTransforms(); },
         [this]() { syncPlacedTowerModels(); }, [this](float deltaTime) { updateCamera(deltaTime); },
         [this]() { updateTowerPlacementFromInput(); }, [this]() { return buildViewMatrix(); }},
        dt);

    luaOnRender(state, scriptRef_, dt);

    if (isLoaded) {
        drawTowerPlacementOverlay();
        drawPlacementBoundsOverlay();
        pickingController_.drawPickSpheresOverlay(worldRenderer_.get(), buildViewMatrix(), lastRenderExtent_);
    }
}

bool PlayLevelScene::requestSpendMoney(float amount) {
    return spendPlayerMoney(kLocalHostPlayerId, amount);
}

bool PlayLevelScene::spendPlayerMoney(multiplayer::PlayerId playerId, float amount) {
    if (amount <= 0 || gameplayState_.matchStatus != MatchStatus::Running || !matchSimulation_.debitPlayer(playerId, amount)) {
        return false;
    }
    syncLocalPlayerMoney();
    return true;
}

bool PlayLevelScene::creditPlayerMoney(multiplayer::PlayerId playerId, float amount) {
    if (!matchSimulation_.creditPlayer(playerId, amount)) {
        return false;
    }
    syncLocalPlayerMoney();
    return true;
}

void PlayLevelScene::syncLocalPlayerMoney() {
    gameplayState_.playerMoney = matchSimulation_.playerBalance(kLocalHostPlayerId);
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
            const TowerArchetype::UpgradeNode::UpgradeLevel* levelData = getUpgradeLevelData(node, lvl);
            const TowerArchetype::UpgradeEffects* effects = levelData ? &levelData->effects : nullptr;
            if (!effects) {
                continue;
            }

            attackDamage = (attackDamage + effects->attackDamageAdd) * effects->attackDamageMul;
            attackRange = (attackRange + effects->attackRangeAdd) * effects->attackRangeMul;
            attackSpeed = (attackSpeed + effects->attackSpeedAdd) * effects->attackSpeedMul;
            projectileSpeed = (projectileSpeed + effects->projectileSpeedAdd) * effects->projectileSpeedMul;
            splashRadius = (splashRadius + effects->splashRadiusAdd) * effects->splashRadiusMul;
            chainRange = (chainRange + effects->chainRangeAdd) * effects->chainRangeMul;
            ricochetRange = (ricochetRange + effects->ricochetRangeAdd) * effects->ricochetRangeMul;
            projectileCount += effects->projectileCountAdd;
            chainTargetCount += effects->chainTargetCountAdd;
            ricochetCount += effects->ricochetCountAdd;
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
                                                       const std::string& nodeId, multiplayer::PlayerId playerId) const {
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }

    const TowerArchetype::UpgradeNode* node = findUpgradeNodeById(archetype, nodeId);
    if (!node) {
        return "upgrade node does not exist";
    }

    auto levelForNode = [&placedTower](const std::string& id) {
        return static_cast<int>(
            std::count(placedTower.unlockedUpgradeNodeIds.begin(), placedTower.unlockedUpgradeNodeIds.end(), id));
    };
    auto hasUnlocked = [&levelForNode](const std::string& id) { return levelForNode(id) > 0; };

    const int currentLevel = levelForNode(node->id);
    const int maxLevel = getUpgradeMaxLevel(*node);
    if (currentLevel >= maxLevel) {
        return "upgrade is at max level";
    }

    const TowerArchetype::UpgradeNode::UpgradeLevel* nextLevel = getUpgradeLevelData(*node, currentLevel);
    if (!nextLevel) {
        return "upgrade level data is invalid";
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

    if (nextLevel->cost > 0 && matchSimulation_.playerBalance(playerId) < static_cast<float>(nextLevel->cost)) {
        return "insufficient funds";
    }

    return {};
}

bool PlayLevelScene::unlockTowerUpgrade(PlacedTower& placedTower, const std::string& nodeId,
                                        multiplayer::PlayerId playerId, std::string& outReason) {
    const TowerArchetype* archetype = towerLoadController_.findArchetype(placedTower.towerId);
    if (!archetype) {
        outReason = "tower archetype not found";
        return false;
    }

    const std::string reason = validateTowerUpgradeUnlock(*archetype, placedTower, nodeId, playerId);
    if (!reason.empty()) {
        outReason = reason;
        return false;
    }

    const TowerArchetype::UpgradeNode* node = findUpgradeNodeById(*archetype, nodeId);
    if (!node) {
        outReason = "upgrade node does not exist";
        return false;
    }

    const int currentLevel = static_cast<int>(
        std::count(placedTower.unlockedUpgradeNodeIds.begin(), placedTower.unlockedUpgradeNodeIds.end(), node->id));
    const TowerArchetype::UpgradeNode::UpgradeLevel* nextLevel = getUpgradeLevelData(*node, currentLevel);
    if (!nextLevel) {
        outReason = "upgrade level data is invalid";
        return false;
    }

    if (nextLevel->cost > 0 && !spendPlayerMoney(playerId, static_cast<float>(nextLevel->cost))) {
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
                             splashRadius, chainRange, ricochetRange, projectileCount, chainTargetCount, ricochetCount);

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

std::string PlayLevelScene::validateTowerPlacement(const TowerArchetype& archetype, const glm::vec3& worldPos,
                                                   float availableFunds, int footprintSampleCount) const {
    const TowerPlacementRules::Context placementContext{
        gameplayState_,        placedTowers_, worldRenderer_.get(), placementRegions_, maxTowerPlacementSlopeDegrees_,
        pathCorridorHalfWidth_};
    return TowerPlacementRules::validatePlacement(placementContext, archetype, worldPos, footprintSampleCount,
                                                  towerPlacementPreviewResolver_.lastTerrainSample(), availableFunds);
}

void PlayLevelScene::clearActiveSelectionForTowerPlacement(const char* reason) {
    selectedEnemyRuntimeId_ = 0;
    if (pickingController_.selectedSelection().valid || pickingController_.selectedInstanceIndex() >= 0) {
        pickingController_.clearSelection(reason);
    }
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
        towerPlacementPreviewResolver_.reset();
    }
    const TowerPlacementRules::Context placementContext{
        gameplayState_,        placedTowers_, worldRenderer_.get(), placementRegions_, maxTowerPlacementSlopeDegrees_,
        pathCorridorHalfWidth_};
    const auto validatePlacement = [this, selected, &placementContext](const glm::vec3& worldPos,
                                                                       int footprintSampleCount,
                                                                       const PlacementTerrainSample& terrainSample) {
        if (!selected) {
            return std::string("no tower selected");
        }
        return TowerPlacementRules::validatePlacement(placementContext, *selected, worldPos, footprintSampleCount,
                                                      terrainSample, gameplayState_.playerMoney);
    };
    towerPlacementController_.updatePlacementFromInput(
        selected != nullptr,
        [this, selected, &placementContext, &validatePlacement](glm::vec3& outHit) {
            const auto result = towerPlacementPreviewResolver_.resolve(
                selected,
                [this, &placementContext]() {
                    const ImGuiIO& io = ImGui::GetIO();
                    return TowerPlacementRules::sampleTerrainAtScreenPoint(
                        placementContext, buildViewMatrix(), cameraController_.position(), io.MousePos.x,
                        io.MousePos.y, io.DisplaySize.x, io.DisplaySize.y);
                },
                validatePlacement);
            if (!result.hasHit) {
                lastPlacementValidationReason_ = result.reason;
                return false;
            }
            outHit = result.worldPos;
            lastPlacementValidationReason_ = result.reason;
            return true;
        },
        [this, selected, &validatePlacement](const glm::vec3& worldPos) {
            return towerPlacementPreviewResolver_.canPlaceAt(selected, worldPos, validatePlacement,
                                                             lastPlacementValidationReason_);
        },
        [this, selected](const glm::vec3& worldPos) {
            if (!selected) {
                return;
            }

            // Re-validate with full footprint precision before committing the placement.
            constexpr int kConfirmFootprintSampleCount = 8;
            const std::string finalReason =
                validateTowerPlacement(*selected, worldPos, gameplayState_.playerMoney, kConfirmFootprintSampleCount);
            if (!finalReason.empty()) {
                lastPlacementValidationReason_ = finalReason;
                towerPlacementPreviewResolver_.cacheValidationResult(selected->id, worldPos, false, finalReason);
                return;
            }

            if (processLocalTowerPlacementCommand(*selected, worldPos)) {
                towerPlacementController_.cancelPlacement();
                towerPlacementPreviewResolver_.reset();
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
        lastPlacementValidationReason_ = validateTowerPlacement(*selected, placementState.worldPos,
                                                                gameplayState_.playerMoney, kPreviewFootprintSampleCount);
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

    // Each enemy independently overrides its own clip/time -- Walking while Alive, Death while
    // Dying -- against ITS OWN template's animator, selected via templatePrototypeIndex (resolved
    // once at spawn time; see findEnemyPrototypeIndex / ActiveEnemy::templatePrototypeIndex). This
    // is what lets two different simultaneously-alive archetypes each show their own clip on their
    // own skeleton, rather than one shared "first alive enemy wins" clip for the whole level -- see
    // AnimatedEntityInstanceSet::Instance::animationClipIndexOverride. Looked up per-enemy (by its
    // own archetype's clip names) rather than once for the whole list, since different archetypes
    // can name their clips differently.
    std::vector<AnimatedEntityInstanceSet::Instance> instances;
    instances.reserve(activeEnemies_.size());

    for (const ActiveEnemy& enemy : activeEnemies_) {
        const glm::vec3 pos = sampleRoutePosition(enemy.distanceAlongPath);
        const float yaw =
            sampleRouteYaw(enemy.distanceAlongPath) + (enemy.facingYawOffsetDegrees * 0.01745329251994329577f);
        const glm::mat4 model = glm::translate(glm::mat4{1.0f}, pos) *
                                glm::rotate(glm::mat4{1.0f}, yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::scale(glm::mat4{1.0f}, glm::vec3(enemy.renderScale));

        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = model;
        instance.prototypeIndex = enemy.templatePrototypeIndex;
        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Dying) {
            const int deathClipIndex =
                worldRenderer_->enemyAnimationClipIndexByName(enemy.deathClipName, enemy.templatePrototypeIndex);
            if (deathClipIndex >= 0) {
                instance.animationClipIndexOverride = deathClipIndex;
                instance.animationClipTimeSecondsOverride = enemy.deathElapsedSeconds;
            }
        } else if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive) {
            const int walkClipIndex =
                worldRenderer_->enemyAnimationClipIndexByName(enemy.walkingClipName, enemy.templatePrototypeIndex);
            if (walkClipIndex >= 0) {
                instance.animationClipIndexOverride = walkClipIndex;
                instance.animationClipTimeSecondsOverride = enemy.walkAnimElapsedSeconds;
            }
        }
        instances.push_back(instance);
    }

    worldRenderer_->setAnimatedEntityInstances(std::move(instances));
}

int PlayLevelScene::countAliveEnemies() const {
    int count = 0;
    for (const ActiveEnemy& enemy : activeEnemies_) {
        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive) {
            ++count;
        }
    }
    return count;
}

void PlayLevelScene::updateEnemyAnimationState() {
    if (!worldRenderer_ || !worldRenderer_->hasEnemyAnimation()) {
        return;
    }

    // One-time initialization only: seeds every registered archetype's OWN template animator with
    // ITS OWN idle clip (data-driven -- see EnemyArchetype::idleClipName), so a differently-rigged
    // enemy doesn't silently fall back to the generic auto-pick heuristic, and so the idle pose
    // shown before any enemy of that archetype has spawned is correct even with multiple
    // simultaneously-registered archetypes/templates. kEnemyClipIdle is only the last-resort
    // default for when no archetype is registered yet.
    //
    // Walking/Death selection no longer lives here: syncTowerInstanceTransforms() now gives every
    // Alive/Dying enemy instance its own per-instance clip/time override against its own
    // template's animator (see AnimatedEntityInstanceSet::Instance::animationClipIndexOverride),
    // so two simultaneously-alive archetypes each correctly show their own Walking clip instead of
    // one shared "first alive enemy wins" clip for the whole level -- the old per-frame
    // first-alive-enemy hack this function used to contain is gone, and with it the one-shared-
    // clip limitation it documented.
    if (enemyAnimationInitialized_) {
        return;
    }
    enemyAnimationInitialized_ = true;

    if (enemyLoadController_.empty()) {
        worldRenderer_->setActiveEnemyAnimationClipByName(kEnemyClipIdle, 0);
        return;
    }

    for (const auto& [id, archetype] : enemyLoadController_.archetypes()) {
        const int prototypeIndex = findEnemyPrototypeIndex(worldAssetSpec_, enemyLoadController_, id);
        // No-op (returns false) if that template has no clip by this name -- leaves whatever
        // TemplateAnimator auto-selected on load in place, for older/simpler enemy models.
        worldRenderer_->setActiveEnemyAnimationClipByName(archetype.idleClipName, prototypeIndex);
    }
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

    // Selected/hovered attack-range and footprint indicators are real Vulkan ground-plane discs
    // (see WorldRenderer::GroundCircle) rather than a screen-space ImGui overlay, so terrain hills
    // and closer enemies/towers correctly occlude them instead of them always painting on top.
    std::vector<GroundCircle> groundCircles;
    constexpr float kGroundCircleYOffset = 0.22f;
    constexpr glm::vec4 kSelectedColor(0.294f, 0.686f, 1.0f, 0.45f);
    constexpr glm::vec4 kSelectedOutlineColor(0.294f, 0.686f, 1.0f, 1.0f);
    // Another player's tower can't be upgraded/sold by us, so its selection ring uses a
    // distinct red tint instead of the normal "this is mine" blue.
    constexpr glm::vec4 kSelectedOtherPlayerColor(0.95f, 0.235f, 0.235f, 0.45f);
    constexpr glm::vec4 kSelectedOtherPlayerOutlineColor(0.95f, 0.235f, 0.235f, 1.0f);
    constexpr glm::vec4 kHoverColor(1.0f, 0.863f, 0.235f, 0.25f);
    // Hovering another player's tower gets the same red family as its selection ring, so the
    // ownership cue is instant even before clicking to select.
    constexpr glm::vec4 kHoverOtherPlayerColor(0.95f, 0.235f, 0.235f, 0.25f);

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
                const bool ownedByLocalPlayer = tower.ownerPlayerId == localPlayerId_;
                groundCircles.push_back({tower.position + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                        std::max(0.5f, tower.attackRange),
                                        ownedByLocalPlayer ? kSelectedColor : kSelectedOtherPlayerColor,
                                        ownedByLocalPlayer ? kSelectedOutlineColor : kSelectedOtherPlayerOutlineColor});
                break;
            }
            ++perTypeIndex;
        }
    }

    // Add selection circle for selected enemy.
    if (pickingController_.selectedSelection().valid && 
        pickingController_.selectedEntityKind() == WorldEntityKind::Enemy) {
        const int selectedEnemyIdx = pickingController_.selectedInstanceIndex();
        if (selectedEnemyIdx >= 0 && static_cast<std::size_t>(selectedEnemyIdx) < activeEnemies_.size()) {
            const ActiveEnemy& enemy = activeEnemies_[static_cast<std::size_t>(selectedEnemyIdx)];
            const glm::vec3 enemyPos = sampleRoutePosition(enemy.distanceAlongPath);
            groundCircles.push_back({enemyPos + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                    std::max(0.35f, 0.5f * enemy.renderScale), kSelectedColor, kSelectedOutlineColor});
        }
    }

    // Hover feedback replaces the old whole-model yellow tint: towers show their attack-range
    // circle (yellow, since blue is reserved for the selected tower's circle above) and enemies
    // show a simple ground circle -- both only when the hovered entity isn't already selected.
    const auto& hover = pickingController_.hoverSelection();
    const bool hoverEqualsSelected = hover.entityKind == pickingController_.selectedSelection().entityKind &&
                                     hover.instanceIndex == pickingController_.selectedSelection().instanceIndex &&
                                     pickingController_.selectedSelection().valid;
    if (hover.valid && !hoverEqualsSelected) {
        if (hover.entityKind == WorldEntityKind::Tower) {
            std::string hoverTowerId;
            int hoverTowerPoolIndex = -1;
            parseTowerPoolGroup(hover.group, hoverTowerId, hoverTowerPoolIndex);
            if (!hoverTowerId.empty() && hoverTowerPoolIndex >= 0) {
                int perTypeIndex = 0;
                for (const PlacedTower& tower : placedTowers_) {
                    if (tower.towerId != hoverTowerId) {
                        continue;
                    }
                    if (perTypeIndex == hoverTowerPoolIndex) {
                        const bool ownedByLocalPlayer = tower.ownerPlayerId == localPlayerId_;
                        groundCircles.push_back({tower.position + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                                std::max(0.5f, tower.attackRange),
                                                ownedByLocalPlayer ? kHoverColor : kHoverOtherPlayerColor});
                        break;
                    }
                    ++perTypeIndex;
                }
            }
        } else if (hover.entityKind == WorldEntityKind::Enemy && hover.instanceIndex >= 0 &&
                   static_cast<std::size_t>(hover.instanceIndex) < activeEnemies_.size()) {
            const ActiveEnemy& enemy = activeEnemies_[static_cast<std::size_t>(hover.instanceIndex)];
            const glm::vec3 enemyPos = sampleRoutePosition(enemy.distanceAlongPath);
            groundCircles.push_back({enemyPos + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                    std::max(0.35f, 0.5f * enemy.renderScale), kHoverColor});
        }
    }

    if (worldRenderer_) {
        worldRenderer_->setGroundCircles(std::move(groundCircles));
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
            valid[i] =
                projectWorldToScreen(corners[i], view, proj, displaySize, renderSize, screen[i], depthAbs, nullptr);
        }
        constexpr int kEdges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // bottom face
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // top face
            {0, 4}, {1, 5}, {2, 6}, {3, 7}, // verticals
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
        TowerPlacementRules::buildRouteSegmentCorridorBoxCorners(route[i], route[i + 1], pathCorridorHalfWidth_,
                                                                 corners);
        drawBoxWireframe(corners, pathColor);
    }

    // Manually-authored water/cliff placement regions.
    for (const TowerPlacementRegion& region : placementRegions_) {
        const glm::vec3& mn = region.boundsMin;
        const glm::vec3& mx = region.boundsMax;
        const std::array<glm::vec3, 8> corners = {
            glm::vec3(mn.x, mn.y, mn.z), glm::vec3(mx.x, mn.y, mn.z), glm::vec3(mx.x, mn.y, mx.z),
            glm::vec3(mn.x, mn.y, mx.z), glm::vec3(mn.x, mx.y, mn.z), glm::vec3(mx.x, mx.y, mn.z),
            glm::vec3(mx.x, mx.y, mx.z), glm::vec3(mn.x, mx.y, mx.z),
        };
        const ImU32 regionColor = (region.type == TowerPlacementRegionType::Water) ? IM_COL32(80, 160, 255, 200)
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
            processLocalStartWaveCommand();
            break;
        }
    }
    pendingCommands_.clear();
}

void PlayLevelScene::advanceAuthoritativeSimulation(float elapsedSeconds) {
    // The persistent session's io_context is polled once centrally per frame by the app runtime
    // (SceneDirector::render(), before any scene renders), not here -- see MultiplayerSession.

    if (!loadedReadySignaled_ && session_ && worldRenderer_ && worldRenderer_->isLoaded()) {
        session_->signalLocalLoadedReady();
        loadedReadySignaled_ = true;
    }

    // Loaded-ready barrier: nobody ticks match simulation or applies snapshots until every party
    // member (including this one) has finished loading the announced level. Connection/join
    // housekeeping still runs so peers can connect and be validated while others finish loading.
    if (session_ && !session_->allMembersLoadedReady()) {
        if (isRemoteClient_) {
            processRemoteJoinResult();
        } else {
            processIncomingJoinRequests();
        }
        return;
    }

    if (isRemoteClient_) {
        // A client never ticks matchSimulation_ itself -- only the real host advances
        // waves/combat. This instance purely reflects whatever snapshot the host last published.
        processRemoteJoinResult();
        if (const auto snapshotPayload = session_->client().consumeLatestSnapshot()) {
            if (const auto snapshot = multiplayer::MatchSnapshotBuilder::deserialize(*snapshotPayload)) {
                applyRemoteSnapshot(*snapshot);
            } else {
                spdlog::error("PlayLevelScene[client]: failed to decode incoming snapshot ({} bytes).",
                              snapshotPayload->size());
            }
        }
        advanceClientCosmeticAnimations(elapsedSeconds);
        session_->client().drainCommandResults();
        return;
    }

    processIncomingJoinRequests();
    matchSimulation_.advance(elapsedSeconds, [this](multiplayer::SimulationTick tick, float tickSeconds) {
        applyPendingGameplayCommands();
        drainRemotePlayerCommands(tick);
        updateWaveSimulation(tickSeconds);
        publishRemoteSnapshotIfDue(tick);
    });
}

void PlayLevelScene::advanceClientCosmeticAnimations(float elapsedSeconds) {
    for (ActiveEnemy& enemy : activeEnemies_) {
        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive) {
            enemy.walkAnimElapsedSeconds += elapsedSeconds;
        } else if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Dying) {
            enemy.deathElapsedSeconds += elapsedSeconds;
        }
    }
}

// Single authority boundary: every player command, whether it came from the local host player
// (applied synchronously below) or a remote peer (drained off the session's host transport), is
// validated and applied here and nowhere else.
std::optional<multiplayer::CommandRejectionReason>
PlayLevelScene::dispatchAuthoritativeCommand(const multiplayer::PlayerCommandRequest& command) {
    return std::visit(
        [this, &command](const auto& payload) -> std::optional<multiplayer::CommandRejectionReason> {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, multiplayer::StartWaveCommand>) {
                if (!validateStartWaveRequest().empty() || !requestStartWave()) {
                    return multiplayer::CommandRejectionReason::WaveCannotStart;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<Payload, multiplayer::PlaceTowerCommand>) {
                if (payload.towerArchetypeId.empty()) {
                    return multiplayer::CommandRejectionReason::InvalidPayload;
                }
                if (gameplayState_.matchStatus != MatchStatus::Running) {
                    return multiplayer::CommandRejectionReason::MatchNotRunning;
                }

                const TowerArchetype* requestedArchetype = towerLoadController_.findArchetype(payload.towerArchetypeId);
                if (!requestedArchetype) {
                    return multiplayer::CommandRejectionReason::UnknownTowerArchetype;
                }

                const glm::vec3 requestedPosition{payload.requestedPosition.x, payload.requestedPosition.y,
                                                  payload.requestedPosition.z};
                constexpr int kConfirmFootprintSampleCount = 8;
                if (!validateTowerPlacement(*requestedArchetype, requestedPosition,
                                           matchSimulation_.playerBalance(command.playerId),
                                           kConfirmFootprintSampleCount)
                         .empty()) {
                    return multiplayer::CommandRejectionReason::InvalidPlacement;
                }
                if (!spendPlayerMoney(command.playerId, static_cast<float>(requestedArchetype->cost))) {
                    return multiplayer::CommandRejectionReason::InsufficientFunds;
                }

                const float attackIntervalSeconds = 1.0f / std::max(0.01f, requestedArchetype->attackSpeed);
                const int towerPrototypeIndex = towerLoadController_.templatePrototypeIndex(requestedArchetype->id);
                const int projectilePrototypeIndex =
                    towerLoadController_.projectileTemplatePrototypeIndex(requestedArchetype->id);
                placedTowers_.push_back(
                    PlacedTower{requestedArchetype->id, requestedPosition, towerPrototypeIndex, projectilePrototypeIndex,
                                requestedArchetype->attackDamage, requestedArchetype->armorPiercing,
                                requestedArchetype->attackRange, attackIntervalSeconds, 0.0f,
                                requestedArchetype->projectileSpeed, requestedArchetype->splashRadius,
                                requestedArchetype->chainRange, requestedArchetype->ricochetRange,
                                std::max(1, requestedArchetype->projectileCount),
                                std::max(1, requestedArchetype->chainTargetCount),
                                std::max(0, requestedArchetype->ricochetCount), requestedArchetype->cost,
                                requestedArchetype->damageType, requestedArchetype->defaultTargetingMode, 0.0f, {},
                                nextTowerRuntimeId_++, command.playerId});
                return std::nullopt;
            } else if constexpr (std::is_same_v<Payload, multiplayer::UpgradeTowerCommand>) {
                if (payload.upgradeNodeId.empty()) {
                    return multiplayer::CommandRejectionReason::InvalidPayload;
                }
                if (gameplayState_.matchStatus != MatchStatus::Running) {
                    return multiplayer::CommandRejectionReason::MatchNotRunning;
                }

                const auto towerIt = std::find_if(placedTowers_.begin(), placedTowers_.end(),
                                                  [towerRuntimeId = payload.towerRuntimeId](const PlacedTower& tower) {
                                                      return tower.runtimeId == towerRuntimeId;
                                                  });
                if (towerIt == placedTowers_.end()) {
                    return multiplayer::CommandRejectionReason::UnknownTower;
                }
                if (towerIt->ownerPlayerId != command.playerId) {
                    return multiplayer::CommandRejectionReason::TowerNotOwnedByPlayer;
                }

                std::string reason;
                if (!unlockTowerUpgrade(*towerIt, payload.upgradeNodeId, command.playerId, reason)) {
                    return reason == "insufficient funds" ? multiplayer::CommandRejectionReason::InsufficientFunds
                                                           : multiplayer::CommandRejectionReason::UpgradeUnavailable;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<Payload, multiplayer::SetTowerTargetingCommand>) {
                if (gameplayState_.matchStatus != MatchStatus::Running) {
                    return multiplayer::CommandRejectionReason::MatchNotRunning;
                }

                const auto towerIt = std::find_if(placedTowers_.begin(), placedTowers_.end(),
                                                  [towerRuntimeId = payload.towerRuntimeId](const PlacedTower& tower) {
                                                      return tower.runtimeId == towerRuntimeId;
                                                  });
                if (towerIt == placedTowers_.end()) {
                    return multiplayer::CommandRejectionReason::UnknownTower;
                }
                if (towerIt->ownerPlayerId != command.playerId) {
                    return multiplayer::CommandRejectionReason::TowerNotOwnedByPlayer;
                }

                towerIt->targetingMode = toGameplayTargetingMode(payload.targetingMode);
                return std::nullopt;
            } else {
                static_assert(std::is_same_v<Payload, multiplayer::SellTowerCommand>);
                if (gameplayState_.matchStatus != MatchStatus::Running) {
                    return multiplayer::CommandRejectionReason::MatchNotRunning;
                }

                const auto towerIt = std::find_if(placedTowers_.begin(), placedTowers_.end(),
                                                  [towerRuntimeId = payload.towerRuntimeId](const PlacedTower& tower) {
                                                      return tower.runtimeId == towerRuntimeId;
                                                  });
                if (towerIt == placedTowers_.end()) {
                    return multiplayer::CommandRejectionReason::UnknownTower;
                }
                if (towerIt->ownerPlayerId != command.playerId) {
                    return multiplayer::CommandRejectionReason::TowerNotOwnedByPlayer;
                }

                const TowerArchetype* archetype = towerLoadController_.findArchetype(towerIt->towerId);
                if (!archetype) {
                    return multiplayer::CommandRejectionReason::UnknownTowerArchetype;
                }

                const int totalSpent = computeTowerTotalSpent(*archetype, *towerIt);
                const int refund = std::max(0, static_cast<int>(std::floor(static_cast<float>(totalSpent) * 0.8f)));
                creditPlayerMoney(towerIt->ownerPlayerId, static_cast<float>(refund));

                const int removedTowerIndex = static_cast<int>(std::distance(placedTowers_.begin(), towerIt));
                placedTowers_.erase(towerIt);

                std::size_t projectileWriteIndex = 0;
                for (std::size_t projectileIndex = 0; projectileIndex < activeProjectiles_.size(); ++projectileIndex) {
                    ActiveProjectile projectile = activeProjectiles_[projectileIndex];
                    if (projectile.sourceTowerPoolIndex == removedTowerIndex) {
                        continue;
                    }
                    if (projectile.sourceTowerPoolIndex > removedTowerIndex) {
                        projectile.sourceTowerPoolIndex -= 1;
                    }
                    activeProjectiles_[projectileWriteIndex++] = std::move(projectile);
                }
                activeProjectiles_.resize(projectileWriteIndex);
                return std::nullopt;
            }
        },
        command.payload);
}

bool PlayLevelScene::submitLocalCommand(const multiplayer::PlayerCommandRequest& command) {
    const auto serializedCommand = multiplayer::MatchProtocolAdapter::serializePlayerCommand(command);
    if (!serializedCommand) {
        spdlog::error("PlayLevelScene: failed to serialize local command.");
        return false;
    }

    if (isRemoteClient_) {
        // The real host validates and applies this command; we only forward it and reflect the
        // outcome once a CommandResult/snapshot arrives. Returning true here only means "sent".
        return session_ && session_->client().sendCommand(*serializedCommand);
    }

    bool applied = false;
    const auto serializedResult = localMatchHost_.processCommand(
        *serializedCommand, matchSimulation_.currentTick(),
        [this, &applied](const multiplayer::PlayerCommandRequest& receivedCommand) {
            const auto rejection = dispatchAuthoritativeCommand(receivedCommand);
            applied = !rejection.has_value();
            return rejection;
        });
    if (!serializedResult) {
        spdlog::error("PlayLevelScene: failed to serialize local command result.");
    }
    return applied;
}

bool PlayLevelScene::startHostingOnPort(unsigned short /*port*/) {
    // The session's host transport is already listening (MultiplayerSession::hostParty() was
    // called back in LobbyScene) -- there is nothing left to (re)bind here.
    if (isRemoteClient_ || !session_ || !session_->isHost()) {
        return false;
    }
    return true;
}

void PlayLevelScene::stopHosting() {
    // Does not touch the session's (persistent, shared) listener socket -- only clears this
    // scene's own per-match peer bookkeeping.
    remotePlayerByPeer_.clear();
}

unsigned short PlayLevelScene::hostingPort() const {
    return session_ ? session_->hostTransport().listenPort() : 0;
}

bool PlayLevelScene::disconnectRemotePlayer(multiplayer::TransportPeerId peerId) {
    const auto playerIt = remotePlayerByPeer_.find(peerId);
    if (playerIt == remotePlayerByPeer_.end()) {
        return false;
    }
    const multiplayer::PlayerId playerId = playerIt->second;
    remotePlayerByPeer_.erase(playerIt);
    localMatchHost_.unregisterPlayer(playerId);
    matchSimulation_.unregisterPlayer(playerId);
    return session_ && session_->hostTransport().disconnectPeer(peerId);
}

void PlayLevelScene::processIncomingJoinRequests() {
    if (!session_) {
        return;
    }

    // Default balance for a joining peer until match rules define a shared/host-configured
    // starting economy for co-op.
    constexpr float kRemotePlayerInitialBalance = 250.0f;

    multiplayer::LanMatchTransport& transport = session_->hostTransport();
    for (multiplayer::PendingJoinRequest& request : transport.drainJoinRequests()) {
        const auto decodedRequest = multiplayer::MatchProtocolAdapter::decodeJoinMatchRequest(request.payload);
        const std::string displayName =
            decodedRequest.request ? decodedRequest.request->playerDisplayName : std::string("<unknown>");
        spdlog::info("PlayLevelScene[host]: join request from peer {} (display name '{}').", request.peerId,
                     displayName);

        const auto outcome = localMatchHost_.processJoinRequest(request.payload, gameplayContentSha256_,
                                                                matchSimulation_.currentTick());
        if (!outcome) {
            spdlog::error("PlayLevelScene[host]: failed to serialize join result for peer {}.", request.peerId);
            transport.disconnectPeer(request.peerId);
            continue;
        }

        if (!outcome->acceptedPlayerId) {
            const char* reasonText = "unknown";
            if (const auto decodedResult =
                    multiplayer::MatchProtocolAdapter::decodeJoinMatchResult(outcome->serializedResult)) {
                if (const auto* rejected = std::get_if<multiplayer::JoinMatchRejected>(&*decodedResult)) {
                    reasonText = joinRejectionReasonToString(rejected->reason);
                }
            }
            spdlog::warn("PlayLevelScene[host]: rejected join from peer {} ('{}'): {}.", request.peerId, displayName,
                         reasonText);
            transport.sendJoinResult(request.peerId, outcome->serializedResult);
            transport.disconnectPeer(request.peerId);
            continue;
        }

        const multiplayer::PlayerId assignedPlayerId = *outcome->acceptedPlayerId;
        if (!matchSimulation_.registerPlayer(assignedPlayerId, kRemotePlayerInitialBalance) ||
            !localMatchHost_.registerPlayer(assignedPlayerId) || !transport.markPeerJoined(request.peerId)) {
            spdlog::error("PlayLevelScene[host]: failed to register accepted peer {} as player {}.", request.peerId,
                         assignedPlayerId);
            transport.disconnectPeer(request.peerId);
            continue;
        }

        remotePlayerByPeer_.emplace(request.peerId, assignedPlayerId);
        spdlog::info("PlayLevelScene[host]: peer {} joined as player {} ('{}').", request.peerId, assignedPlayerId,
                     displayName);
        transport.sendJoinResult(request.peerId, outcome->serializedResult);
        if (const auto snapshot = multiplayer::MatchSnapshotBuilder::serialize(matchSimulation_)) {
            transport.publishSnapshot(*snapshot);
        }
    }
}

bool PlayLevelScene::startJoiningHost(const std::string& /*hostAddress*/, unsigned short /*port*/,
                                      const std::string& displayName) {
    // The session's client is already connected (MultiplayerSession::joinParty() was called back
    // in LobbyScene) -- only the match-level JoinMatchRequest handshake happens here, over that
    // existing connection. Do not disconnect on failure: a failed handshake attempt should not
    // tear down the shared party connection.
    isRemoteClient_ = false;
    remoteJoinPending_ = false;
    remoteJoinFailureReason_.clear();

    if (!session_ || !session_->client().isConnected()) {
        remoteJoinFailureReason_ = "not connected to a host";
        spdlog::error("PlayLevelScene[client]: {}.", remoteJoinFailureReason_);
        return false;
    }

    multiplayer::JoinMatchRequest request;
    request.protocolVersion = multiplayer::kMatchProtocolVersion;
    request.playerDisplayName = displayName;
    request.contentManifest.gameplayContentSha256 = gameplayContentSha256_;
    const auto serializedRequest = multiplayer::MatchProtocolAdapter::serializeJoinMatchRequest(request);
    if (!serializedRequest || !session_->client().sendJoinRequest(*serializedRequest)) {
        remoteJoinFailureReason_ = "failed to send join request";
        spdlog::error("PlayLevelScene[client]: {}.", remoteJoinFailureReason_);
        return false;
    }

    spdlog::info("PlayLevelScene[client]: sent join request over existing connection as '{}', awaiting host reply...",
                 displayName);
    isRemoteClient_ = true;
    remoteJoinPending_ = true;
    return true;
}

void PlayLevelScene::processRemoteJoinResult() {
    if (!remoteJoinPending_ || !session_) {
        return;
    }

    const auto payload = session_->client().consumeJoinResult();
    if (!payload) {
        if (!session_->client().isConnected()) {
            remoteJoinPending_ = false;
            isRemoteClient_ = false;
            remoteJoinFailureReason_ = "connection lost while waiting for join result";
            spdlog::error("PlayLevelScene[client]: {}.", remoteJoinFailureReason_);
        }
        return;
    }

    remoteJoinPending_ = false;
    const auto result = multiplayer::MatchProtocolAdapter::decodeJoinMatchResult(*payload);
    if (!result) {
        isRemoteClient_ = false;
        remoteJoinFailureReason_ = "malformed join result";
        spdlog::error("PlayLevelScene[client]: {}.", remoteJoinFailureReason_);
        return;
    }

    if (const auto* accepted = std::get_if<multiplayer::JoinMatchAccepted>(&*result)) {
        localPlayerId_ = accepted->playerId;
        spdlog::info("PlayLevelScene[client]: join accepted, assigned player id {}.", localPlayerId_);
        return;
    }

    const auto* rejected = std::get_if<multiplayer::JoinMatchRejected>(&*result);
    remoteJoinFailureReason_ =
        rejected ? std::string("join rejected by host: ") + joinRejectionReasonToString(rejected->reason)
                 : "join rejected by host";
    spdlog::error("PlayLevelScene[client]: {}.", remoteJoinFailureReason_);
    isRemoteClient_ = false;
    // Does not disconnect the shared party session over a match-level rejection (e.g. content
    // digest mismatch) -- the player stays in the party/chat, just not this particular match.
}

void PlayLevelScene::applyRemoteSnapshot(const multiplayer::DecodedMatchSnapshot& snapshot) {
    spdlog::info("PlayLevelScene[client]: applying snapshot tick={} status={} wave={} waveInProgress={} towers={} "
                 "enemies={}",
                 snapshot.simulationTick, static_cast<int>(snapshot.matchStatus), snapshot.currentWave,
                 snapshot.waveInProgress, snapshot.towers.size(), snapshot.enemies.size());
    gameplayState_.matchStatus = snapshot.matchStatus;
    gameplayState_.baseHealth = snapshot.baseHealth;
    gameplayState_.currentWave = snapshot.currentWave;
    gameplayState_.waveInProgress = snapshot.waveInProgress;
    gameplayState_.waveCountdownActive = snapshot.waveCountdownActive;
    gameplayState_.waveCountdownRemainingSeconds = snapshot.waveCountdownRemainingSeconds;
    gameplayState_.waveRoundRemainingSeconds = snapshot.waveRoundRemainingSeconds;
    gameplayState_.waveRoundDurationSeconds = snapshot.waveRoundDurationSeconds;

    for (const auto& player : snapshot.players) {
        if (player.playerId == localPlayerId_) {
            gameplayState_.playerMoney = player.money;
            break;
        }
    }

    // Towers: merge by runtime id so client-only fields on an already-known tower are preserved.
    // The wire snapshot only carries position/targeting/ownership/unlocked-upgrade-node-ids, so
    // combat/render-facing stats (attackRange etc., used by the selection ground circle) are
    // resolved locally from the tower's archetype + applyTowerUpgradeEffects on every merge,
    // mirroring what dispatchAuthoritativeCommand/unlockTowerUpgrade compute on the host. Towers
    // no longer present on the host (sold) are dropped.
    std::vector<PlacedTower> mergedTowers;
    mergedTowers.reserve(snapshot.towers.size());
    for (const auto& wireTower : snapshot.towers) {
        const auto existingIt = std::find_if(placedTowers_.begin(), placedTowers_.end(),
                                             [&wireTower](const PlacedTower& tower) {
                                                 return tower.runtimeId == wireTower.runtimeId;
                                             });
        const bool isNewTower = existingIt == placedTowers_.end();
        PlacedTower tower = isNewTower ? PlacedTower{} : *existingIt;
        if (isNewTower) {
            tower.towerId = wireTower.towerArchetypeId;
            tower.runtimeId = wireTower.runtimeId;
        }
        tower.position = {wireTower.positionX, wireTower.positionY, wireTower.positionZ};
        tower.targetingMode = toGameplayTargetingMode(wireTower.targetingMode);
        tower.unlockedUpgradeNodeIds = wireTower.unlockedUpgradeNodeIds;
        tower.ownerPlayerId = wireTower.ownerPlayerId;

        if (const TowerArchetype* archetype = towerLoadController_.findArchetype(tower.towerId)) {
            if (isNewTower) {
                tower.cost = archetype->cost;
                tower.damageType = archetype->damageType;
                tower.armorPiercing = archetype->armorPiercing;
            }
            float attackSpeed = 1.0f / std::max(0.01f, tower.attackIntervalSeconds);
            applyTowerUpgradeEffects(*archetype, tower, tower.attackDamage, tower.attackRange, attackSpeed,
                                     tower.projectileSpeed, tower.splashRadius, tower.chainRange,
                                     tower.ricochetRange, tower.projectileCount, tower.chainTargetCount,
                                     tower.ricochetCount);
            tower.attackIntervalSeconds = 1.0f / std::max(0.01f, attackSpeed);

            int activeTowerPrototype = towerLoadController_.templatePrototypeIndex(tower.towerId);
            int activeProjectilePrototype = towerLoadController_.projectileTemplatePrototypeIndex(tower.towerId);
            for (const std::string& unlockedId : tower.unlockedUpgradeNodeIds) {
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
            tower.towerPrototypeIndex = activeTowerPrototype;
            tower.projectilePrototypeIndex = activeProjectilePrototype;
        }
        mergedTowers.push_back(std::move(tower));
    }
    placedTowers_ = std::move(mergedTowers);

    // Enemies: merge by runtime id, preserving client-only animation timers
    // (walkAnimElapsedSeconds/deathElapsedSeconds) so playback doesn't pop every snapshot; newly-
    // seen enemies are constructed from archetype data, mirroring updateWaveSimulation()'s spawn
    // logic.
    std::vector<ActiveEnemy> mergedEnemies;
    mergedEnemies.reserve(snapshot.enemies.size());
    for (const auto& wireEnemy : snapshot.enemies) {
        const auto existingIt = std::find_if(activeEnemies_.begin(), activeEnemies_.end(),
                                             [&wireEnemy](const ActiveEnemy& enemy) {
                                                 return enemy.runtimeId == wireEnemy.runtimeId;
                                             });
        ActiveEnemy enemy = (existingIt != activeEnemies_.end()) ? *existingIt : ActiveEnemy{};
        const bool wasDying =
            existingIt != activeEnemies_.end() && existingIt->lifecycleState == playlevel::EnemyLifecycleState::Dying;
        if (existingIt == activeEnemies_.end()) {
            enemy.enemyId = wireEnemy.enemyArchetypeId;
            enemy.runtimeId = wireEnemy.runtimeId;
            if (const EnemyArchetype* archetype = enemyLoadController_.findArchetype(wireEnemy.enemyArchetypeId)) {
                enemy.renderScale = std::max(0.01f, archetype->renderScale);
                enemy.facingYawOffsetDegrees = archetype->facingYawOffsetDegrees;
                enemy.idleClipName = archetype->idleClipName;
                enemy.walkingClipName = archetype->walkingClipName;
                enemy.deathClipName = archetype->deathClipName;
            }
            enemy.templatePrototypeIndex =
                findEnemyPrototypeIndex(worldAssetSpec_, enemyLoadController_, enemy.enemyId);
        }
        enemy.distanceAlongPath = wireEnemy.distanceAlongPath;
        enemy.health = wireEnemy.health;
        enemy.maxHealth = std::max(enemy.maxHealth, enemy.health);
        enemy.shield = wireEnemy.shield;
        enemy.maxShield = std::max(enemy.maxShield, enemy.shield);
        enemy.lifecycleState = static_cast<playlevel::EnemyLifecycleState>(wireEnemy.lifecycleState);
        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Dying && !wasDying) {
            // Mirrors PlayLevelCombatController::collectDefeatedEnemies() resetting the clip to 0
            // at the exact Alive->Dying flip, so the Death clip doesn't start mid-playback.
            enemy.deathElapsedSeconds = 0.0f;
        }
        mergedEnemies.push_back(std::move(enemy));
    }
    activeEnemies_ = std::move(mergedEnemies);

    // Projectiles are short-lived and purely cosmetic client-side; positions snap directly from
    // the snapshot each tick (no local simulation). velocity is reconstructed here as a
    // direction-only hint toward the projectile's current target (falling back to the previous
    // snapshot's direction if the target can't be found), so syncPlacedTowerModels's
    // yaw-from-velocity math renders the correct facing instead of always defaulting to yaw=0.
    std::vector<ActiveProjectile> mergedProjectiles;
    mergedProjectiles.reserve(snapshot.projectiles.size());
    for (const auto& wireProjectile : snapshot.projectiles) {
        const auto existingIt = std::find_if(activeProjectiles_.begin(), activeProjectiles_.end(),
                                             [&wireProjectile](const ActiveProjectile& projectile) {
                                                 return projectile.runtimeId == wireProjectile.runtimeId;
                                             });
        ActiveProjectile projectile = (existingIt != activeProjectiles_.end()) ? *existingIt : ActiveProjectile{};
        projectile.runtimeId = wireProjectile.runtimeId;
        projectile.towerId = wireProjectile.towerArchetypeId;
        projectile.prototypeIndex = towerLoadController_.projectileTemplatePrototypeIndex(wireProjectile.towerArchetypeId);
        projectile.position = {wireProjectile.positionX, wireProjectile.positionY, wireProjectile.positionZ};
        projectile.targetEnemyRuntimeId = wireProjectile.targetEnemyRuntimeId;

        const auto targetIt = std::find_if(activeEnemies_.begin(), activeEnemies_.end(),
                                           [&projectile](const ActiveEnemy& enemy) {
                                               return enemy.runtimeId == projectile.targetEnemyRuntimeId;
                                           });
        if (targetIt != activeEnemies_.end()) {
            const glm::vec3 direction = sampleRoutePosition(targetIt->distanceAlongPath) - projectile.position;
            if (glm::dot(direction, direction) > 1e-6f) {
                projectile.velocity = direction;
            }
        }
        mergedProjectiles.push_back(std::move(projectile));
    }
    activeProjectiles_ = std::move(mergedProjectiles);

    reconcileSelectedEnemyAfterSimulation();
}

void PlayLevelScene::drainRemotePlayerCommands(multiplayer::SimulationTick currentTick) {
    if (!session_) {
        return;
    }
    for (multiplayer::ReceivedClientCommand& received : session_->hostTransport().drainClientCommands()) {
        const auto playerIt = remotePlayerByPeer_.find(received.peerId);
        if (playerIt == remotePlayerByPeer_.end()) {
            continue;
        }

        const multiplayer::PlayerId expectedPlayerId = playerIt->second;
        const auto serializedResult = localMatchHost_.processCommand(
            received.payload, currentTick,
            [this, expectedPlayerId](const multiplayer::PlayerCommandRequest& receivedCommand)
                -> std::optional<multiplayer::CommandRejectionReason> {
                if (receivedCommand.playerId != expectedPlayerId) {
                    return multiplayer::CommandRejectionReason::UnknownPlayer;
                }
                return dispatchAuthoritativeCommand(receivedCommand);
            });
        if (serializedResult) {
            session_->hostTransport().sendCommandResult(received.peerId, *serializedResult);
        }
    }
}

void PlayLevelScene::publishRemoteSnapshotIfDue(multiplayer::SimulationTick currentTick) {
    if (!session_ || remotePlayerByPeer_.empty() || currentTick % kSnapshotIntervalTicks != 0) {
        return;
    }
    if (const auto snapshot = multiplayer::MatchSnapshotBuilder::serialize(matchSimulation_)) {
        spdlog::trace("PlayLevelScene[host]: publishing snapshot tick={} status={} wave={} waveInProgress={} peers={}",
                     currentTick, static_cast<int>(gameplayState_.matchStatus), gameplayState_.currentWave,
                     gameplayState_.waveInProgress, remotePlayerByPeer_.size());
        session_->hostTransport().publishSnapshot(*snapshot);
    }
}

void PlayLevelScene::processLocalStartWaveCommand() {
    multiplayer::PlayerCommandRequest command;
    command.playerId = localPlayerId_;
    command.sequence = nextLocalCommandSequence_++;
    command.payload = multiplayer::StartWaveCommand{};
    submitLocalCommand(command);
}

bool PlayLevelScene::processLocalTowerPlacementCommand(const TowerArchetype& archetype, const glm::vec3& worldPos) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = localPlayerId_;
    command.sequence = nextLocalCommandSequence_++;
    command.payload = multiplayer::PlaceTowerCommand{archetype.id, {worldPos.x, worldPos.y, worldPos.z}};
    return submitLocalCommand(command);
}

bool PlayLevelScene::processLocalTowerUpgradeCommand(multiplayer::TowerRuntimeId towerRuntimeId,
                                                      const std::string& nodeId) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = localPlayerId_;
    command.sequence = nextLocalCommandSequence_++;
    command.payload = multiplayer::UpgradeTowerCommand{towerRuntimeId, nodeId};
    return submitLocalCommand(command);
}

bool PlayLevelScene::processLocalTowerTargetingCommand(multiplayer::TowerRuntimeId towerRuntimeId,
                                                        playlevel::TowerTargetingMode targetingMode) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = localPlayerId_;
    command.sequence = nextLocalCommandSequence_++;
    command.payload = multiplayer::SetTowerTargetingCommand{towerRuntimeId, toMultiplayerTargetingMode(targetingMode)};
    return submitLocalCommand(command);
}

bool PlayLevelScene::processLocalTowerSellCommand(multiplayer::TowerRuntimeId towerRuntimeId) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = localPlayerId_;
    command.sequence = nextLocalCommandSequence_++;
    command.payload = multiplayer::SellTowerCommand{towerRuntimeId};
    return submitLocalCommand(command);
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
    // One-time idle-clip seeding for every registered archetype's template (see
    // updateEnemyAnimationState) -- a no-op on every call after the first.
    updateEnemyAnimationState();

    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }

    waveController_.updateWaveSpawning(gameplayState_, dt, countAliveEnemies(), [this](const std::string& enemyId) {
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
        ActiveEnemy enemy{enemyId,
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
                          facingYawOffsetDegrees};
        // Data-driven clip names (see EnemyArchetype::idleClipName/walkingClipName/deathClipName)
        // -- defaults match the Idle/Walking/Death convention when an archetype doesn't specify.
        if (archetype) {
            enemy.idleClipName = archetype->idleClipName;
            enemy.walkingClipName = archetype->walkingClipName;
            enemy.deathClipName = archetype->deathClipName;
        }
        // Resolved once at spawn time rather than re-derived every frame: which animated
        // template (and therefore which independent TemplateAnimator/skeleton) this enemy's
        // own model maps to. See ActiveEnemy::templatePrototypeIndex.
        enemy.templatePrototypeIndex = findEnemyPrototypeIndex(worldAssetSpec_, enemyLoadController_, enemyId);
        activeEnemies_.push_back(std::move(enemy));
    });

    combatController_.advanceEnemies(dt, routeController_.totalLength(), activeEnemies_,
                                     [this](float baseDamage) { requestDamageBase(baseDamage); });
    reconcileSelectedEnemyAfterSimulation();

    combatController_.updateTowerAttacks(
        dt, [this](float distanceAlongPath) { return sampleRoutePosition(distanceAlongPath); }, placedTowers_,
        activeEnemies_, activeProjectiles_, nextProjectileRuntimeId_);

    combatController_.updateProjectiles(
        dt, [this](float distanceAlongPath) { return sampleRoutePosition(distanceAlongPath); }, placedTowers_,
        activeEnemies_, activeProjectiles_);

    combatController_.collectDefeatedEnemies(
        activeEnemies_, [this](float rewardMoney, multiplayer::TowerRuntimeId towerRuntimeId) {
            const auto towerIt = std::find_if(placedTowers_.begin(), placedTowers_.end(),
                                              [towerRuntimeId](const PlacedTower& tower) {
                                                  return tower.runtimeId == towerRuntimeId;
                                              });
            const multiplayer::PlayerId rewardPlayerId =
                towerIt != placedTowers_.end() ? towerIt->ownerPlayerId : kLocalHostPlayerId;
            creditPlayerMoney(rewardPlayerId, rewardMoney);
        },
        [this]() { gameplayState_.enemiesDefeated += 1; });
    reconcileSelectedEnemyAfterSimulation();

    // Enemies that just transitioned to Dying (health hit 0) keep playing their own Death clip
    // (by name, per their archetype -- see ActiveEnemy::deathClipName) in place until it finishes,
    // then get removed. Falls back to 0-duration (instant removal, same as the old behavior) for a
    // model with no clip by that name.
    combatController_.advanceDyingEnemies(
        dt,
        [this](const playlevel::ActiveEnemy& enemy) {
            if (!worldRenderer_) {
                return 0.0f;
            }
            const int clipIndex =
                worldRenderer_->enemyAnimationClipIndexByName(enemy.deathClipName, enemy.templatePrototypeIndex);
            return worldRenderer_->enemyAnimationClipDurationSeconds(clipIndex, enemy.templatePrototypeIndex);
        },
        activeEnemies_);
    reconcileSelectedEnemyAfterSimulation();

    gameplayState_.enemiesAlive = countAliveEnemies();

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
        // Use the Alive-only count here too: a Dying enemy still finishing its Death clip must not
        // delay Victory -- it's kept in activeEnemies_ purely to render
        // out its death animation, not because it's still meaningfully "in the fight".
        const bool noEnemiesAlive = countAliveEnemies() == 0;
        const int waveCount = static_cast<int>(waveController_.waveCount());
        if (gameplayState_.currentWave > waveCount) {
            if (noEnemiesAlive) {
                gameplayState_.matchStatus = MatchStatus::Victory;
            }
            return;
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
                const TowerArchetype::UpgradeNode::UpgradeLevel* firstLevel = getUpgradeLevelData(node, 0);
                const TowerArchetype::UpgradeEffects previewEffects =
                    firstLevel ? firstLevel->effects : TowerArchetype::UpgradeEffects{};
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
                lua_pushinteger(L, firstLevel ? firstLevel->cost : 0);
                lua_setfield(L, -2, "cost");
                lua_pushinteger(L, node.tier);
                lua_setfield(L, -2, "tier");
                lua_pushinteger(L, node.column);
                lua_setfield(L, -2, "column");
                lua_pushinteger(L, getUpgradeMaxLevel(node));
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

                pushUpgradeEffectsTable(L, previewEffects);
                lua_setfield(L, -2, "effects");

                pushUpgradeLevelsTable(L, node);
                lua_setfield(L, -2, "upgradeLevels");

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
            const bool hasTowerSelection = self->pickingController_.selectedSelection().valid &&
                                           parseTowerPoolGroup(self->pickingController_.selectedSelection().group,
                                                               selectedTowerId, selectedPoolIndex);

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
            lua_pushstring(L, archetype->bio.c_str());
            lua_setfield(L, -2, "bio");
            lua_pushinteger(L, static_cast<lua_Integer>(placedTower->ownerPlayerId));
            lua_setfield(L, -2, "ownerPlayerId");
            lua_pushboolean(L, placedTower->ownerPlayerId == self->localPlayerId_ ? 1 : 0);
            lua_setfield(L, -2, "isOwnedByLocalPlayer");
            lua_pushinteger(L, archetype->cost);
            lua_setfield(L, -2, "baseCost");

            const int totalSpent = computeTowerTotalSpent(*archetype, *placedTower);
            lua_pushinteger(L, totalSpent);
            lua_setfield(L, -2, "totalSpent");

            lua_pushnumber(L, std::max(0.0f, placedTower->totalDamageDealt));
            lua_setfield(L, -2, "totalDamageDealt");

            lua_pushstring(L, playlevel::towerTargetingModeToString(placedTower->targetingMode));
            lua_setfield(L, -2, "targetingMode");

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

                const int currentLevel = static_cast<int>(std::count(
                    placedTower->unlockedUpgradeNodeIds.begin(), placedTower->unlockedUpgradeNodeIds.end(), node.id));
                const bool unlocked = currentLevel > 0;
                const int maxLevel = getUpgradeMaxLevel(node);
                const bool canLevelUp = currentLevel < maxLevel;
                const TowerArchetype::UpgradeNode::UpgradeLevel* nextLevel =
                    canLevelUp ? getUpgradeLevelData(node, currentLevel) : nullptr;
                const std::string reason = canLevelUp
                                               ? self->validateTowerUpgradeUnlock(*archetype, *placedTower, node.id,
                                                                                   kLocalHostPlayerId)
                                               : std::string("upgrade is at max level");
                const bool canUnlock = canLevelUp && reason.empty();
                const TowerArchetype::UpgradeEffects previewEffects =
                    nextLevel ? nextLevel->effects : TowerArchetype::UpgradeEffects{};

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
                lua_pushinteger(L, nextLevel ? nextLevel->cost : 0);
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

                pushUpgradeEffectsTable(L, previewEffects);
                lua_setfield(L, -2, "effects");

                pushUpgradeLevelsTable(L, node);
                lua_setfield(L, -2, "upgradeLevels");

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
            const bool hasTowerSelection = self->pickingController_.selectedSelection().valid &&
                                           parseTowerPoolGroup(self->pickingController_.selectedSelection().group,
                                                               selectedTowerId, selectedPoolIndex);
            if (!hasTowerSelection) {
                return pushCommandResult(L, false, "select a placed tower");
            }

            PlacedTower* placedTower = self->findPlacedTowerByPoolKey(selectedTowerId, selectedPoolIndex);
            if (!placedTower) {
                return pushCommandResult(L, false, "tower state not found");
            }

            const bool accepted = self->processLocalTowerUpgradeCommand(placedTower->runtimeId, nodeId);
            return pushCommandResult(L, accepted, accepted ? "unlocked" : "host rejected upgrade");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestSelectedTowerUpgrade");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const std::string modeRaw = luaL_checkstring(L, 1);
            playlevel::TowerTargetingMode parsedMode{};
            if (!playlevel::tryParseTowerTargetingMode(modeRaw, parsedMode)) {
                return pushCommandResult(L, false, "invalid targeting mode");
            }

            std::string selectedTowerId;
            int selectedPoolIndex = -1;
            const bool hasTowerSelection = self->pickingController_.selectedSelection().valid &&
                                           parseTowerPoolGroup(self->pickingController_.selectedSelection().group,
                                                               selectedTowerId, selectedPoolIndex);
            if (!hasTowerSelection) {
                return pushCommandResult(L, false, "select a placed tower");
            }

            PlacedTower* placedTower = self->findPlacedTowerByPoolKey(selectedTowerId, selectedPoolIndex);
            if (!placedTower) {
                return pushCommandResult(L, false, "tower state not found");
            }

            const bool accepted = self->processLocalTowerTargetingCommand(placedTower->runtimeId, parsedMode);
            return pushCommandResult(L, accepted, accepted ? "targeting mode updated" : "host rejected targeting mode");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestSelectedTowerTargetingMode");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);

            std::string selectedTowerId;
            int selectedPoolIndex = -1;
            const bool hasTowerSelection = self->pickingController_.selectedSelection().valid &&
                                           parseTowerPoolGroup(self->pickingController_.selectedSelection().group,
                                                               selectedTowerId, selectedPoolIndex);
            if (!hasTowerSelection) {
                return pushCommandResult(L, false, "select a placed tower");
            }

            PlacedTower* placedTower = self->findPlacedTowerByPoolKey(selectedTowerId, selectedPoolIndex);
            if (!placedTower) {
                return pushCommandResult(L, false, "tower state not found");
            }

            const bool accepted = self->processLocalTowerSellCommand(placedTower->runtimeId);
            if (accepted) {
                self->pickingController_.clearSelection("selection cleared");
            }
            return pushCommandResult(L, accepted, accepted ? "tower sold" : "host rejected sell");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestSellSelectedTower");

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
            lua_pushboolean(L, self->towerPlacementPreviewResolver_.bungeeInvalidActive());
            lua_setfield(L, -2, "bungeeActive");
            lua_pushboolean(L, self->towerPlacementPreviewResolver_.hasValidPlacementAnchor());
            lua_setfield(L, -2, "hasAnchor");

            lua_newtable(L);
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.bungeeAnchorPos().x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.bungeeAnchorPos().y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.bungeeAnchorPos().z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "anchorPos");

            lua_newtable(L);
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.bungeeResolvedPos().x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.bungeeResolvedPos().y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.bungeeResolvedPos().z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "resolvedPos");

            lua_pushboolean(L, self->towerPlacementPreviewResolver_.hasLastRawPlacementCandidate());
            lua_setfield(L, -2, "hasRawCandidate");
            lua_pushboolean(L, self->towerPlacementPreviewResolver_.lastRawPlacementCandidateValid());
            lua_setfield(L, -2, "rawCandidateValid");
            lua_newtable(L);
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.lastRawPlacementCandidatePos().x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.lastRawPlacementCandidatePos().y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, self->towerPlacementPreviewResolver_.lastRawPlacementCandidatePos().z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "rawCandidatePos");
            lua_pushnumber(L, glm::distance(self->towerPlacementPreviewResolver_.bungeeAnchorPos(),
                                            self->towerPlacementPreviewResolver_.lastRawPlacementCandidatePos()));
            lua_setfield(L, -2, "anchorToCandidateDistance");
            lua_pushnumber(L, glm::distance(self->towerPlacementPreviewResolver_.bungeeResolvedPos(),
                                            self->towerPlacementPreviewResolver_.lastRawPlacementCandidatePos()));
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
                        reason = self->validateTowerPlacement(*tower, placementState.worldPos,
                                                              self->gameplayState_.playerMoney);
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
