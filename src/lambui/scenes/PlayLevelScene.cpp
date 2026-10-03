#include "lambui/scenes/PlayLevelScene.hpp"

#include "AudioEngine.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"
#include "lambui_backend/VulkanUiRenderer.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "scenes/EnemyLoadController.hpp"
#include "scenes/EnemySpawnFactory.hpp"
#include "scenes/TowerLoadController.hpp"
#include "utility/WorldRenderer.hpp"

#include <LambUI/UIManager.h>

#include <LambUI/UIInputBox.h>

#include <lua.hpp>

#include <stdexcept>
#include <type_traits>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <glm/vec4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <sstream>
#include <string_view>


namespace NodeSpireUi {

namespace {

constexpr float kTowerGhostAlpha = 0.45f;
constexpr float kTowerSellRefundRatio = 0.80f;
constexpr multiplayer::SimulationTick kSnapshotIntervalTicks = 3;
constexpr glm::vec4 kPlacementRangeFill{0.18f, 0.72f, 0.48f, 0.16f};
constexpr glm::vec4 kPlacementRangeOutline{0.35f, 1.0f, 0.65f, 0.85f};
constexpr glm::vec4 kInvalidPlacementRangeFill{0.82f, 0.16f, 0.14f, 0.18f};
constexpr glm::vec4 kInvalidPlacementRangeOutline{1.0f, 0.28f, 0.24f, 0.92f};
constexpr glm::vec4 kOtherPlayerRangeFill{0.82f, 0.65f, 0.20f, 0.13f};
constexpr glm::vec4 kOtherPlayerRangeOutline{1.0f, 0.82f, 0.28f, 0.9f};
constexpr glm::vec4 kHoverRangeFill{1.0f, 0.78f, 0.18f, 0.22f};
constexpr glm::vec4 kHoverOtherPlayerRangeFill{0.95f, 0.235f, 0.235f, 0.22f};
constexpr float kGroundCircleYOffset = 0.22f;
constexpr float kSettingsPollIntervalSeconds = 0.5f;
constexpr float kGameplayTuningPollIntervalSeconds = 0.5f;

std::optional<std::filesystem::file_time_type> tryGetLastWriteTime(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return std::nullopt;
    }
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    return writeTime;
}

bool audioSettingsDiffer(const AppSettings& lhs, const AppSettings& rhs) {
    return std::abs(lhs.masterVolume - rhs.masterVolume) > 0.0001f ||
           std::abs(lhs.musicVolume - rhs.musicVolume) > 0.0001f ||
           std::abs(lhs.sfxVolume - rhs.sfxVolume) > 0.0001f || lhs.audioDevice != rhs.audioDevice;
}

glm::vec3 cameraForward(float yaw, float pitch) {
    return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                    std::cos(pitch) * std::cos(yaw)));
}

bool pickWorldAtCursor(const WorldRenderer* worldRenderer, const VulkanContext& vulkanContext,
                       const glm::vec3& cameraPosition, float cameraYaw, float cameraPitch, WorldPickHit& hit) {
    if (!worldRenderer) return false;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    const VkExtent2D extent = vulkanContext.extent();
    if (extent.width == 0 || extent.height == 0) return false;

    const float ndcX = 2.0f * mouseX / static_cast<float>(extent.width) - 1.0f;
    const float ndcY = 2.0f * mouseY / static_cast<float>(extent.height) - 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(extent.width) / extent.height,
                                            0.05f, 2000.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraForward(cameraYaw, cameraPitch),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return worldRenderer->pickModel(cameraPosition, glm::normalize(glm::vec3(farPoint - nearPoint)), hit);
}


bool hasUpgrade(const playlevel::PlacedTower& tower, const std::string& nodeId) {
    return std::find(tower.unlockedUpgradeNodeIds.begin(), tower.unlockedUpgradeNodeIds.end(), nodeId) !=
           tower.unlockedUpgradeNodeIds.end();
}

bool canPurchaseUpgrade(const playlevel::PlacedTower& tower, const TowerArchetype::UpgradeNode& node,
                        multiplayer::PlayerId viewerId, float balance, std::string* reason = nullptr) {
    const int currentLevel = static_cast<int>(std::count(tower.unlockedUpgradeNodeIds.begin(),
                                                         tower.unlockedUpgradeNodeIds.end(), node.id));
    auto reject = [reason](const std::string& message) {
        if (reason) *reason = message;
        return false;
    };
    if (tower.ownerPlayerId != viewerId) return reject("Only this tower's owner can choose talents.");
    if (currentLevel >= static_cast<int>(node.upgradeLevels.size())) return reject("Maximum level reached.");
    if (static_cast<int>(tower.unlockedUpgradeNodeIds.size()) < node.minUpgradesRequired) {
        return reject("Requires " + std::to_string(node.minUpgradesRequired) + " total talent levels.");
    }
    for (const std::string& required : node.requiredNodeIds) {
        if (!hasUpgrade(tower, required)) return reject("Requires its preceding talent.");
    }
    for (const std::string& excluded : node.excludes) {
        if (hasUpgrade(tower, excluded)) return reject("Locked by your chosen specialization.");
    }
    if (balance < static_cast<float>(node.upgradeLevels[static_cast<std::size_t>(currentLevel)].cost)) {
        return reject("Not enough credits.");
    }
    if (reason) reason->clear();
    return true;
}

const char* damageTypeBadge(playlevel::DamageType type) {
    switch (type) {
        case playlevel::DamageType::Physical: return "PHY";
        case playlevel::DamageType::Fire: return "FIR";
        case playlevel::DamageType::Poison: return "PSN";
        case playlevel::DamageType::Arcane: return "ARC";
        case playlevel::DamageType::Electric: return "ELC";
        case playlevel::DamageType::Holy: return "HLY";
        case playlevel::DamageType::Necrotic: return "NEC";
    }
    return "DMG";
}

int towerTotalSpent(const playlevel::PlacedTower& tower, const TowerArchetype* archetype) {
    int totalSpent = tower.cost;
    if (!archetype) {
        return totalSpent;
    }
    std::unordered_map<std::string, int> purchasedLevels;
    for (const auto& nodeId : tower.unlockedUpgradeNodeIds) {
        const auto node = std::find_if(archetype->upgradeNodes.begin(), archetype->upgradeNodes.end(),
                                       [&nodeId](const auto& item) { return item.id == nodeId; });
        if (node == archetype->upgradeNodes.end()) {
            continue;
        }
        const int level = purchasedLevels[nodeId]++;
        if (level < static_cast<int>(node->upgradeLevels.size())) {
            totalSpent += node->upgradeLevels[static_cast<std::size_t>(level)].cost;
        }
    }
    return totalSpent;
}

int towerSellValue(const playlevel::PlacedTower& tower, const TowerArchetype* archetype) {
    return static_cast<int>(std::lround(static_cast<float>(towerTotalSpent(tower, archetype)) * kTowerSellRefundRatio));
}

void field(lua_State* lua, const char* name, const std::string& value) {
    lua_pushlstring(lua, value.data(), value.size());
    lua_setfield(lua, -2, name);
}

void field(lua_State* lua, const char* name, const char* value) {
    lua_pushstring(lua, value);
    lua_setfield(lua, -2, name);
}

void field(lua_State* lua, const char* name, bool value) {
    lua_pushboolean(lua, value);
    lua_setfield(lua, -2, name);
}

template <typename Value>
void number(lua_State* lua, const char* name, Value value) {
    if constexpr (std::is_integral_v<Value>) lua_pushinteger(lua, static_cast<lua_Integer>(value));
    else lua_pushnumber(lua, static_cast<lua_Number>(value));
    lua_setfield(lua, -2, name);
}

std::string argument(lua_State* lua, int index) {
    if (lua_type(lua, index) != LUA_TSTRING) throw std::runtime_error("Expected text");
    std::size_t length = 0;
    const char* value = lua_tolstring(lua, index, &length);
    return std::string(value, length);
}

int integerArgument(lua_State* lua, int index, int minimum, int maximum) {
    if (!lua_isinteger(lua, index)) throw std::runtime_error("Expected an integer");
    const auto value = lua_tointeger(lua, index);
    if (value < minimum || value > maximum) throw std::runtime_error("Value out of range");
    return static_cast<int>(value);
}

void hashCombine(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

void hashText(std::uint64_t& hash, std::string_view value) {
    hashCombine(hash, static_cast<std::uint64_t>(value.size()));
    for (unsigned char character : value) hashCombine(hash, character);
}

template <typename TValue>
void hashNumber(std::uint64_t& hash, TValue value) {
    if constexpr (std::is_floating_point_v<TValue>) {
        hashCombine(hash, static_cast<std::uint64_t>(std::llround(static_cast<double>(value) * 1000.0)));
    } else {
        hashCombine(hash, static_cast<std::uint64_t>(value));
    }
}

}

PlayLevelScene::PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                               const PlayLevelLaunchConfig& launchConfig, lambui_backend::VulkanUiRenderer& renderer,
                               AppSettings& settings, SDL_Window* window)
    : LuaUiScene("assets/scenes/PlayLevel.lua", renderer), vulkanContext_(vulkanContext), uiRenderer_(renderer), session_(session),
      launchConfig_(launchConfig), settings_(settings), window_(window),
      gameplayState_(matchSimulation_.gameplayState()), placedTowers_(matchSimulation_.placedTowers()),
      activeEnemies_(matchSimulation_.activeEnemies()) {}

PlayLevelScene::~PlayLevelScene() = default;

void PlayLevelScene::onEnter(LambUI::UIManager& ui, AudioEngine& audio) {
    ui_ = &ui;
    LuaUiScene::onEnter(ui, audio);
    lastPublishedUiStateRevision_ = 0;
    publishStateToLuaIfDirty();
}

void PlayLevelScene::bindSceneApi(lua_State* lua, AudioEngine& audio) {
    lua_ = lua;
    audio_ = &audio;
    settingsLastWriteTime_ = tryGetLastWriteTime(SettingsManager().settingsFilePath()).value_or(std::filesystem::file_time_type{});
    onlineMatch_ = session_.isInParty();
    localPlayerId_ = session_.localPlayerId() == 0 ? 1 : session_.localPlayerId();
    matchSimulation_.reset();
    if (!session_.isClient()) {
        matchSimulation_.registerPlayer(localPlayerId_, gameplayState_.playerMoney);
        localMatchHost_.registerPlayer(localPlayerId_);
    }
    towerLoadController_ = std::make_unique<TowerLoadController>(lua);
    towerLoadController_->discoverTowerArchetypesInDirectory("assets/models/towers");
    towerLoadController_->setLoadoutIds(launchConfig_.towerLoadoutIds);
    enemyLoadController_ = std::make_unique<EnemyLoadController>(lua);
    if (!enemyLoadController_->loadEnemyArchetype("assets/models/enemy/goblin1.enemy.lua"))
        enemyLoadController_->registerArchetype(EnemyArchetype{});
    enemyLoadController_->loadEnemyArchetype("assets/models/enemy/goblin_scout.enemy.lua");
    loadWaveDefinitions();
    beginWorldLoad();
    lua_newtable(lua);
    for (const char* name : {"State", "StateRevision", "Start", "SelectSlot", "CancelPlacement", "ClearSelection", "Upgrade",
                             "Sell", "Pause", "Restart", "Lobby", "Retry", "SendChat", "SetVolume", "Preview"}) {
        lua_pushlightuserdata(lua, this);
        lua_pushstring(lua, name);
        lua_pushcclosure(lua, dispatch, 2);
        lua_setfield(lua, -2, name);
    }
    lua_setglobal(lua, "Play");
}

void PlayLevelScene::onExit(LambUI::UIManager& ui) {
    if (mouseLookActive_) SDL_SetWindowRelativeMouseMode(window_, false);
    mouseLookActive_ = false;
    vulkanContext_.waitIdle();
    worldRenderer_.reset();
    towerLoadController_.reset();
    enemyLoadController_.reset();
    LuaUiScene::onExit(ui);
    lua_ = nullptr;
    ui_ = nullptr;
}

SceneTransition PlayLevelScene::update(float dt) {
    if ((onlineMatch_ && !session_.isInParty()) || (session_.isClient() && session_.consumeMatchEnded()))
        return SceneId::Lobby;
    consumeChat();
    pollExternalSettings(dt);
    pollGameplayTuning(dt);
    if (worldRenderer_ && snapshot_.phase == PlayLevelUiPhase::Loading) {
        worldRenderer_->tickLoad();
        snapshot_.loadingProgress = worldRenderer_->loadProgress();
        snapshot_.loadingActivity = worldRenderer_->loadActivity();
        snapshot_.supportingText = snapshot_.loadingActivity;
        if (worldRenderer_->loadFailed()) {
            snapshot_.phase = PlayLevelUiPhase::LoadFailed;
            snapshot_.headline = "Deployment failed";
            snapshot_.supportingText = worldRenderer_->statusMessage();
        } else if (worldRenderer_->isLoaded()) {
            snapshot_.phase = PlayLevelUiPhase::WaitingToStart;
            if (onlineMatch_ && !loadedReadySignaled_) {
                session_.signalLocalLoadedReady();
                loadedReadySignaled_ = true;
            }
        }
    }
    if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart && session_.isClient() && session_.isMatchStarted())
        gameplayState_.matchStatus = MatchStatus::Running;
    if (!pauseMenuVisible_) {
        towerPreviewSpinRadians_ = std::fmod(towerPreviewSpinRadians_ + dt * 0.55f, 6.2831853f);
        updateCamera(dt);
        updateTowerPlacement();
    } else {
        leftMouseDown_ = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    }
    if (!pauseMenuVisible_ || onlineMatch_) updateMatchSimulation(dt);
    refreshHud();
    publishStateToLuaIfDirty();
    const auto luaTransition = LuaUiScene::update(dt);
    return pendingTransition_ ? std::exchange(pendingTransition_, std::nullopt) : luaTransition;
}

void PlayLevelScene::refreshHud() {
    if (snapshot_.phase == PlayLevelUiPhase::Loading || snapshot_.phase == PlayLevelUiPhase::LoadFailed) return;
    PlayLevelUiStateInput input;
    input.phase = gameplayState_.matchStatus == MatchStatus::Victory ? PlayLevelUiPhase::Victory :
                  gameplayState_.matchStatus == MatchStatus::Defeat ? PlayLevelUiPhase::Defeat :
                  gameplayState_.matchStatus == MatchStatus::Running ? PlayLevelUiPhase::Running :
                  PlayLevelUiPhase::WaitingToStart;
    input.levelName = launchConfig_.displayName;
    input.baseHealth = static_cast<int>(gameplayState_.baseHealth);
    input.money = static_cast<int>(gameplayState_.playerMoney);
    input.currentWave = gameplayState_.currentWave;
    input.waveCount = static_cast<int>(matchSimulation_.waveController().waveCount());
    input.enemiesAlive = gameplayState_.enemiesAlive;
    input.enemiesToSpawn = gameplayState_.enemiesToSpawn;
    input.waveInProgress = gameplayState_.waveInProgress;
    input.preWaveCountdownActive = gameplayState_.waveCountdownActive;
    input.preWaveCountdownRemainingSeconds = gameplayState_.waveCountdownRemainingSeconds;
    input.preWaveCountdownDurationSeconds = gameplayState_.waveCountdownDurationSeconds;
    input.roundCountdownRemainingSeconds = gameplayState_.waveRoundRemainingSeconds;
    input.roundCountdownDurationSeconds = gameplayState_.waveRoundDurationSeconds;
    input.worldReady = worldRenderer_ && worldRenderer_->isLoaded();
    input.routeReady = worldRenderer_ && worldRenderer_->routePoints().size() >= 2;
    input.enemyTemplateReady = worldRenderer_ && worldRenderer_->hasAnimatedEntityTemplate();
    snapshot_ = buildPlayLevelUiSnapshot(input);
    if (session_.isClient() || (onlineMatch_ && !session_.allMembersLoadedReady())) {
        snapshot_.startWaveEnabled = false;
        snapshot_.startWaveDisabledReason = session_.isClient() ? "Waiting for the host to start." : "Waiting for players to load.";
    }
}

std::uint64_t PlayLevelScene::computeUiStateFingerprint() const {
    std::uint64_t hash = 1469598103934665603ULL;
    if (ui_) {
        const auto rect = ui_->GetRoot().GetComputedRect();
        hashNumber(hash, rect.width);
        hashNumber(hash, rect.height);
    }
    hashNumber(hash, static_cast<int>(snapshot_.phase));
    hashText(hash, snapshot_.headline);
    hashText(hash, snapshot_.supportingText);
    hashText(hash, snapshot_.countdownLabel);
    hashNumber(hash, snapshot_.countdownSeconds);
    hashNumber(hash, snapshot_.countdownProgress);
    hashNumber(hash, snapshot_.loadingProgress);
    hashNumber(hash, snapshot_.startWaveEnabled);
    hashText(hash, snapshot_.startWaveDisabledReason);
    hashNumber(hash, snapshot_.loadoutVisible);
    hashNumber(hash, snapshot_.countdownVisible);
    hashNumber(hash, gameplayState_.baseHealth);
    hashNumber(hash, gameplayState_.playerMoney);
    hashNumber(hash, gameplayState_.currentWave);
    hashNumber(hash, gameplayState_.enemiesAlive);
    hashNumber(hash, selectedTowerSlot_);
    hashNumber(hash, selectedTowerRuntimeId_);
    hashNumber(hash, selectedEnemyRuntimeId_);
    hashNumber(hash, pauseMenuVisible_);
    hashNumber(hash, onlineMatch_);
    hashNumber(hash, session_.isClient());
    hashText(hash, placementReason_);
    hashNumber(hash, placementSample_.hit);
    hashNumber(hash, settings_.masterVolume);
    hashNumber(hash, settings_.musicVolume);
    hashNumber(hash, settings_.sfxVolume);
    hashNumber(hash, chat_.size());
    if (!chat_.empty()) hashText(hash, chat_.back());

    const auto selected = std::find_if(placedTowers_.begin(), placedTowers_.end(), [this](const auto& tower) {
        return tower.runtimeId == selectedTowerRuntimeId_;
    });
    if (selected != placedTowers_.end()) {
        hashText(hash, selected->towerId);
        hashNumber(hash, selected->ownerPlayerId);
        hashNumber(hash, selected->attackDamage);
        hashNumber(hash, selected->attackRange);
        hashNumber(hash, selected->attackIntervalSeconds);
        hashNumber(hash, selected->armorPiercing);
        hashNumber(hash, selected->burnDamagePerSecond);
        hashNumber(hash, selected->burnDuration);
        hashNumber(hash, selected->slowAmount);
        hashNumber(hash, selected->slowDuration);
        hashNumber(hash, selected->freezeChance);
        hashNumber(hash, selected->freezeDuration);
        hashNumber(hash, selected->critChance);
        hashNumber(hash, selected->critDamageMul);
        hashNumber(hash, selected->splashRadius);
        hashNumber(hash, selected->chainRange);
        hashNumber(hash, selected->chainTargetCount);
        hashNumber(hash, selected->ricochetRange);
        hashNumber(hash, selected->ricochetCount);
        hashNumber(hash, selected->unlockedUpgradeNodeIds.size());
        for (const auto& id : selected->unlockedUpgradeNodeIds) hashText(hash, id);
    }

    const auto enemy = std::find_if(activeEnemies_.begin(), activeEnemies_.end(), [this](const auto& item) {
        return item.runtimeId == selectedEnemyRuntimeId_;
    });
    if (enemy != activeEnemies_.end()) {
        hashText(hash, enemy->enemyId);
        hashNumber(hash, enemy->health);
        hashNumber(hash, enemy->maxHealth);
        hashNumber(hash, enemy->shield);
        hashNumber(hash, enemy->maxShield);
        hashNumber(hash, enemy->armor);
        hashNumber(hash, enemy->moveSpeed);
        hashNumber(hash, enemy->rewardMoney);
        hashNumber(hash, enemy->baseDamage);
    }

    return hash;
}

std::uint64_t PlayLevelScene::currentUiStateRevision() {
    refreshHud();
    const std::uint64_t fingerprint = computeUiStateFingerprint();
    if (fingerprint != uiStateFingerprint_) {
        uiStateFingerprint_ = fingerprint;
        ++uiStateRevision_;
    }
    return uiStateRevision_;
}

void PlayLevelScene::publishStateToLuaIfDirty() {
    if (!lua_) return;

    const std::uint64_t revision = currentUiStateRevision();
    if (revision == lastPublishedUiStateRevision_) return;
    lastPublishedUiStateRevision_ = revision;

    lua_getglobal(lua_, "OnStateChanged");
    if (!lua_isfunction(lua_, -1)) {
        lua_pop(lua_, 1);
        return;
    }
    pushState(lua_);
    if (lua_pcall(lua_, 1, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(lua_, -1);
        std::fprintf(stderr, "PlayLevelScene: OnStateChanged failed: %s\n", error ? error : "unknown error");
        lua_pop(lua_, 1);
    }
}

bool PlayLevelScene::pointerIsOverHud() const {
    if (!lua_ || pauseMenuVisible_) return true;
    float mouseX = 0, mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    lua_getglobal(lua_, "PlayPointerOverHud");
    if (!lua_isfunction(lua_, -1)) { lua_pop(lua_, 1); return true; }
    lua_pushnumber(lua_, mouseX);
    lua_pushnumber(lua_, mouseY);
    if (lua_pcall(lua_, 2, 1, 0) != LUA_OK) { lua_pop(lua_, 1); return true; }
    const bool hit = lua_toboolean(lua_, -1) != 0;
    lua_pop(lua_, 1);
    return hit;
}

void PlayLevelScene::setPauseMenuVisible(bool visible) {
    pauseMenuVisible_ = visible;
    if (visible && mouseLookActive_) {
        SDL_SetWindowRelativeMouseMode(window_, false);
        mouseLookActive_ = false;
    }
    placementSample_ = {};
    leftMouseDown_ = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    syncTowerInstances();
}

SceneTransition PlayLevelScene::onKeyDown(uint32_t scanCode) {
    if (scanCode != LambUI::ScanCode::Escape) return std::nullopt;
    if (!pauseMenuVisible_ && selectedTowerSlot_ >= 0) {
        selectedTowerSlot_ = -1;
        placementSample_ = {};
        towerPlacementPreviewResolver_.reset();
    } else if (!pauseMenuVisible_ && (selectedTowerRuntimeId_ || selectedEnemyRuntimeId_)) {
        selectedTowerRuntimeId_ = 0;
        selectedEnemyRuntimeId_ = 0;
    } else {
        setPauseMenuVisible(!pauseMenuVisible_);
    }
    syncTowerInstances();
    return std::nullopt;
}

bool PlayLevelScene::handleSceneShortcut(uint32_t scanCode) {
    if (pauseMenuVisible_ || gameplayState_.matchStatus != MatchStatus::Running ||
        (ui_ && dynamic_cast<LambUI::UIInputBox*>(ui_->GetFocusedWidget()))) return false;
    if (scanCode < '1' || scanCode > '5') return false;
    const int slot = static_cast<int>(scanCode - '1');
    if (towerLoadController_->archetypeAtLoadoutSlot(slot)) {
        selectedTowerSlot_ = selectedTowerSlot_ == slot ? -1 : slot;
        selectedTowerRuntimeId_ = 0;
        selectedEnemyRuntimeId_ = 0;
        towerPlacementPreviewResolver_.reset();
    }
    return true;
}

void PlayLevelScene::consumeChat() {
    for (const auto& message : session_.consumeChatMessages())
        chat_.push_back(message.playerId == 0 || message.isEmote ? message.text : message.displayName + ": " + message.text);
    for (const auto& error : session_.consumeChatErrors()) chat_.push_back(error);
    if (chat_.size() > 100) chat_.erase(chat_.begin(), chat_.end() - 100);
}

void PlayLevelScene::pollExternalSettings(float dt) {
    settingsPollAccumulator_ += dt;
    if (settingsPollAccumulator_ < kSettingsPollIntervalSeconds) return;
    settingsPollAccumulator_ = 0.0f;
    SettingsManager manager;
    const auto writeTime = tryGetLastWriteTime(manager.settingsFilePath());
    if (!writeTime || *writeTime <= settingsLastWriteTime_) return;
    settingsLastWriteTime_ = *writeTime;
    const auto updated = manager.loadOrCreateDefaults();
    if (!audioSettingsDiffer(settings_, updated)) return;
    settings_.masterVolume = updated.masterVolume;
    settings_.musicVolume = updated.musicVolume;
    settings_.sfxVolume = updated.sfxVolume;
    settings_.audioDevice = updated.audioDevice;
    audio_->setEffectiveSettings(settings_);
}

int PlayLevelScene::pushState(lua_State* lua) {
    currentUiStateRevision();
    lua_newtable(lua);
    const char* phases[] = {"loading", "failed", "ready", "running", "paused", "victory", "defeat"};
    field(lua, "phase", phases[static_cast<int>(snapshot_.phase)]);
    field(lua, "level", launchConfig_.displayName);
    field(lua, "headline", snapshot_.headline);
    field(lua, "description", snapshot_.supportingText);
    field(lua, "canStart", snapshot_.startWaveEnabled);
    field(lua, "startReason", snapshot_.startWaveDisabledReason);
    field(lua, "paused", pauseMenuVisible_);
    field(lua, "online", onlineMatch_);
    field(lua, "client", session_.isClient());
    field(lua, "loadoutVisible", snapshot_.loadoutVisible);
    field(lua, "countdownVisible", snapshot_.countdownVisible);
    field(lua, "countdownLabel", snapshot_.countdownLabel);
    number(lua, "countdown", snapshot_.countdownSeconds);
    number(lua, "countdownProgress", snapshot_.countdownProgress);
    number(lua, "loadingProgress", snapshot_.loadingProgress);
    number(lua, "health", gameplayState_.baseHealth);
    number(lua, "money", gameplayState_.playerMoney);
    number(lua, "wave", gameplayState_.currentWave);
    number(lua, "waveCount", matchSimulation_.waveController().waveCount());
    number(lua, "enemies", gameplayState_.enemiesAlive);
    number(lua, "selectedSlot", selectedTowerSlot_ + 1);
    field(lua, "placement", placementReason_);
    field(lua, "canPlace", placementSample_.hit && placementReason_.empty());
    number(lua, "masterVolume", settings_.masterVolume);
    number(lua, "musicVolume", settings_.musicVolume);
    number(lua, "sfxVolume", settings_.sfxVolume);
    lua_newtable(lua);
    for (int slot = 0; slot < 5; ++slot) {
        lua_newtable(lua);
        if (const auto* tower = towerLoadController_->archetypeAtLoadoutSlot(slot)) {
            field(lua, "name", tower->displayName);
            field(lua, "bio", tower->bio);
            field(lua, "portrait", tower->previewImagePath);
            number(lua, "cost", tower->cost);
            field(lua, "available", true);
        } else {
            field(lua, "name", "Empty");
            number(lua, "cost", 0);
            field(lua, "available", false);
        }
        lua_rawseti(lua, -2, slot + 1);
    }
    lua_setfield(lua, -2, "slots");
    lua_newtable(lua);
    for (std::size_t index = 0; index < chat_.size(); ++index) {
        lua_pushlstring(lua, chat_[index].data(), chat_[index].size());
        lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(lua, -2, "chat");
    const auto selected = std::find_if(placedTowers_.begin(), placedTowers_.end(), [this](const auto& tower) {
        return tower.runtimeId == selectedTowerRuntimeId_;
    });
    if (selected != placedTowers_.end()) {
        const auto* archetype = towerLoadController_->findArchetype(selected->towerId);
        lua_newtable(lua);
        field(lua, "kind", "tower");
        field(lua, "archetype", selected->towerId);
        field(lua, "name", archetype ? archetype->displayName : selected->towerId);
        field(lua, "bio", archetype ? archetype->bio : "");
        field(lua, "owned", selected->ownerPlayerId == localPlayerId_);
        number(lua, "id", selected->runtimeId);
        number(lua, "damage", selected->attackDamage);
        number(lua, "range", selected->attackRange);
        number(lua, "rate", 1.0f / std::max(0.01f, selected->attackIntervalSeconds));
        number(lua, "spent", towerTotalSpent(*selected, archetype));
        number(lua, "sell", towerSellValue(*selected, archetype));
        field(lua, "damageType", damageTypeBadge(selected->damageType));
        number(lua, "armorPiercing", selected->armorPiercing);
        std::ostringstream effects;
        effects << std::fixed << std::setprecision(1);
        if (selected->burnDamagePerSecond > 0) effects << "Burn " << selected->burnDamagePerSecond << "/s  " << selected->burnDuration << "s\n";
        if (selected->slowAmount > 0) effects << "Slow " << selected->slowAmount * 100 << "%  " << selected->slowDuration << "s\n";
        if (selected->freezeChance > 0) effects << "Freeze " << selected->freezeChance * 100 << "%  " << selected->freezeDuration << "s\n";
        if (selected->critChance > 0) effects << "Crit " << selected->critChance * 100 << "% x" << selected->critDamageMul << "\n";
        if (selected->splashRadius > 0) effects << "Splash " << selected->splashRadius << "\n";
        if (selected->chainTargetCount > 1) effects << "Chain " << selected->chainRange << " x" << selected->chainTargetCount << "\n";
        if (selected->ricochetCount > 0) effects << "Ricochet " << selected->ricochetRange << " x" << selected->ricochetCount;
        field(lua, "effects", effects.str());
        lua_newtable(lua);
        int index = 1;
        if (archetype) for (const auto& node : archetype->upgradeNodes) {
            lua_newtable(lua);
            field(lua, "id", node.id);
            field(lua, "name", node.displayName);
            field(lua, "description", node.description);
            number(lua, "minUpgradesRequired", node.minUpgradesRequired);
            lua_newtable(lua);
            int requiredIndex = 1;
            for (const auto& required : node.requiredNodeIds) {
                lua_pushlstring(lua, required.data(), required.size());
                lua_rawseti(lua, -2, requiredIndex++);
            }
            lua_setfield(lua, -2, "requires");
            const auto level = std::count(selected->unlockedUpgradeNodeIds.begin(), selected->unlockedUpgradeNodeIds.end(), node.id);
            number(lua, "level", level);
            number(lua, "maxLevel", node.upgradeLevels.size());
            number(lua, "cost", level < static_cast<int>(node.upgradeLevels.size()) ? node.upgradeLevels[level].cost : 0);
            std::string reason;
            field(lua, "enabled", canPurchaseUpgrade(*selected, node, localPlayerId_, gameplayState_.playerMoney, &reason));
            field(lua, "reason", reason);
            lua_rawseti(lua, -2, index++);
        }
        lua_setfield(lua, -2, "upgrades");
        lua_setfield(lua, -2, "selection");
    } else {
        const auto enemy = std::find_if(activeEnemies_.begin(), activeEnemies_.end(), [this](const auto& item) {
            return item.runtimeId == selectedEnemyRuntimeId_;
        });
        if (enemy != activeEnemies_.end()) {
            const auto* archetype = enemyLoadController_->findArchetype(enemy->enemyId);
            lua_newtable(lua);
            field(lua, "kind", "enemy");
            field(lua, "name", archetype ? archetype->displayName : enemy->enemyId);
            field(lua, "bio", archetype ? archetype->description : "");
            number(lua, "id", enemy->runtimeId);
            number(lua, "health", enemy->health);
            number(lua, "maxHealth", enemy->maxHealth);
            number(lua, "shield", enemy->shield);
            number(lua, "maxShield", enemy->maxShield);
            number(lua, "armor", enemy->armor);
            number(lua, "speed", enemy->moveSpeed);
            number(lua, "reward", enemy->rewardMoney);
            number(lua, "baseDamage", enemy->baseDamage);
            std::ostringstream resistances;
            for (const auto& [type, value] : enemy->resistances)
                resistances << damageTypeBadge(type) << " " << std::lround(value * 100) << "%\n";
            field(lua, "resistances", resistances.str().empty() ? "No resistances" : resistances.str());
            lua_setfield(lua, -2, "selection");
        }
    }
    return 1;
}

int PlayLevelScene::pushStateRevision(lua_State* lua) {
    lua_pushinteger(lua, static_cast<lua_Integer>(currentUiStateRevision()));
    return 1;
}

int PlayLevelScene::dispatch(lua_State* lua) {
    auto& scene = *static_cast<PlayLevelScene*>(lua_touserdata(lua, lua_upvalueindex(1)));
    const std::string_view action = lua_tostring(lua, lua_upvalueindex(2));
    bool success = true;
    std::string message;
    try {
        if (action == "State") return scene.pushState(lua);
        if (action == "StateRevision") return scene.pushStateRevision(lua);
        if (action == "Start") {
            scene.refreshHud();
            if (!scene.snapshot_.startWaveVisible || !scene.snapshot_.startWaveEnabled || scene.pauseMenuVisible_)
                throw std::runtime_error(scene.snapshot_.startWaveDisabledReason.empty() ? "Match cannot start now." : scene.snapshot_.startWaveDisabledReason);
            if (scene.onlineMatch_ && !scene.session_.beginMatch()) throw std::runtime_error("Waiting for players to load.");
            scene.restartMatch();
        } else if (action == "SelectSlot") {
            const int slot = integerArgument(lua, 1, 1, 5) - 1;
            const auto* tower = scene.towerLoadController_->archetypeAtLoadoutSlot(slot);
            if (!tower || scene.pauseMenuVisible_ || scene.gameplayState_.matchStatus != MatchStatus::Running)
                throw std::runtime_error("Tower unavailable.");
            if (scene.gameplayState_.playerMoney < tower->cost) throw std::runtime_error("Not enough gold.");
            scene.selectedTowerSlot_ = scene.selectedTowerSlot_ == slot ? -1 : slot;
            scene.selectedTowerRuntimeId_ = 0;
            scene.selectedEnemyRuntimeId_ = 0;
            scene.placementReason_.clear();
            scene.placementSample_ = {};
            scene.towerPlacementPreviewResolver_.reset();
            scene.leftMouseDown_ = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
        } else if (action == "CancelPlacement") {
            scene.selectedTowerSlot_ = -1;
            scene.placementSample_ = {};
            scene.placementReason_.clear();
            scene.towerPlacementPreviewResolver_.reset();
        } else if (action == "ClearSelection") {
            scene.selectedTowerRuntimeId_ = 0;
            scene.selectedEnemyRuntimeId_ = 0;
        } else if (action == "Upgrade" || action == "Sell") {
            if (scene.pauseMenuVisible_ || scene.gameplayState_.matchStatus != MatchStatus::Running)
                throw std::runtime_error("Match is not running.");
            if (action == "Upgrade") {
                multiplayer::UpgradeTowerCommand command;
                command.towerRuntimeId = scene.selectedTowerRuntimeId_;
                command.upgradeNodeId = argument(lua, 1);
                success = scene.submitCommand(std::move(command));
            } else {
                multiplayer::SellTowerCommand command;
                command.towerRuntimeId = scene.selectedTowerRuntimeId_;
                success = scene.submitCommand(command);
            }
            if (!success) message = "The host rejected this action.";
        } else if (action == "Pause") {
            scene.setPauseMenuVisible(lua_toboolean(lua, 1) != 0);
        } else if (action == "Restart") {
            if (scene.session_.isClient() || (scene.gameplayState_.matchStatus != MatchStatus::Victory &&
                scene.gameplayState_.matchStatus != MatchStatus::Defeat)) throw std::runtime_error("Replay unavailable.");
            scene.setPauseMenuVisible(false);
            scene.restartMatch();
        } else if (action == "Lobby") {
            if (scene.session_.isClient()) scene.session_.leaveParty();
            else scene.session_.endMatch();
            scene.pendingTransition_ = SceneId::Lobby;
        } else if (action == "Retry") {
            if (scene.snapshot_.phase != PlayLevelUiPhase::LoadFailed) throw std::runtime_error("Retry unavailable.");
            scene.vulkanContext_.waitIdle();
            scene.beginWorldLoad();
        } else if (action == "SendChat") {
            success = scene.session_.sendChatMessage(argument(lua, 1));
            if (!success) message = "Message could not be sent.";
        } else if (action == "SetVolume") {
            const auto name = argument(lua, 1);
            const auto value = lua_tonumber(lua, 2);
            if (!lua_isnumber(lua, 2) || !std::isfinite(value) || value < 0 || value > 1)
                throw std::runtime_error("Invalid volume.");
            if (name == "masterVolume") scene.settings_.masterVolume = static_cast<float>(value);
            else if (name == "musicVolume") scene.settings_.musicVolume = static_cast<float>(value);
            else if (name == "sfxVolume") scene.settings_.sfxVolume = static_cast<float>(value);
            else throw std::runtime_error("Unknown volume.");
            scene.audio_->setEffectiveSettings(scene.settings_);
            success = SettingsManager().save(scene.settings_);
            if (!success) message = "Volume changed, but settings could not be saved.";
        } else if (action == "Preview") {
            const int slot = integerArgument(lua, 1, 1, 5) - 1;
            float values[4]{};
            for (int index = 0; index < 4; ++index) {
                const auto value = lua_tonumber(lua, index + 2);
                if (!lua_isnumber(lua, index + 2) || !std::isfinite(value) || value < -32768 || value > 32768 ||
                    (index >= 2 && value < 0))
                    throw std::runtime_error("Invalid preview bounds.");
                values[index] = static_cast<float>(value);
            }
            const auto* context = scene.uiRenderer_.GetCustomRenderContext();
            if (!context) throw std::runtime_error("Preview requires a canvas render callback.");
            if (scene.worldRenderer_ && scene.worldRenderer_->isLoaded() && scene.snapshot_.loadoutVisible &&
                !scene.pauseMenuVisible_) {
                std::vector<TowerPreviewPanel> panels(5);
                for (int index = 0; index < 5; ++index) {
                    const auto* tower = scene.towerLoadController_->archetypeAtLoadoutSlot(index);
                    panels[index].prototypeIndex = tower ? scene.towerLoadController_->templatePrototypeIndex(tower->id) : -1;
                }
                panels[slot] = {panels[slot].prototypeIndex, values[0], values[1], values[2], values[3]};
                scene.worldRenderer_->renderTowerPreviewPanels(context->commandBuffer, context->framebufferExtent,
                    panels, scene.towerPreviewSpinRadians_, &context->scissor);
            }
        }
    } catch (const std::exception& error) {
        success = false;
        message = error.what();
    }
    lua_pushboolean(lua, success);
    lua_pushlstring(lua, message.data(), message.size());
    return 2;
}

bool PlayLevelScene::loadWaveDefinitions() {
    return matchSimulation_.waveController().loadWaveDefinitions(
        lua_, "assets/scenes/PlayLevelWaves.lua", enemyLoadController_->defaultId(),
        [this](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
            const EnemyArchetype* enemy = enemyLoadController_->findArchetype(enemyId);
            return enemy ? std::optional{PlayLevelWaveController::EnemyWaveDefaults{enemy->spawnIntervalSeconds}}
                         : std::nullopt;
        });
}


void PlayLevelScene::beginWorldLoad() {
    snapshot_ = {};
    snapshot_.phase = PlayLevelUiPhase::Loading;
    snapshot_.levelName = launchConfig_.displayName;
    snapshot_.headline = "Loading " + launchConfig_.displayName;
    snapshot_.supportingText = "Preparing the battlefield...";

    if (launchConfig_.mapAssetPath.empty() || launchConfig_.displayName.empty()) {
        snapshot_.phase = PlayLevelUiPhase::LoadFailed;
        snapshot_.headline = "Deployment failed";
        snapshot_.supportingText = "The selected level configuration is incomplete.";
        worldRenderer_.reset();
        return;
    }

    WorldAssetSpec assetSpec;
    assetSpec.startModelPath = launchConfig_.startModelPath;
    assetSpec.endModelPath = launchConfig_.endModelPath;
    for (const std::string& modelPath : launchConfig_.animatedTemplateModelPaths) {
        assetSpec.animatedTemplateModelPaths.emplace_back(modelPath);
    }
    if (towerLoadController_) {
        towerLoadController_->populateWorldAssets(assetSpec);
    }
    worldRenderer_ = std::make_unique<WorldRenderer>(nullptr, vulkanContext_);
    worldRenderer_->beginLoad(launchConfig_.mapAssetPath, assetSpec);
}


void PlayLevelScene::updateCamera(float dt) {
    SDL_Window* window = window_;
    if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
        return;
    }
    if (ui_ && dynamic_cast<LambUI::UIInputBox*>(ui_->GetFocusedWidget())) {
        return;
    }

    const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(nullptr, nullptr);
    const bool wantsMouseLook = (mouseButtons & SDL_BUTTON_RMASK) != 0;
    if (wantsMouseLook != mouseLookActive_) {
        if (SDL_SetWindowRelativeMouseMode(window, wantsMouseLook)) {
            mouseLookActive_ = wantsMouseLook;
            SDL_GetRelativeMouseState(nullptr, nullptr);
        }
    }

    if (mouseLookActive_) {
        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        SDL_GetRelativeMouseState(&mouseDeltaX, &mouseDeltaY);
        cameraYaw_ -= mouseDeltaX * 0.003f;
        cameraPitch_ = glm::clamp(cameraPitch_ - mouseDeltaY * 0.003f, -1.48f, 1.48f);
    }

    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    const float frameTime = std::min(dt, 0.1f);
    const float speedMultiplier = (keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT]) ? 4.0f : 1.0f;
    const float movement = 8.0f * speedMultiplier * frameTime;
    const glm::vec3 forward = cameraForward(cameraYaw_, cameraPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (keyboard[SDL_SCANCODE_W]) cameraPosition_ += forward * movement;
    if (keyboard[SDL_SCANCODE_S]) cameraPosition_ -= forward * movement;
    if (keyboard[SDL_SCANCODE_A]) cameraPosition_ -= right * movement;
    if (keyboard[SDL_SCANCODE_D]) cameraPosition_ += right * movement;
    if (keyboard[SDL_SCANCODE_SPACE]) cameraPosition_.y += movement;
    if (keyboard[SDL_SCANCODE_Q]) cameraPosition_.y -= movement;
}

void PlayLevelScene::renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent) {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    const glm::vec3 forward = cameraForward(cameraYaw_, cameraPitch_);
    const glm::mat4 view = glm::lookAt(cameraPosition_, cameraPosition_ + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    worldRenderer_->render(commandBuffer, extent, view);
}

const TowerArchetype* PlayLevelScene::selectedTower() const {
    return towerLoadController_ ? towerLoadController_->archetypeAtLoadoutSlot(selectedTowerSlot_) : nullptr;
}


glm::mat4 PlayLevelScene::buildTowerTransform(const glm::vec3& position,
                                              float facingYawOffsetDegrees,
                                              float renderScale) const {
    return glm::translate(glm::mat4{1.0f}, position) *
           glm::rotate(glm::mat4{1.0f}, glm::radians(facingYawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, renderScale)));
}

void PlayLevelScene::updateTowerPlacement() {
    const bool leftMouseDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    const bool leftClicked = leftMouseDown && !leftMouseDown_;
    leftMouseDown_ = leftMouseDown;

    const TowerArchetype* tower = selectedTower();
    updateWorldHover();
    if (!tower || !worldRenderer_ || !worldRenderer_->isLoaded() ||
        gameplayState_.matchStatus != MatchStatus::Running) {
        if (!tower && leftClicked && !pointerIsOverHud() && gameplayState_.matchStatus == MatchStatus::Running) {
            updateWorldSelection();
        }
        placementSample_ = {};
        if (!tower) {
            towerPlacementPreviewResolver_.reset();
        }
        syncTowerInstances();
        return;
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    const VkExtent2D extent = vulkanContext_.extent();
    const glm::vec3 forward = cameraForward(cameraYaw_, cameraPitch_);
    const glm::mat4 view = glm::lookAt(cameraPosition_, cameraPosition_ + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const TowerPlacementRules::Context placementContext{gameplayState_, placedTowers_, worldRenderer_.get(),
                                                        worldRenderer_->placementRegions(), 30.0f, 2.0f};
    const auto validatePlacement = [this, tower, &placementContext](const glm::vec3& worldPosition,
                                                                    int footprintSampleCount,
                                                                    const PlacementTerrainSample& terrainSample) {
        return TowerPlacementRules::validatePlacement(placementContext, *tower, worldPosition, footprintSampleCount,
                                                      terrainSample, gameplayState_.playerMoney);
    };
    const auto resolved = towerPlacementPreviewResolver_.resolve(
        tower,
        [&]() {
            return TowerPlacementRules::sampleTerrainAtScreenPoint(
                placementContext, view, cameraPosition_, mouseX, mouseY, static_cast<float>(extent.width),
                static_cast<float>(extent.height));
        },
        validatePlacement);
    placementSample_ = towerPlacementPreviewResolver_.lastTerrainSample();
    placementSample_.hit = resolved.hasHit;
    placementSample_.worldPosition = resolved.worldPos;
    placementReason_ = resolved.hasHit ? resolved.reason : "Cursor is not over valid terrain.";

    const bool placementPointerOverHud = pointerIsOverHud();
    if (leftClicked && !placementPointerOverHud && placementSample_.hit && placementReason_.empty()) {
        const std::string finalReason = TowerPlacementRules::validatePlacement(
            placementContext, *tower, placementSample_.worldPosition, 8, placementSample_, gameplayState_.playerMoney);
        if (finalReason.empty()) {
            multiplayer::PlaceTowerCommand placeTower;
            placeTower.towerArchetypeId = tower->id;
            placeTower.requestedPosition = {placementSample_.worldPosition.x, placementSample_.worldPosition.y,
                                            placementSample_.worldPosition.z};
            if (submitCommand(std::move(placeTower))) {
                selectedTowerSlot_ = -1;
                placementSample_ = {};
                towerPlacementPreviewResolver_.reset();
                placementReason_ = tower->displayName + " deployed.";
                refreshHud();
            } else {
                placementReason_ = "Tower placement was rejected by the host.";
            }
        } else {
            placementReason_ = finalReason;
        }
    }
    syncTowerInstances();
}

void PlayLevelScene::updateWorldHover() {
    hoveredTowerRuntimeId_ = 0;
    hoveredEnemyRuntimeId_ = 0;
    if (!worldRenderer_ || !worldRenderer_->isLoaded() || pointerIsOverHud() || mouseLookActive_ ||
        selectedTower() || gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }

    WorldPickHit hit;
    if (!NodeSpireUi::pickWorldAtCursor(worldRenderer_.get(), vulkanContext_, cameraPosition_, cameraYaw_,
                                        cameraPitch_, hit)) return;
    if (hit.entityKind == WorldEntityKind::Tower && hit.instanceIndex >= 0 &&
        static_cast<std::size_t>(hit.instanceIndex) < placedTowers_.size()) {
        hoveredTowerRuntimeId_ = placedTowers_[static_cast<std::size_t>(hit.instanceIndex)].runtimeId;
    } else if (hit.entityKind == WorldEntityKind::Enemy && hit.instanceIndex >= 0 &&
               static_cast<std::size_t>(hit.instanceIndex) < activeEnemies_.size()) {
        hoveredEnemyRuntimeId_ = activeEnemies_[static_cast<std::size_t>(hit.instanceIndex)].runtimeId;
    }
}

void PlayLevelScene::updateWorldSelection() {
    WorldPickHit hit;
    const bool picked = NodeSpireUi::pickWorldAtCursor(worldRenderer_.get(), vulkanContext_, cameraPosition_,
                                                       cameraYaw_, cameraPitch_, hit);
    if (picked && hit.entityKind == WorldEntityKind::Tower && hit.instanceIndex >= 0 &&
        static_cast<std::size_t>(hit.instanceIndex) < placedTowers_.size()) {
        selectedTowerRuntimeId_ = placedTowers_[static_cast<std::size_t>(hit.instanceIndex)].runtimeId;
        selectedEnemyRuntimeId_ = 0;
    } else if (picked && hit.entityKind == WorldEntityKind::Enemy && hit.instanceIndex >= 0 &&
               static_cast<std::size_t>(hit.instanceIndex) < activeEnemies_.size()) {
        selectedEnemyRuntimeId_ = activeEnemies_[static_cast<std::size_t>(hit.instanceIndex)].runtimeId;
        selectedTowerRuntimeId_ = 0;
    } else {
        selectedTowerRuntimeId_ = 0;
        selectedEnemyRuntimeId_ = 0;
    }
    syncTowerInstances();
}


bool PlayLevelScene::submitCommand(multiplayer::PlayerCommandPayload payload) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = localPlayerId_;
    command.sequence = nextCommandSequence_++;
    command.payload = std::move(payload);
    const auto bytes = multiplayer::MatchProtocolAdapter::serializePlayerCommand(command);
    if (!bytes) {
        return false;
    }
    if (session_.isClient()) {
        return remoteJoinAccepted_ && session_.client().sendCommand(*bytes);
    }

    bool accepted = false;
    localMatchHost_.processCommand(
        *bytes, matchSimulation_.currentTick(),
        [this, &accepted](const multiplayer::PlayerCommandRequest& received) {
            const auto rejection = dispatchAuthoritativeCommand(received);
            accepted = !rejection.has_value();
            return rejection;
        });
    return accepted;
}

std::optional<multiplayer::CommandRejectionReason>
PlayLevelScene::dispatchAuthoritativeCommand(const multiplayer::PlayerCommandRequest& command) {
    if (const auto* place = std::get_if<multiplayer::PlaceTowerCommand>(&command.payload)) {
        if (gameplayState_.matchStatus != MatchStatus::Running) {
            return multiplayer::CommandRejectionReason::MatchNotRunning;
        }
        const TowerArchetype* tower = towerLoadController_->findArchetype(place->towerArchetypeId);
        if (!tower) {
            return multiplayer::CommandRejectionReason::UnknownTowerArchetype;
        }
        const glm::vec3 position{place->requestedPosition.x, place->requestedPosition.y, place->requestedPosition.z};
        const TowerPlacementRules::Context placementContext{gameplayState_, placedTowers_, worldRenderer_.get(),
                                                            worldRenderer_->placementRegions(), 30.0f, 2.0f};
        PlacementTerrainSample sample;
        sample.hit = true;
        sample.worldPosition = position;
        sample.surfaceNormal = {0.0f, 1.0f, 0.0f};
        if (!TowerPlacementRules::validatePlacement(placementContext, *tower, position, 8, sample,
                                                    matchSimulation_.playerBalance(command.playerId))
                 .empty()) {
            return multiplayer::CommandRejectionReason::InvalidPlacement;
        }
        if (!matchSimulation_.debitPlayer(command.playerId, static_cast<float>(tower->cost))) {
            return multiplayer::CommandRejectionReason::InsufficientFunds;
        }

        playlevel::PlacedTower placed;
        placed.towerId = tower->id;
        placed.position = position;
        placed.towerPrototypeIndex = towerLoadController_->templatePrototypeIndex(tower->id);
        placed.projectilePrototypeIndex = towerLoadController_->projectileTemplatePrototypeIndex(tower->id);
        placed.renderScale = tower->renderScale;
        placed.facingYawOffsetDegrees = tower->facingYawOffsetDegrees;
        placed.projectileFacingYawOffsetDegrees = tower->projectileFacingYawOffsetDegrees;
        placed.projectileRenderScale = tower->projectileRenderScale;
        placed.attackDamage = tower->attackDamage;
        placed.armorPiercing = tower->armorPiercing;
        placed.attackRange = tower->attackRange;
        placed.attackIntervalSeconds = 1.0f / std::max(0.01f, tower->attackSpeed);
        placed.projectileSpeed = tower->projectileSpeed;
        placed.splashRadius = tower->splashRadius;
        placed.chainRange = tower->chainRange;
        placed.ricochetRange = tower->ricochetRange;
        placed.burnDamagePerSecond = tower->burnDamagePerSecond;
        placed.burnDuration = tower->burnDuration;
        placed.slowAmount = tower->slowAmount;
        placed.slowDuration = tower->slowDuration;
        placed.freezeChance = tower->freezeChance;
        placed.freezeDuration = tower->freezeDuration;
        placed.critChance = tower->critChance;
        placed.critDamageMul = tower->critDamageMul;
        placed.projectileCount = std::max(1, tower->projectileCount);
        placed.chainTargetCount = std::max(1, tower->chainTargetCount);
        placed.ricochetCount = std::max(0, tower->ricochetCount);
        placed.cost = tower->cost;
        placed.damageType = tower->damageType;
        placed.targetingMode = tower->defaultTargetingMode;
        placed.runtimeId = matchSimulation_.nextTowerRuntimeId()++;
        placed.ownerPlayerId = command.playerId;
        placedTowers_.push_back(std::move(placed));
        if (command.playerId == localPlayerId_) {
            gameplayState_.playerMoney = matchSimulation_.playerBalance(localPlayerId_);
        }
        syncTowerInstances();
        return std::nullopt;
    }
    if (const auto* upgrade = std::get_if<multiplayer::UpgradeTowerCommand>(&command.payload)) {
        const auto placed = std::find_if(placedTowers_.begin(), placedTowers_.end(), [upgrade](const auto& tower) {
            return tower.runtimeId == upgrade->towerRuntimeId;
        });
        if (placed == placedTowers_.end()) return multiplayer::CommandRejectionReason::UnknownTower;
        if (placed->ownerPlayerId != command.playerId) return multiplayer::CommandRejectionReason::TowerNotOwnedByPlayer;
        const TowerArchetype* archetype = towerLoadController_->findArchetype(placed->towerId);
        if (!archetype) return multiplayer::CommandRejectionReason::UnknownTowerArchetype;
        const auto node = std::find_if(archetype->upgradeNodes.begin(), archetype->upgradeNodes.end(), [upgrade](const auto& item) {
            return item.id == upgrade->upgradeNodeId;
        });
        if (node == archetype->upgradeNodes.end()) return multiplayer::CommandRejectionReason::UpgradeUnavailable;
        const int currentLevel = static_cast<int>(std::count(placed->unlockedUpgradeNodeIds.begin(),
                                                             placed->unlockedUpgradeNodeIds.end(), node->id));
        if (currentLevel >= static_cast<int>(node->upgradeLevels.size()) ||
            static_cast<int>(placed->unlockedUpgradeNodeIds.size()) < node->minUpgradesRequired) {
            return multiplayer::CommandRejectionReason::UpgradeUnavailable;
        }
        for (const auto& required : node->requiredNodeIds) {
            if (std::find(placed->unlockedUpgradeNodeIds.begin(), placed->unlockedUpgradeNodeIds.end(), required) ==
                placed->unlockedUpgradeNodeIds.end()) return multiplayer::CommandRejectionReason::UpgradeUnavailable;
        }
        for (const auto& excluded : node->excludes) {
            if (std::find(placed->unlockedUpgradeNodeIds.begin(), placed->unlockedUpgradeNodeIds.end(), excluded) !=
                placed->unlockedUpgradeNodeIds.end()) return multiplayer::CommandRejectionReason::UpgradeUnavailable;
        }
        const auto& level = node->upgradeLevels[static_cast<std::size_t>(currentLevel)];
        if (!matchSimulation_.debitPlayer(command.playerId, static_cast<float>(level.cost))) {
            return multiplayer::CommandRejectionReason::InsufficientFunds;
        }
        const auto& effects = level.effects;
        placed->attackDamage = (placed->attackDamage + effects.attackDamageAdd) * effects.attackDamageMul;
        placed->attackRange = (placed->attackRange + effects.attackRangeAdd) * effects.attackRangeMul;
        const float attackSpeed = (1.0f / std::max(0.01f, placed->attackIntervalSeconds) + effects.attackSpeedAdd) *
                                  effects.attackSpeedMul;
        placed->attackIntervalSeconds = 1.0f / std::max(0.01f, attackSpeed);
        placed->projectileSpeed = (placed->projectileSpeed + effects.projectileSpeedAdd) * effects.projectileSpeedMul;
        placed->splashRadius = std::max(0.0f, (placed->splashRadius + effects.splashRadiusAdd) * effects.splashRadiusMul);
        placed->chainRange = std::max(0.0f, (placed->chainRange + effects.chainRangeAdd) * effects.chainRangeMul);
        placed->ricochetRange = std::max(0.0f, (placed->ricochetRange + effects.ricochetRangeAdd) * effects.ricochetRangeMul);
        placed->burnDamagePerSecond = std::max(0.0f, placed->burnDamagePerSecond + effects.burnDamagePerSecondAdd);
        placed->burnDuration = std::max(0.0f, placed->burnDuration + effects.burnDurationAdd);
        placed->slowAmount = std::clamp(placed->slowAmount + effects.slowAmountAdd, 0.0f, 1.0f);
        placed->slowDuration = std::max(0.0f, placed->slowDuration + effects.slowDurationAdd);
        placed->freezeChance = std::clamp(placed->freezeChance + effects.freezeChanceAdd, 0.0f, 1.0f);
        placed->freezeDuration = std::max(0.0f, placed->freezeDuration + effects.freezeDurationAdd);
        placed->critChance = std::clamp(placed->critChance + effects.critChanceAdd, 0.0f, 1.0f);
        placed->critDamageMul = std::max(1.0f, placed->critDamageMul + effects.critDamageMulAdd);
        placed->projectileCount = std::max(1, placed->projectileCount + effects.projectileCountAdd);
        placed->chainTargetCount = std::max(1, placed->chainTargetCount + effects.chainTargetCountAdd);
        placed->ricochetCount = std::max(0, placed->ricochetCount + effects.ricochetCountAdd);
        placed->unlockedUpgradeNodeIds.push_back(node->id);
        if (level.towerPrototypeOverrideIndex >= 0) {
            placed->towerPrototypeIndex = level.towerPrototypeOverrideIndex;
        } else if (node->towerPrototypeOverrideIndex >= 0) {
            placed->towerPrototypeIndex = node->towerPrototypeOverrideIndex;
        }
        if (level.projectilePrototypeOverrideIndex >= 0) {
            placed->projectilePrototypeIndex = level.projectilePrototypeOverrideIndex;
        } else if (node->projectilePrototypeOverrideIndex >= 0) {
            placed->projectilePrototypeIndex = node->projectilePrototypeOverrideIndex;
        }
        if (level.renderScaleOverride) {
            placed->renderScale = std::max(0.01f, *level.renderScaleOverride);
        } else if (node->renderScaleOverride) {
            placed->renderScale = std::max(0.01f, *node->renderScaleOverride);
        }
        if (level.facingYawOffsetDegreesOverride) {
            placed->facingYawOffsetDegrees = *level.facingYawOffsetDegreesOverride;
        } else if (node->facingYawOffsetDegreesOverride) {
            placed->facingYawOffsetDegrees = *node->facingYawOffsetDegreesOverride;
        }
        if (level.projectileFacingYawOffsetDegreesOverride) {
            placed->projectileFacingYawOffsetDegrees = *level.projectileFacingYawOffsetDegreesOverride;
        } else if (node->projectileFacingYawOffsetDegreesOverride) {
            placed->projectileFacingYawOffsetDegrees = *node->projectileFacingYawOffsetDegreesOverride;
        }
        if (level.projectileRenderScaleOverride) {
            placed->projectileRenderScale = std::max(0.01f, *level.projectileRenderScaleOverride);
        } else if (node->projectileRenderScaleOverride) {
            placed->projectileRenderScale = std::max(0.01f, *node->projectileRenderScaleOverride);
        }
        if (command.playerId == localPlayerId_) gameplayState_.playerMoney = matchSimulation_.playerBalance(localPlayerId_);
        syncTowerInstances();
        return std::nullopt;
    }
    if (const auto* sell = std::get_if<multiplayer::SellTowerCommand>(&command.payload)) {
        const auto placed = std::find_if(placedTowers_.begin(), placedTowers_.end(), [sell](const auto& tower) {
            return tower.runtimeId == sell->towerRuntimeId;
        });
        if (placed == placedTowers_.end()) return multiplayer::CommandRejectionReason::UnknownTower;
        if (placed->ownerPlayerId != command.playerId) return multiplayer::CommandRejectionReason::TowerNotOwnedByPlayer;
        const TowerArchetype* archetype = towerLoadController_->findArchetype(placed->towerId);
        const int refund = towerSellValue(*placed, archetype);
        if (!matchSimulation_.creditPlayer(command.playerId, static_cast<float>(refund))) {
            return multiplayer::CommandRejectionReason::InvalidPayload;
        }
        placedTowers_.erase(placed);
        if (command.playerId == localPlayerId_) gameplayState_.playerMoney = matchSimulation_.playerBalance(localPlayerId_);
        selectedTowerRuntimeId_ = 0;
        selectedEnemyRuntimeId_ = 0;
        syncTowerInstances();
        return std::nullopt;
    }
    return multiplayer::CommandRejectionReason::InvalidPayload;
}

void PlayLevelScene::processIncomingJoinRequests() {
    if (!session_.isHost()) {
        return;
    }
    auto& transport = session_.hostTransport();
    for (auto& request : transport.drainJoinRequests()) {
        const auto outcome = localMatchHost_.processJoinRequest(request.payload, "", matchSimulation_.currentTick());
        if (!outcome) {
            transport.disconnectPeer(request.peerId);
            continue;
        }
        transport.sendJoinResult(request.peerId, outcome->serializedResult);
        if (!outcome->acceptedPlayerId) {
            transport.disconnectPeerAfterWrites(request.peerId);
            continue;
        }
        const multiplayer::PlayerId playerId = *outcome->acceptedPlayerId;
        if (matchSimulation_.registerPlayer(playerId, 250.0f) && localMatchHost_.registerPlayer(playerId) &&
            transport.markPeerJoined(request.peerId)) {
            remotePlayerByPeer_[request.peerId] = playerId;
        }
    }
}

void PlayLevelScene::processRemoteJoinResult() {
    if (!session_.isClient() || !remoteJoinSent_ || remoteJoinAccepted_) {
        return;
    }
    const auto payload = session_.client().consumeJoinResult();
    if (!payload) {
        return;
    }
    const auto result = multiplayer::MatchProtocolAdapter::decodeJoinMatchResult(*payload);
    if (result) {
        if (const auto* accepted = std::get_if<multiplayer::JoinMatchAccepted>(&*result)) {
            localPlayerId_ = accepted->playerId;
            remoteJoinAccepted_ = true;
        }
    }
}

void PlayLevelScene::drainRemoteCommands(multiplayer::SimulationTick tick) {
    if (!session_.isHost()) {
        return;
    }
    for (auto& received : session_.hostTransport().drainClientCommands()) {
        const auto player = remotePlayerByPeer_.find(received.peerId);
        if (player == remotePlayerByPeer_.end()) {
            continue;
        }
        const auto result = localMatchHost_.processCommand(
            received.payload, tick,
            [this, expectedPlayer = player->second](const multiplayer::PlayerCommandRequest& command) {
                if (command.playerId != expectedPlayer) {
                    return std::optional{multiplayer::CommandRejectionReason::UnknownPlayer};
                }
                return dispatchAuthoritativeCommand(command);
            });
        if (result) {
            session_.hostTransport().sendCommandResult(received.peerId, *result);
        }
    }
}

void PlayLevelScene::publishSnapshot(multiplayer::SimulationTick tick) {
    const bool terminal = gameplayState_.matchStatus == MatchStatus::Victory ||
                          gameplayState_.matchStatus == MatchStatus::Defeat;
    if (!session_.isHost() || remotePlayerByPeer_.empty() || (!terminal && tick % kSnapshotIntervalTicks != 0)) {
        return;
    }
    if (const auto snapshot = multiplayer::MatchSnapshotBuilder::serialize(matchSimulation_)) {
        session_.hostTransport().publishSnapshot(*snapshot);
    }
}

void PlayLevelScene::updateMatchSimulation(float dt) {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }
    if (session_.isClient() && !remoteJoinSent_) {
        multiplayer::JoinMatchRequest request;
        request.playerDisplayName = "Player";
        if (const auto bytes = multiplayer::MatchProtocolAdapter::serializeJoinMatchRequest(request)) {
            remoteJoinSent_ = session_.client().sendJoinRequest(*bytes);
        }
    }
    processIncomingJoinRequests();
    processRemoteJoinResult();

    if (session_.isClient()) {
        if (const auto bytes = session_.client().consumeLatestSnapshot()) {
            if (const auto snapshot = multiplayer::MatchSnapshotBuilder::deserialize(*bytes)) {
                applyRemoteSnapshot(*snapshot);
                refreshHud();
            }
        }
        for (playlevel::ActiveEnemy& enemy : activeEnemies_) {
            if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Dying) {
                enemy.deathElapsedSeconds += dt;
            } else if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive) {
                enemy.walkAnimElapsedSeconds += dt;
            }
        }
        session_.client().drainCommandResults();
        syncTowerInstances();
        syncEnemyInstances();
        return;
    }
    const bool terminalState = gameplayState_.matchStatus == MatchStatus::Victory ||
                              gameplayState_.matchStatus == MatchStatus::Defeat;
    if (!terminalState && gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }

    if (gameplayState_.matchStatus == MatchStatus::Running) {
        matchSimulation_.advance(dt, [this](multiplayer::SimulationTick tick, float tickSeconds) {
            drainRemoteCommands(tick);
            updateWaveSimulation(tickSeconds);
            publishSnapshot(tick);
        });
    } else {
        const auto countAliveEnemies = [this]() {
            return static_cast<int>(std::count_if(activeEnemies_.begin(), activeEnemies_.end(), [](const auto& enemy) {
                return enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive;
            }));
        };

        matchSimulation_.combatController().advanceDyingEnemies(
            dt,
            [this](const playlevel::ActiveEnemy& enemy) {
                const int clipIndex = worldRenderer_->templateAnimationClipIndexByName(
                    enemy.deathClipName, enemy.templatePrototypeIndex);
                return worldRenderer_->templateAnimationClipDurationSeconds(clipIndex, enemy.templatePrototypeIndex);
            },
            activeEnemies_);
        gameplayState_.enemiesAlive = countAliveEnemies();
    }
    syncTowerInstances();
    syncEnemyInstances();
}

float PlayLevelScene::routeLength() const {
    if (!worldRenderer_) return 0.0f;
    const auto& points = worldRenderer_->routePoints();
    float length = 0.0f;
    for (std::size_t index = 1; index < points.size(); ++index) length += glm::distance(points[index - 1], points[index]);
    return length;
}

glm::vec3 PlayLevelScene::sampleRoutePosition(float distance) const {
    const auto& points = worldRenderer_->routePoints();
    if (points.empty()) return {};
    for (std::size_t index = 1; index < points.size(); ++index) {
        const float segmentLength = glm::distance(points[index - 1], points[index]);
        if (distance <= segmentLength) {
            return glm::mix(points[index - 1], points[index], segmentLength > 0.0f ? distance / segmentLength : 0.0f);
        }
        distance -= segmentLength;
    }
    return points.back();
}

float PlayLevelScene::sampleRouteYaw(float distance) const {
    const float totalLength = routeLength();
    const glm::vec3 position = sampleRoutePosition(distance);
    glm::vec3 direction = sampleRoutePosition(std::min(totalLength, distance + 0.2f)) - position;
    direction.y = 0.0f;
    if (glm::dot(direction, direction) <= 1e-6f) {
        return 0.0f;
    }
    direction = glm::normalize(direction);
    return std::atan2(direction.x, direction.z);
}

void PlayLevelScene::updateWaveSimulation(float dt) {
    auto& wave = matchSimulation_.waveController();
    const auto countAliveEnemies = [this]() {
        return static_cast<int>(std::count_if(activeEnemies_.begin(), activeEnemies_.end(), [](const auto& enemy) {
            return enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive;
        }));
    };
    wave.updateWaveSpawning(gameplayState_, dt, countAliveEnemies(), [this](const std::string& id) {
        const EnemyArchetype* archetype = enemyLoadController_->findArchetype(id);
        int prototypeIndex = 0;
        if (archetype) {
            const auto found = std::find(launchConfig_.animatedTemplateModelPaths.begin(),
                                         launchConfig_.animatedTemplateModelPaths.end(), archetype->modelPath);
            if (found != launchConfig_.animatedTemplateModelPaths.end()) {
                prototypeIndex = static_cast<int>(std::distance(launchConfig_.animatedTemplateModelPaths.begin(), found));
            }
        }
        playlevel::ActiveEnemy enemy =
            EnemySpawnFactory::create(id, archetype, matchSimulation_.nextEnemyRuntimeId()++, prototypeIndex);
        if (gameplayTuningEnabled_) {
            enemy.health = std::max(1.0f, enemy.health * gameplayTuning_.enemyHealthMultiplier);
            enemy.maxHealth = std::max(enemy.health, enemy.maxHealth * gameplayTuning_.enemyHealthMultiplier);
            enemy.moveSpeed = std::max(0.01f, enemy.moveSpeed * gameplayTuning_.enemySpeedMultiplier);
            enemy.rewardMoney = std::max(0.0f, enemy.rewardMoney * gameplayTuning_.enemyRewardMultiplier);
            enemy.baseDamage = std::max(0.0f, enemy.baseDamage * gameplayTuning_.enemyBaseDamageMultiplier);
        }
        activeEnemies_.push_back(std::move(enemy));
    });

    PlayLevelCombatController& combat = matchSimulation_.combatController();
    combat.advanceEnemies(dt, routeLength(), activeEnemies_, [this](float damage) {
        const float scaledDamage = gameplayTuningEnabled_ ? damage * gameplayTuning_.enemyBaseDamageMultiplier : damage;
        gameplayState_.baseHealth = std::max(0.0f, gameplayState_.baseHealth - scaledDamage);
    });
    combat.updateEnemyStatusEffects(dt, placedTowers_, activeEnemies_);
    combat.updateTowerAttacks(
        dt, [this](float distance) { return sampleRoutePosition(distance); }, placedTowers_, activeEnemies_,
        matchSimulation_.activeProjectiles(), matchSimulation_.nextProjectileRuntimeId());
    combat.updateProjectiles(
        dt, [this](float distance) { return sampleRoutePosition(distance); }, placedTowers_, activeEnemies_,
        matchSimulation_.activeProjectiles());
    combat.collectDefeatedEnemies(
        activeEnemies_,
        [this](float reward, multiplayer::TowerRuntimeId towerRuntimeId) {
            const auto tower = std::find_if(placedTowers_.begin(), placedTowers_.end(), [towerRuntimeId](const auto& item) {
                return item.runtimeId == towerRuntimeId;
            });
            const multiplayer::PlayerId playerId = tower != placedTowers_.end() ? tower->ownerPlayerId : localPlayerId_;
            matchSimulation_.creditPlayer(playerId, reward);
            if (playerId == localPlayerId_) gameplayState_.playerMoney = matchSimulation_.playerBalance(localPlayerId_);
        },
        [this]() { ++gameplayState_.enemiesDefeated; });
    combat.advanceDyingEnemies(
        dt,
        [this](const playlevel::ActiveEnemy& enemy) {
            const int clipIndex = worldRenderer_->templateAnimationClipIndexByName(
                enemy.deathClipName, enemy.templatePrototypeIndex);
            return worldRenderer_->templateAnimationClipDurationSeconds(clipIndex, enemy.templatePrototypeIndex);
        },
        activeEnemies_);
    gameplayState_.enemiesAlive = countAliveEnemies();
    if (gameplayState_.baseHealth <= 0.0f) {
        gameplayState_.matchStatus = MatchStatus::Defeat;
    } else if (!gameplayState_.waveInProgress && !gameplayState_.waveCountdownActive &&
               gameplayState_.currentWave > static_cast<int>(wave.waveCount()) && gameplayState_.enemiesAlive == 0) {
        gameplayState_.matchStatus = MatchStatus::Victory;
    }
    if (gameplayState_.matchStatus == MatchStatus::Victory || gameplayState_.matchStatus == MatchStatus::Defeat) {
        gameplayState_.waveInProgress = false;
        gameplayState_.waveCountdownActive = false;
        selectedTowerSlot_ = -1;
        selectedTowerRuntimeId_ = 0;
        selectedEnemyRuntimeId_ = 0;
        placementSample_ = {};
        towerPlacementPreviewResolver_.reset();
    }
}

void PlayLevelScene::restartMatch() {
    if (session_.isClient() || !worldRenderer_ || !worldRenderer_->isLoaded()) {
        return;
    }

    PlayLevelUiStateInput input;
    input.phase = PlayLevelUiPhase::Running;
    input.levelName = launchConfig_.displayName;
    input.worldReady = true;
    snapshot_ = buildPlayLevelUiSnapshot(input);

    matchSimulation_.reset();
    localMatchHost_ = multiplayer::LocalMatchHost{};
    matchSimulation_.registerPlayer(localPlayerId_, gameplayState_.playerMoney);
    localMatchHost_.registerPlayer(localPlayerId_);
    for (const auto& [peerId, playerId] : remotePlayerByPeer_) {
        (void)peerId;
        matchSimulation_.registerPlayer(playerId, gameplayState_.playerMoney);
        localMatchHost_.registerPlayer(playerId);
    }
    loadWaveDefinitions();
    selectedTowerSlot_ = -1;
    selectedTowerRuntimeId_ = 0;
    selectedEnemyRuntimeId_ = 0;
    placementSample_ = {};
    towerPlacementPreviewResolver_.reset();
    placementReason_.clear();
    gameplayState_.matchStatus = MatchStatus::Running;
    matchSimulation_.waveController().beginWaveCountdown(
        gameplayState_, true, worldRenderer_->hasAnimatedEntityTemplate(), worldRenderer_->routePoints().size() >= 2);
    publishSnapshot(matchSimulation_.currentTick());
    syncTowerInstances();
    syncEnemyInstances();
    refreshHud();
}

void PlayLevelScene::applyRemoteSnapshot(const multiplayer::DecodedMatchSnapshot& snapshot) {
    gameplayState_.matchStatus = snapshot.matchStatus;
    gameplayState_.baseHealth = snapshot.baseHealth;
    gameplayState_.currentWave = snapshot.currentWave;
    gameplayState_.waveInProgress = snapshot.waveInProgress;
    gameplayState_.waveCountdownActive = snapshot.waveCountdownActive;
    gameplayState_.waveCountdownRemainingSeconds = snapshot.waveCountdownRemainingSeconds;
    gameplayState_.waveRoundRemainingSeconds = snapshot.waveRoundRemainingSeconds;
    gameplayState_.waveRoundDurationSeconds = snapshot.waveRoundDurationSeconds;
    for (const auto& player : snapshot.players) if (player.playerId == localPlayerId_) gameplayState_.playerMoney = player.money;

    placedTowers_.clear();
    for (const auto& remote : snapshot.towers) {
        const TowerArchetype* tower = towerLoadController_->findArchetype(remote.towerArchetypeId);
        if (!tower) continue;
        playlevel::PlacedTower placed;
        placed.towerId = tower->id;
        placed.position = {remote.positionX, remote.positionY, remote.positionZ};
        placed.towerPrototypeIndex = towerLoadController_->templatePrototypeIndex(tower->id);
        placed.projectilePrototypeIndex = towerLoadController_->projectileTemplatePrototypeIndex(tower->id);
        placed.renderScale = tower->renderScale;
        placed.facingYawOffsetDegrees = tower->facingYawOffsetDegrees;
        placed.projectileFacingYawOffsetDegrees = tower->projectileFacingYawOffsetDegrees;
        placed.projectileRenderScale = tower->projectileRenderScale;
        placed.attackDamage = tower->attackDamage;
        placed.armorPiercing = tower->armorPiercing;
        placed.attackRange = tower->attackRange;
        placed.attackIntervalSeconds = 1.0f / std::max(0.01f, tower->attackSpeed);
        placed.projectileSpeed = tower->projectileSpeed;
        placed.splashRadius = tower->splashRadius;
        placed.chainRange = tower->chainRange;
        placed.ricochetRange = tower->ricochetRange;
        placed.burnDamagePerSecond = tower->burnDamagePerSecond;
        placed.burnDuration = tower->burnDuration;
        placed.slowAmount = tower->slowAmount;
        placed.slowDuration = tower->slowDuration;
        placed.freezeChance = tower->freezeChance;
        placed.freezeDuration = tower->freezeDuration;
        placed.critChance = tower->critChance;
        placed.critDamageMul = tower->critDamageMul;
        placed.projectileCount = std::max(1, tower->projectileCount);
        placed.chainTargetCount = std::max(1, tower->chainTargetCount);
        placed.ricochetCount = std::max(0, tower->ricochetCount);
        placed.cost = tower->cost;
        placed.damageType = tower->damageType;
        placed.targetingMode = static_cast<playlevel::TowerTargetingMode>(remote.targetingMode);
        placed.runtimeId = remote.runtimeId;
        placed.ownerPlayerId = remote.ownerPlayerId;
        placed.unlockedUpgradeNodeIds = remote.unlockedUpgradeNodeIds;
        std::unordered_map<std::string, int> appliedLevels;
        for (const auto& nodeId : placed.unlockedUpgradeNodeIds) {
            const auto node = std::find_if(tower->upgradeNodes.begin(), tower->upgradeNodes.end(), [&nodeId](const auto& item) {
                return item.id == nodeId;
            });
            if (node == tower->upgradeNodes.end()) continue;
            const int levelIndex = appliedLevels[nodeId]++;
            if (levelIndex >= static_cast<int>(node->upgradeLevels.size())) continue;
            const auto& effects = node->upgradeLevels[static_cast<std::size_t>(levelIndex)].effects;
            placed.attackDamage = (placed.attackDamage + effects.attackDamageAdd) * effects.attackDamageMul;
            placed.attackRange = (placed.attackRange + effects.attackRangeAdd) * effects.attackRangeMul;
            const float attackSpeed = (1.0f / std::max(0.01f, placed.attackIntervalSeconds) + effects.attackSpeedAdd) *
                                      effects.attackSpeedMul;
            placed.attackIntervalSeconds = 1.0f / std::max(0.01f, attackSpeed);
            placed.projectileSpeed = (placed.projectileSpeed + effects.projectileSpeedAdd) * effects.projectileSpeedMul;
            placed.splashRadius = std::max(0.0f, (placed.splashRadius + effects.splashRadiusAdd) * effects.splashRadiusMul);
            placed.chainRange = std::max(0.0f, (placed.chainRange + effects.chainRangeAdd) * effects.chainRangeMul);
            placed.ricochetRange = std::max(0.0f, (placed.ricochetRange + effects.ricochetRangeAdd) * effects.ricochetRangeMul);
            placed.burnDamagePerSecond = std::max(0.0f, placed.burnDamagePerSecond + effects.burnDamagePerSecondAdd);
            placed.burnDuration = std::max(0.0f, placed.burnDuration + effects.burnDurationAdd);
            placed.slowAmount = std::clamp(placed.slowAmount + effects.slowAmountAdd, 0.0f, 1.0f);
            placed.slowDuration = std::max(0.0f, placed.slowDuration + effects.slowDurationAdd);
            placed.freezeChance = std::clamp(placed.freezeChance + effects.freezeChanceAdd, 0.0f, 1.0f);
            placed.freezeDuration = std::max(0.0f, placed.freezeDuration + effects.freezeDurationAdd);
            placed.critChance = std::clamp(placed.critChance + effects.critChanceAdd, 0.0f, 1.0f);
            placed.critDamageMul = std::max(1.0f, placed.critDamageMul + effects.critDamageMulAdd);
            placed.projectileCount = std::max(1, placed.projectileCount + effects.projectileCountAdd);
            placed.chainTargetCount = std::max(1, placed.chainTargetCount + effects.chainTargetCountAdd);
            placed.ricochetCount = std::max(0, placed.ricochetCount + effects.ricochetCountAdd);
            const auto& level = node->upgradeLevels[static_cast<std::size_t>(levelIndex)];
            if (level.towerPrototypeOverrideIndex >= 0) {
                placed.towerPrototypeIndex = level.towerPrototypeOverrideIndex;
            } else if (node->towerPrototypeOverrideIndex >= 0) {
                placed.towerPrototypeIndex = node->towerPrototypeOverrideIndex;
            }
            if (level.projectilePrototypeOverrideIndex >= 0) {
                placed.projectilePrototypeIndex = level.projectilePrototypeOverrideIndex;
            } else if (node->projectilePrototypeOverrideIndex >= 0) {
                placed.projectilePrototypeIndex = node->projectilePrototypeOverrideIndex;
            }
            if (level.renderScaleOverride) {
                placed.renderScale = std::max(0.01f, *level.renderScaleOverride);
            } else if (node->renderScaleOverride) {
                placed.renderScale = std::max(0.01f, *node->renderScaleOverride);
            }
            if (level.facingYawOffsetDegreesOverride) {
                placed.facingYawOffsetDegrees = *level.facingYawOffsetDegreesOverride;
            } else if (node->facingYawOffsetDegreesOverride) {
                placed.facingYawOffsetDegrees = *node->facingYawOffsetDegreesOverride;
            }
            if (level.projectileFacingYawOffsetDegreesOverride) {
                placed.projectileFacingYawOffsetDegrees = *level.projectileFacingYawOffsetDegreesOverride;
            } else if (node->projectileFacingYawOffsetDegreesOverride) {
                placed.projectileFacingYawOffsetDegrees = *node->projectileFacingYawOffsetDegreesOverride;
            }
            if (level.projectileRenderScaleOverride) {
                placed.projectileRenderScale = std::max(0.01f, *level.projectileRenderScaleOverride);
            } else if (node->projectileRenderScaleOverride) {
                placed.projectileRenderScale = std::max(0.01f, *node->projectileRenderScaleOverride);
            }
        }
        placedTowers_.push_back(std::move(placed));
    }

    std::unordered_map<std::uint64_t, std::pair<float, float>> animationTimes;
    animationTimes.reserve(activeEnemies_.size());
    for (const auto& enemy : activeEnemies_) {
        animationTimes.emplace(enemy.runtimeId,
                               std::pair{enemy.walkAnimElapsedSeconds, enemy.deathElapsedSeconds});
    }
    activeEnemies_.clear();
    for (const auto& remote : snapshot.enemies) {
        const EnemyArchetype* archetype = enemyLoadController_->findArchetype(remote.enemyArchetypeId);
        int prototypeIndex = 0;
        if (archetype) {
            const auto found = std::find(launchConfig_.animatedTemplateModelPaths.begin(),
                                         launchConfig_.animatedTemplateModelPaths.end(), archetype->modelPath);
            if (found != launchConfig_.animatedTemplateModelPaths.end()) {
                prototypeIndex = static_cast<int>(std::distance(launchConfig_.animatedTemplateModelPaths.begin(), found));
            }
        }
        auto enemy = EnemySpawnFactory::create(remote.enemyArchetypeId, archetype, remote.runtimeId, prototypeIndex);
        enemy.distanceAlongPath = remote.distanceAlongPath;
        enemy.health = remote.health;
        enemy.shield = remote.shield;
        enemy.lifecycleState = static_cast<playlevel::EnemyLifecycleState>(remote.lifecycleState);
        if (const auto previous = animationTimes.find(enemy.runtimeId); previous != animationTimes.end()) {
            enemy.walkAnimElapsedSeconds = previous->second.first;
            enemy.deathElapsedSeconds = previous->second.second;
        }
        activeEnemies_.push_back(std::move(enemy));
    }
    auto& activeProjectiles = matchSimulation_.activeProjectiles();
    activeProjectiles.clear();
    activeProjectiles.reserve(snapshot.projectiles.size());
    for (const auto& remote : snapshot.projectiles) {
        playlevel::ActiveProjectile projectile;
        projectile.runtimeId = remote.runtimeId;
        projectile.towerId = remote.towerArchetypeId;
        projectile.position = {remote.positionX, remote.positionY, remote.positionZ};
        projectile.targetEnemyRuntimeId = remote.targetEnemyRuntimeId;
        if (const TowerArchetype* tower = towerLoadController_->findArchetype(projectile.towerId)) {
            projectile.prototypeIndex = towerLoadController_->projectileTemplatePrototypeIndex(tower->id);
        }
        activeProjectiles.push_back(std::move(projectile));
    }
    gameplayState_.enemiesAlive = static_cast<int>(activeEnemies_.size());
}

void PlayLevelScene::syncEnemyInstances() {
    if (!worldRenderer_ || !worldRenderer_->isLoaded()) return;
    std::vector<AnimatedEntityInstanceSet::Instance> instances;
    instances.reserve(activeEnemies_.size());
    for (const auto& enemy : activeEnemies_) {
        const glm::vec3 position = sampleRoutePosition(enemy.distanceAlongPath);
        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = glm::translate(glm::mat4{1.0f}, position) *
                             glm::rotate(glm::mat4{1.0f},
                                         sampleRouteYaw(enemy.distanceAlongPath) +
                                             glm::radians(enemy.facingYawOffsetDegrees),
                                         glm::vec3(0.0f, 1.0f, 0.0f)) *
                             glm::scale(glm::mat4{1.0f}, glm::vec3(enemy.renderScale));
        instance.prototypeIndex = enemy.templatePrototypeIndex;
        instance.debugGroup = "enemy:" + std::to_string(enemy.runtimeId);
        instance.debugLabel = enemy.enemyId;
        const bool dying = enemy.lifecycleState == playlevel::EnemyLifecycleState::Dying;
        const std::string& clipName = dying ? enemy.deathClipName : enemy.walkingClipName;
        instance.animationClipIndexOverride =
            worldRenderer_->templateAnimationClipIndexByName(clipName, enemy.templatePrototypeIndex);
        instance.animationClipTimeSecondsOverride = dying ? enemy.deathElapsedSeconds : enemy.walkAnimElapsedSeconds;
        instances.push_back(std::move(instance));
    }
    worldRenderer_->setAnimatedEntityInstances(std::move(instances));
}

void PlayLevelScene::syncTowerInstances() {
    if (!worldRenderer_ || !worldRenderer_->isLoaded() || !towerLoadController_) {
        return;
    }
    std::vector<GroundCircle> groundCircles;
    std::vector<AnimatedEntityInstanceSet::Instance> instances;
    instances.reserve(placedTowers_.size() + 1);
    for (std::size_t index = 0; index < placedTowers_.size(); ++index) {
        const playlevel::PlacedTower& placed = placedTowers_[index];
        const TowerArchetype* tower = towerLoadController_->findArchetype(placed.towerId);
        if (!tower || placed.towerPrototypeIndex < 0) {
            continue;
        }
        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = buildTowerTransform(placed.position, placed.facingYawOffsetDegrees, placed.renderScale);
        instance.prototypeIndex = placed.towerPrototypeIndex;
        instance.debugGroup = "placed-tower:" + std::to_string(index);
        instance.debugLabel = tower->displayName;
        instances.push_back(std::move(instance));
        if (placed.runtimeId == selectedTowerRuntimeId_) {
            const bool isOwnedByLocalPlayer = placed.ownerPlayerId == localPlayerId_;
            groundCircles.push_back({placed.position + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                     std::max(0.01f, placed.attackRange),
                                     isOwnedByLocalPlayer ? kPlacementRangeFill : kOtherPlayerRangeFill,
                                     isOwnedByLocalPlayer ? kPlacementRangeOutline : kOtherPlayerRangeOutline});
        } else if (placed.runtimeId == hoveredTowerRuntimeId_) {
            const bool isOwnedByLocalPlayer = placed.ownerPlayerId == localPlayerId_;
            groundCircles.push_back({placed.position + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                     std::max(0.01f, placed.attackRange),
                                     isOwnedByLocalPlayer ? kHoverRangeFill : kHoverOtherPlayerRangeFill});
        }
    }
    for (const playlevel::ActiveEnemy& enemy : activeEnemies_) {
        const bool selected = enemy.runtimeId == selectedEnemyRuntimeId_;
        const bool hovered = !selected && enemy.runtimeId == hoveredEnemyRuntimeId_;
        if (!selected && !hovered) continue;
        const glm::vec3 position = sampleRoutePosition(enemy.distanceAlongPath);
        groundCircles.push_back({position + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f),
                                 std::max(0.35f, 0.5f * enemy.renderScale),
                                 selected ? kPlacementRangeFill : kHoverRangeFill,
                                 selected ? kPlacementRangeOutline : glm::vec4{0.0f}});
    }
    for (const playlevel::ActiveProjectile& projectile : matchSimulation_.activeProjectiles()) {
        const TowerArchetype* tower = towerLoadController_->findArchetype(projectile.towerId);
        const auto sourceTower = std::find_if(placedTowers_.begin(), placedTowers_.end(), [&projectile](const auto& placed) {
            return placed.runtimeId == projectile.sourceTowerRuntimeId;
        });
        const int prototypeIndex = projectile.prototypeIndex >= 0
                                       ? projectile.prototypeIndex
                                       : towerLoadController_->projectileTemplatePrototypeIndex(projectile.towerId);
        if (!tower || prototypeIndex < 0) {
            continue;
        }
        const float projectileFacingYawOffsetDegrees =
            sourceTower != placedTowers_.end() ? sourceTower->projectileFacingYawOffsetDegrees
                                               : tower->projectileFacingYawOffsetDegrees;
        const float projectileRenderScale = sourceTower != placedTowers_.end() ? sourceTower->projectileRenderScale
                                                                                : tower->projectileRenderScale;
        glm::vec3 direction = projectile.velocity;
        direction.y = 0.0f;
        const float yaw = glm::dot(direction, direction) > 1e-6f ? std::atan2(direction.x, direction.z) : 0.0f;
        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = glm::translate(glm::mat4{1.0f}, projectile.position) *
                             glm::rotate(glm::mat4{1.0f},
                                         yaw + glm::radians(projectileFacingYawOffsetDegrees),
                                         glm::vec3(0.0f, 1.0f, 0.0f)) *
                             glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, projectileRenderScale)));
        instance.prototypeIndex = prototypeIndex;
        instance.debugGroup = "tower-projectile:" + std::to_string(projectile.runtimeId);
        instance.debugLabel = projectile.towerId;
        instances.push_back(std::move(instance));
    }
    if (const TowerArchetype* tower = selectedTower()) {
        const int prototypeIndex = towerLoadController_->templatePrototypeIndex(tower->id);
        if (prototypeIndex >= 0 && placementSample_.hit) {
            AnimatedEntityInstanceSet::Instance ghost;
            ghost.transform = buildTowerTransform(placementSample_.worldPosition + glm::vec3(0.0f, 0.02f, 0.0f),
                                                  tower->facingYawOffsetDegrees,
                                                  tower->renderScale);
            ghost.prototypeIndex = prototypeIndex;
            ghost.alpha = kTowerGhostAlpha;
            ghost.debugGroup = "tower-placement-preview";
            ghost.debugLabel = tower->displayName;
            instances.push_back(std::move(ghost));

            GroundCircle rangeCircle;
            rangeCircle.center =
                placementSample_.worldPosition + glm::vec3(0.0f, kGroundCircleYOffset, 0.0f);
            rangeCircle.radius = std::max(0.01f, tower->attackRange);
            const bool validPlacement = placementReason_.empty();
            rangeCircle.color = validPlacement ? kPlacementRangeFill : kInvalidPlacementRangeFill;
            rangeCircle.outlineColor = validPlacement ? kPlacementRangeOutline : kInvalidPlacementRangeOutline;
            groundCircles.push_back(rangeCircle);
        }
    }
    worldRenderer_->setGroundCircles(std::move(groundCircles));
    worldRenderer_->setTowerInstanceTransforms(instances);
    const auto selectedTower = std::find_if(placedTowers_.begin(), placedTowers_.end(), [this](const auto& tower) {
        return tower.runtimeId == selectedTowerRuntimeId_;
    });
    const auto selectedEnemy = std::find_if(activeEnemies_.begin(), activeEnemies_.end(), [this](const auto& enemy) {
        return enemy.runtimeId == selectedEnemyRuntimeId_;
    });
    const int selectedTowerIndex = selectedTower == placedTowers_.end()
                                       ? -1
                                       : static_cast<int>(std::distance(placedTowers_.begin(), selectedTower));
    const int selectedEnemyIndex = selectedEnemy == activeEnemies_.end()
                                       ? -1
                                       : static_cast<int>(std::distance(activeEnemies_.begin(), selectedEnemy));
    const WorldEntityKind selectedKind = selectedEnemyIndex >= 0 ? WorldEntityKind::Enemy : WorldEntityKind::Tower;
    const int selectedIndex = selectedEnemyIndex >= 0 ? selectedEnemyIndex : selectedTowerIndex;
    worldRenderer_->setHighlightedInstances(WorldEntityKind::None, -1, selectedKind, selectedIndex);
}


void PlayLevelScene::pollGameplayTuning(float dt) {
#if !NODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING
    (void)dt;
    return;
#else
    if (session_.isClient()) {
        return;
    }

    gameplayTuningPollAccumulator_ += dt;
    if (gameplayTuningPollAccumulator_ < kGameplayTuningPollIntervalSeconds) {
        return;
    }
    gameplayTuningPollAccumulator_ = 0.0f;

    const bool enabledNow = gameplayTuningManager_.isRuntimeTuningEnabled();
    if (enabledNow != gameplayTuningEnabled_) {
        gameplayTuningEnabled_ = enabledNow;
        if (gameplayTuningEnabled_) {
            gameplayTuning_ = gameplayTuningManager_.loadOrCreateDefaults();
            gameplayTuningLastWriteTime_ =
                tryGetLastWriteTime(gameplayTuningManager_.tuningFilePath()).value_or(gameplayTuningLastWriteTime_);
            applyGameplayTuning();
        }
    }

    if (!gameplayTuningEnabled_) {
        return;
    }

    const auto writeTime = tryGetLastWriteTime(gameplayTuningManager_.tuningFilePath());
    if (!writeTime || *writeTime <= gameplayTuningLastWriteTime_) {
        return;
    }

    gameplayTuning_ = gameplayTuningManager_.loadOrCreateDefaults();
    gameplayTuningLastWriteTime_ = *writeTime;
    applyGameplayTuning();
#endif
}

void PlayLevelScene::applyGameplayTuning() {
#if !NODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING
    return;
#else
    if (!gameplayTuningEnabled_ || session_.isClient()) {
        return;
    }

    if (gameplayTuning_.baseHealthOverride >= 0.0f) {
        gameplayState_.baseHealth = gameplayTuning_.baseHealthOverride;
    }
    if (gameplayTuning_.waveCountdownSecondsOverride >= 0.0f) {
        gameplayState_.waveCountdownDurationSeconds = gameplayTuning_.waveCountdownSecondsOverride;
        if (gameplayState_.waveCountdownActive) {
            gameplayState_.waveCountdownRemainingSeconds =
                std::min(gameplayState_.waveCountdownRemainingSeconds, gameplayState_.waveCountdownDurationSeconds);
        }
    }
    if (gameplayTuning_.hostMoneyOverride >= 0.0f && matchSimulation_.hasPlayer(localPlayerId_)) {
        const float current = matchSimulation_.playerBalance(localPlayerId_);
        const float target = gameplayTuning_.hostMoneyOverride;
        if (target > current) {
            matchSimulation_.creditPlayer(localPlayerId_, target - current);
        } else if (target < current) {
            matchSimulation_.debitPlayer(localPlayerId_, current - target);
        }
        gameplayState_.playerMoney = matchSimulation_.playerBalance(localPlayerId_);
    }
#endif
}


} // namespace NodeSpireUi
