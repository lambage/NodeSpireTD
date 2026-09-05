#include "rmlui/scenes/PlayLevelScene.hpp"

#include "RmlUi_Backend.h"
#include "AudioEngine.hpp"
#include "VulkanContext.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "scenes/EnemyLoadController.hpp"
#include "scenes/EnemySpawnFactory.hpp"
#include "scenes/TowerLoadController.hpp"
#include "utility/WorldRenderer.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Lua/Interpreter.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <glm/vec4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <sstream>

namespace NodeSpireUi {
namespace {
constexpr const char* kDocumentPath = "assets/ui/playlevel/playlevel.rml";
constexpr const char* kInteractiveIds[] = {"retry-button", "start-match-button", "resume-button", "back-to-lobby-button",
                                            "master-volume-slider", "music-volume-slider", "sfx-volume-slider",
                                            "tower-slot-0", "tower-slot-1", "tower-slot-2", "tower-slot-3",
                                            "tower-slot-4", "close-tower-profile"};
constexpr float kTowerGhostAlpha = 0.45f;
constexpr multiplayer::SimulationTick kSnapshotIntervalTicks = 3;
constexpr glm::vec4 kPlacementRangeFill{0.18f, 0.72f, 0.48f, 0.16f};
constexpr glm::vec4 kPlacementRangeOutline{0.35f, 1.0f, 0.65f, 0.85f};

glm::vec3 cameraForward(float yaw, float pitch) {
    return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                    std::cos(pitch) * std::cos(yaw)));
}

void setText(Rml::ElementDocument* document, const char* id, const std::string& value) {
    if (Rml::Element* element = document->GetElementById(id)) {
        element->SetInnerRML(value);
    }
}
} // namespace

PlayLevelScene::PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                               const PlayLevelLaunchConfig& launchConfig)
        : vulkanContext_(vulkanContext), session_(session), launchConfig_(launchConfig),
            gameplayState_(matchSimulation_.gameplayState()), placedTowers_(matchSimulation_.placedTowers()),
            activeEnemies_(matchSimulation_.activeEnemies()) {}

PlayLevelScene::~PlayLevelScene() = default;

void PlayLevelScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    pendingTransition_.reset();
    pauseMenuVisible_ = false;
    onlineMatch_ = session_.isInParty();
    loadedReadySignaled_ = false;
    remoteJoinSent_ = false;
    remoteJoinAccepted_ = false;
    remotePlayerByPeer_.clear();
    nextCommandSequence_ = 1;
    localPlayerId_ = session_.localPlayerId() == 0 ? 1 : session_.localPlayerId();
    context_ = &context;
    matchSimulation_.reset();
    localMatchHost_ = multiplayer::LocalMatchHost{};
    if (!session_.isClient()) {
        matchSimulation_.registerPlayer(localPlayerId_, gameplayState_.playerMoney);
        localMatchHost_.registerPlayer(localPlayerId_);
    }
    placementSample_ = {};
    selectedTowerSlot_ = -1;
    selectedTowerRuntimeId_ = 0;
    placementReason_.clear();
    leftMouseDown_ = false;
    audio_ = &audio;
    settings_ = settingsManager_.loadOrCreateDefaults();
    document_ = context.LoadDocument(kDocumentPath);
    if (!document_) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", kDocumentPath);
        return;
    }

    for (const char* id : kInteractiveIds) {
        if (Rml::Element* element = document_->GetElementById(id)) {
            element->AddEventListener(Rml::EventId::Click, this);
            element->AddEventListener(Rml::EventId::Change, this);
        }
    }

    document_->Show();
    populateAudioControls();
    towerLoadController_ = std::make_unique<TowerLoadController>(Rml::Lua::Interpreter::GetLuaState());
    towerLoadController_->discoverTowerArchetypesInDirectory("assets/models/towers");
    towerLoadController_->setLoadoutIds(launchConfig_.towerLoadoutIds);
    enemyLoadController_ = std::make_unique<EnemyLoadController>(Rml::Lua::Interpreter::GetLuaState());
    if (!enemyLoadController_->loadEnemyArchetype("assets/models/enemy/goblin1.enemy.lua")) {
        enemyLoadController_->registerArchetype(EnemyArchetype{});
    }
    enemyLoadController_->loadEnemyArchetype("assets/models/enemy/goblin_scout.enemy.lua");
    matchSimulation_.waveController().loadWaveDefinitions(
        Rml::Lua::Interpreter::GetLuaState(), "assets/scenes/PlayLevelWaves.lua", enemyLoadController_->defaultId(),
        [this](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
            const EnemyArchetype* enemy = enemyLoadController_->findArchetype(enemyId);
            return enemy ? std::optional{PlayLevelWaveController::EnemyWaveDefaults{enemy->spawnIntervalSeconds}}
                         : std::nullopt;
        });
    beginWorldLoad();
    refreshHud();
}

void PlayLevelScene::onExit(Rml::Context& context) {
    if (mouseLookActive_) {
        SDL_SetWindowRelativeMouseMode(Backend::GetWindow(), false);
        mouseLookActive_ = false;
    }

    if (document_) {
        for (Rml::Element* button : upgradeButtonElements_) {
            button->RemoveEventListener(Rml::EventId::Click, this);
        }
        upgradeButtonElements_.clear();
        for (const char* id : kInteractiveIds) {
            if (Rml::Element* element = document_->GetElementById(id)) {
                element->RemoveEventListener(Rml::EventId::Click, this);
                element->RemoveEventListener(Rml::EventId::Change, this);
            }
        }
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }

    vulkanContext_.waitIdle();
    worldRenderer_.reset();
    towerLoadController_.reset();
    enemyLoadController_.reset();
    context_ = nullptr;
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

SceneTransition PlayLevelScene::update(float dt) {
    if (onlineMatch_ && !session_.isInParty()) {
        return SceneId::Lobby;
    }
    if (session_.isClient() && session_.consumeMatchEnded()) {
        return SceneId::Lobby;
    }

    if (!pauseMenuVisible_) {
        updateCamera(dt);
        updateTowerPlacement();
        updateMatchSimulation(dt);
    }

    if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart && session_.isClient() && session_.isMatchStarted()) {
        PlayLevelUiStateInput input;
        input.phase = PlayLevelUiPhase::Running;
        input.levelName = launchConfig_.displayName;
        input.worldReady = true;
        snapshot_ = buildPlayLevelUiSnapshot(input);
        gameplayState_.matchStatus = MatchStatus::Running;
        refreshHud();
    }

    if (worldRenderer_ && snapshot_.phase == PlayLevelUiPhase::Loading) {
        worldRenderer_->tickLoad();
        snapshot_.loadingProgress = worldRenderer_->loadProgress();
        snapshot_.loadingActivity = worldRenderer_->loadActivity();
        snapshot_.supportingText = snapshot_.loadingActivity.empty() ? "Preparing the battlefield..." : snapshot_.loadingActivity;

        if (worldRenderer_->loadFailed()) {
            snapshot_.phase = PlayLevelUiPhase::LoadFailed;
            snapshot_.headline = "Deployment failed";
            snapshot_.supportingText = worldRenderer_->statusMessage();
        } else if (worldRenderer_->isLoaded()) {
            PlayLevelUiStateInput input;
            input.phase = PlayLevelUiPhase::WaitingToStart;
            input.levelName = launchConfig_.displayName;
            input.worldReady = true;
            input.routeReady = !worldRenderer_->routePoints().empty();
            snapshot_ = buildPlayLevelUiSnapshot(input);
            if (onlineMatch_ && !loadedReadySignaled_) {
                session_.signalLocalLoadedReady();
                loadedReadySignaled_ = true;
            }
            if (session_.isClient() && session_.isMatchStarted()) {
                input.phase = PlayLevelUiPhase::Running;
                snapshot_ = buildPlayLevelUiSnapshot(input);
                gameplayState_.matchStatus = MatchStatus::Running;
            }
            syncTowerInstances();
            syncEnemyInstances();
        }
        refreshHud();
    }
    if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart) {
        refreshHud();
    }
    if (snapshot_.phase == PlayLevelUiPhase::Running) {
        refreshHud();
    }

    SceneTransition transition = pendingTransition_;
    pendingTransition_.reset();
    return transition;
}

void PlayLevelScene::updateCamera(float dt) {
    SDL_Window* window = Backend::GetWindow();
    if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
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

SceneTransition PlayLevelScene::onKeyDown(Rml::Input::KeyIdentifier key) {
    if (key == Rml::Input::KI_ESCAPE) {
        if (selectedTowerSlot_ >= 0) {
            selectedTowerSlot_ = -1;
            placementSample_ = {};
            placementReason_ = "Tower placement cancelled.";
            syncTowerInstances();
            refreshLoadout();
            return std::nullopt;
        }
        setPauseMenuVisible(!pauseMenuVisible_);
    } else if (key >= Rml::Input::KI_1 && key <= Rml::Input::KI_5) {
        const int slot = static_cast<int>(key) - static_cast<int>(Rml::Input::KI_1);
        const TowerArchetype* tower = towerLoadController_ ? towerLoadController_->archetypeAtLoadoutSlot(slot) : nullptr;
        if (tower && gameplayState_.playerMoney >= static_cast<float>(tower->cost)) {
            selectedTowerSlot_ = selectedTowerSlot_ == slot ? -1 : slot;
            placementReason_.clear();
            refreshLoadout();
        }
    }
    return std::nullopt;
}

void PlayLevelScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetCurrentElement();
    if (!target) {
        return;
    }

    const Rml::String id = target->GetId();
    if (event == Rml::EventId::Change) {
        if (id == "master-volume-slider") {
            settings_.masterVolume = event.GetParameter<float>("value", settings_.masterVolume);
            setAudioValueLabel("master-volume-value", settings_.masterVolume);
        } else if (id == "music-volume-slider") {
            settings_.musicVolume = event.GetParameter<float>("value", settings_.musicVolume);
            setAudioValueLabel("music-volume-value", settings_.musicVolume);
        } else if (id == "sfx-volume-slider") {
            settings_.sfxVolume = event.GetParameter<float>("value", settings_.sfxVolume);
            setAudioValueLabel("sfx-volume-value", settings_.sfxVolume);
        } else {
            return;
        }
        audio_->setEffectiveSettings(settings_);
        settingsManager_.save(settings_);
        return;
    }

    if (event != Rml::EventId::Click) {
        return;
    }

    if (id.starts_with("tower-slot-")) {
        const int slot = std::stoi(id.substr(11));
        const TowerArchetype* tower = towerLoadController_ ? towerLoadController_->archetypeAtLoadoutSlot(slot) : nullptr;
        if (!tower) {
            return;
        }
        if (gameplayState_.playerMoney < static_cast<float>(tower->cost)) {
            placementReason_ = "Not enough credits for " + tower->displayName + ".";
        } else {
            selectedTowerSlot_ = selectedTowerSlot_ == slot ? -1 : slot;
            placementReason_.clear();
        }
        refreshLoadout();
        return;
    }

    if (id == "close-tower-profile") {
        selectedTowerRuntimeId_ = 0;
        refreshTowerProfile();
        syncTowerInstances();
        return;
    }

    if (id.starts_with("upgrade-")) {
        multiplayer::UpgradeTowerCommand upgrade;
        upgrade.towerRuntimeId = selectedTowerRuntimeId_;
        upgrade.upgradeNodeId = id.substr(8);
        if (selectedTowerRuntimeId_ != 0 && submitCommand(std::move(upgrade))) {
            placementReason_ = "Tower upgraded.";
            refreshTowerProfile();
            refreshHud();
        } else {
            placementReason_ = "Upgrade unavailable or rejected by the host.";
        }
        refreshLoadout();
        return;
    }

    if (id == "retry-button") {
        vulkanContext_.waitIdle();
        worldRenderer_.reset();
        loadedReadySignaled_ = false;
        beginWorldLoad();
        refreshHud();
    } else if (id == "resume-button") {
        setPauseMenuVisible(false);
    } else if (id == "start-match-button") {
        if (!onlineMatch_ || session_.beginMatch()) {
            PlayLevelUiStateInput input;
            input.phase = PlayLevelUiPhase::Running;
            input.levelName = launchConfig_.displayName;
            input.worldReady = true;
            snapshot_ = buildPlayLevelUiSnapshot(input);
            gameplayState_.matchStatus = MatchStatus::Running;
            matchSimulation_.waveController().beginWaveCountdown(
                gameplayState_, worldRenderer_ && worldRenderer_->isLoaded(),
                worldRenderer_ && worldRenderer_->hasAnimatedEntityTemplate(),
                worldRenderer_ && worldRenderer_->routePoints().size() >= 2);
            refreshHud();
        }
    } else if (id == "back-to-lobby-button") {
        if (session_.isHost()) {
            session_.endMatch();
        }
        pendingTransition_ = SceneId::Lobby;
    }
}

const TowerArchetype* PlayLevelScene::selectedTower() const {
    return towerLoadController_ ? towerLoadController_->archetypeAtLoadoutSlot(selectedTowerSlot_) : nullptr;
}

bool PlayLevelScene::pointerIsOverHud() const {
    if (!context_ || !document_) {
        return false;
    }
    Rml::Element* hovered = context_->GetHoverElement();
    return hovered && hovered != document_ && hovered->GetId() != "playlevel-root";
}

glm::mat4 PlayLevelScene::buildTowerTransform(const TowerArchetype& tower, const glm::vec3& position) const {
    return glm::translate(glm::mat4{1.0f}, position) *
           glm::rotate(glm::mat4{1.0f}, glm::radians(tower.facingYawOffsetDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, tower.renderScale)));
}

void PlayLevelScene::updateTowerPlacement() {
    const bool leftMouseDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    const bool leftClicked = leftMouseDown && !leftMouseDown_;
    leftMouseDown_ = leftMouseDown;

    const TowerArchetype* tower = selectedTower();
    if (!tower || !worldRenderer_ || !worldRenderer_->isLoaded() ||
        gameplayState_.matchStatus != MatchStatus::Running) {
        if (!tower && leftClicked && !pointerIsOverHud() && gameplayState_.matchStatus == MatchStatus::Running) {
            updateTowerSelection();
        }
        placementSample_ = {};
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
    placementSample_ = TowerPlacementRules::sampleTerrainAtScreenPoint(
        placementContext, view, cameraPosition_, mouseX, mouseY, static_cast<float>(extent.width),
        static_cast<float>(extent.height));

    if (!placementSample_.hit) {
        placementReason_ = "Cursor is not over valid terrain.";
    } else {
        placementReason_ = TowerPlacementRules::validatePlacement(
            placementContext, *tower, placementSample_.worldPosition, 4, placementSample_, gameplayState_.playerMoney);
    }

    if (leftClicked && !pointerIsOverHud() && placementSample_.hit && placementReason_.empty()) {
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
    refreshLoadout();
}

void PlayLevelScene::updateTowerSelection() {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    const VkExtent2D extent = vulkanContext_.extent();
    if (extent.width == 0 || extent.height == 0) return;

    const float ndcX = 2.0f * mouseX / static_cast<float>(extent.width) - 1.0f;
    const float ndcY = 2.0f * mouseY / static_cast<float>(extent.height) - 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(extent.width) / extent.height,
                                            0.05f, 2000.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(cameraPosition_, cameraPosition_ + cameraForward(cameraYaw_, cameraPitch_),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    WorldPickHit hit;
    if (worldRenderer_->pickModel(cameraPosition_, glm::normalize(glm::vec3(farPoint - nearPoint)), hit) &&
        hit.entityKind == WorldEntityKind::Tower && hit.instanceIndex >= 0 &&
        static_cast<std::size_t>(hit.instanceIndex) < placedTowers_.size()) {
        selectedTowerRuntimeId_ = placedTowers_[static_cast<std::size_t>(hit.instanceIndex)].runtimeId;
    } else {
        selectedTowerRuntimeId_ = 0;
    }
    refreshTowerProfile();
}

void PlayLevelScene::refreshTowerProfile() {
    if (!document_ || !towerLoadController_) return;
    Rml::Element* panel = document_->GetElementById("tower-profile");
    if (!panel) return;
    const auto found = std::find_if(placedTowers_.begin(), placedTowers_.end(), [this](const auto& tower) {
        return tower.runtimeId == selectedTowerRuntimeId_;
    });
    if (found == placedTowers_.end()) {
        selectedTowerRuntimeId_ = 0;
        panel->SetClass("hidden", true);
        return;
    }
    const TowerArchetype* archetype = towerLoadController_->findArchetype(found->towerId);
    if (!archetype) {
        panel->SetClass("hidden", true);
        return;
    }
    panel->SetClass("hidden", false);
    setText(document_, "tower-profile-name", Rml::StringUtilities::EncodeRml(archetype->displayName));
    setText(document_, "tower-profile-bio", Rml::StringUtilities::EncodeRml(archetype->bio));
    char value[32];
    std::snprintf(value, sizeof(value), "%.1f", found->attackDamage);
    setText(document_, "tower-damage", value);
    std::snprintf(value, sizeof(value), "%.1f", found->attackRange);
    setText(document_, "tower-range", value);
    std::snprintf(value, sizeof(value), "%.2f/s", 1.0f / std::max(0.01f, found->attackIntervalSeconds));
    setText(document_, "tower-rate", value);
    int totalSpent = found->cost;
    std::unordered_map<std::string, int> purchasedLevels;
    for (const auto& nodeId : found->unlockedUpgradeNodeIds) {
        const auto node = std::find_if(archetype->upgradeNodes.begin(), archetype->upgradeNodes.end(), [&nodeId](const auto& item) {
            return item.id == nodeId;
        });
        if (node == archetype->upgradeNodes.end()) continue;
        const int level = purchasedLevels[nodeId]++;
        if (level < static_cast<int>(node->upgradeLevels.size())) totalSpent += node->upgradeLevels[level].cost;
    }
    setText(document_, "tower-spent", "$" + std::to_string(totalSpent));
    setText(document_, "tower-damage-type", std::string(playlevel::damageTypeToString(found->damageType)) + " damage");
    std::snprintf(value, sizeof(value), "Armor piercing: %.1f", found->armorPiercing);
    setText(document_, "tower-armor-piercing", value);
    std::snprintf(value, sizeof(value), "Projectile: %.1f speed, x%d", found->projectileSpeed, found->projectileCount);
    setText(document_, "tower-projectile", value);
    char areaStats[96];
    std::snprintf(areaStats, sizeof(areaStats), "Splash: %.1f | Chain: %.1f (x%d) | Ricochet: %.1f (x%d)",
                  found->splashRadius, found->chainRange, found->chainTargetCount, found->ricochetRange,
                  found->ricochetCount);
    setText(document_, "tower-area-stats", areaStats);

    for (Rml::Element* button : upgradeButtonElements_) {
        button->RemoveEventListener(Rml::EventId::Click, this);
    }
    upgradeButtonElements_.clear();

    std::ifstream fragment("assets/ui/playlevel/towers/" + archetype->id + ".rml");
    std::ostringstream contents;
    if (fragment) contents << fragment.rdbuf();
    setText(document_, "tower-tech-tree", fragment ? contents.str() : "<p class=\"empty-tree\">No upgrades available.</p>");
    for (const auto& node : archetype->upgradeNodes) {
        if (Rml::Element* button = document_->GetElementById("upgrade-" + node.id)) {
            button->AddEventListener(Rml::EventId::Click, this);
            upgradeButtonElements_.push_back(button);
            const int level = static_cast<int>(std::count(found->unlockedUpgradeNodeIds.begin(),
                                                          found->unlockedUpgradeNodeIds.end(), node.id));
            button->SetClass("is-unlocked", level > 0);
            if (level >= static_cast<int>(node.upgradeLevels.size()) || found->ownerPlayerId != localPlayerId_) {
                button->SetAttribute("disabled", "");
            }
        }
    }
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
        placed.attackDamage = tower->attackDamage;
        placed.armorPiercing = tower->armorPiercing;
        placed.attackRange = tower->attackRange;
        placed.attackIntervalSeconds = 1.0f / std::max(0.01f, tower->attackSpeed);
        placed.projectileSpeed = tower->projectileSpeed;
        placed.splashRadius = tower->splashRadius;
        placed.chainRange = tower->chainRange;
        placed.ricochetRange = tower->ricochetRange;
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
        placed->projectileCount = std::max(1, placed->projectileCount + effects.projectileCountAdd);
        placed->chainTargetCount = std::max(1, placed->chainTargetCount + effects.chainTargetCountAdd);
        placed->ricochetCount = std::max(0, placed->ricochetCount + effects.ricochetCountAdd);
        placed->unlockedUpgradeNodeIds.push_back(node->id);
        if (node->towerPrototypeOverrideIndex >= 0) placed->towerPrototypeIndex = node->towerPrototypeOverrideIndex;
        if (node->projectilePrototypeOverrideIndex >= 0) placed->projectilePrototypeIndex = node->projectilePrototypeOverrideIndex;
        if (command.playerId == localPlayerId_) gameplayState_.playerMoney = matchSimulation_.playerBalance(localPlayerId_);
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
            transport.disconnectPeer(request.peerId);
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
    if (!session_.isHost() || remotePlayerByPeer_.empty() || tick % kSnapshotIntervalTicks != 0) {
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
    if (gameplayState_.matchStatus != MatchStatus::Running) {
        return;
    }
    matchSimulation_.advance(dt, [this](multiplayer::SimulationTick tick, float tickSeconds) {
        drainRemoteCommands(tick);
        updateWaveSimulation(tickSeconds);
        publishSnapshot(tick);
    });
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
    wave.updateWaveSpawning(gameplayState_, dt, static_cast<int>(activeEnemies_.size()), [this](const std::string& id) {
        const EnemyArchetype* archetype = enemyLoadController_->findArchetype(id);
        int prototypeIndex = 0;
        if (archetype) {
            const auto found = std::find(launchConfig_.animatedTemplateModelPaths.begin(),
                                         launchConfig_.animatedTemplateModelPaths.end(), archetype->modelPath);
            if (found != launchConfig_.animatedTemplateModelPaths.end()) {
                prototypeIndex = static_cast<int>(std::distance(launchConfig_.animatedTemplateModelPaths.begin(), found));
            }
        }
        activeEnemies_.push_back(EnemySpawnFactory::create(id, archetype, matchSimulation_.nextEnemyRuntimeId()++,
                                                           prototypeIndex));
    });

    PlayLevelCombatController& combat = matchSimulation_.combatController();
    combat.advanceEnemies(dt, routeLength(), activeEnemies_, [this](float damage) {
        gameplayState_.baseHealth = std::max(0.0f, gameplayState_.baseHealth - damage);
    });
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
    gameplayState_.enemiesAlive = static_cast<int>(activeEnemies_.size());
    if (gameplayState_.baseHealth <= 0.0f) gameplayState_.matchStatus = MatchStatus::Defeat;
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
        placed.attackDamage = tower->attackDamage;
        placed.armorPiercing = tower->armorPiercing;
        placed.attackRange = tower->attackRange;
        placed.attackIntervalSeconds = 1.0f / std::max(0.01f, tower->attackSpeed);
        placed.projectileSpeed = tower->projectileSpeed;
        placed.splashRadius = tower->splashRadius;
        placed.chainRange = tower->chainRange;
        placed.ricochetRange = tower->ricochetRange;
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
            placed.projectileCount = std::max(1, placed.projectileCount + effects.projectileCountAdd);
            placed.chainTargetCount = std::max(1, placed.chainTargetCount + effects.chainTargetCountAdd);
            placed.ricochetCount = std::max(0, placed.ricochetCount + effects.ricochetCountAdd);
            if (node->towerPrototypeOverrideIndex >= 0) placed.towerPrototypeIndex = node->towerPrototypeOverrideIndex;
            if (node->projectilePrototypeOverrideIndex >= 0) placed.projectilePrototypeIndex = node->projectilePrototypeOverrideIndex;
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
    refreshTowerProfile();
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
        instance.transform = buildTowerTransform(*tower, placed.position);
        instance.prototypeIndex = placed.towerPrototypeIndex;
        instance.debugGroup = "placed-tower:" + std::to_string(index);
        instance.debugLabel = tower->displayName;
        instances.push_back(std::move(instance));
        if (placed.runtimeId == selectedTowerRuntimeId_) {
            groundCircles.push_back({placed.position + glm::vec3(0.0f, 0.03f, 0.0f),
                                     std::max(0.01f, placed.attackRange),
                                     {0.82f, 0.65f, 0.20f, 0.13f}, {1.0f, 0.82f, 0.28f, 0.9f}});
        }
    }
    for (const playlevel::ActiveProjectile& projectile : matchSimulation_.activeProjectiles()) {
        const TowerArchetype* tower = towerLoadController_->findArchetype(projectile.towerId);
        const int prototypeIndex = projectile.prototypeIndex >= 0
                                       ? projectile.prototypeIndex
                                       : towerLoadController_->projectileTemplatePrototypeIndex(projectile.towerId);
        if (!tower || prototypeIndex < 0) {
            continue;
        }
        glm::vec3 direction = projectile.velocity;
        direction.y = 0.0f;
        const float yaw = glm::dot(direction, direction) > 1e-6f ? std::atan2(direction.x, direction.z) : 0.0f;
        AnimatedEntityInstanceSet::Instance instance;
        instance.transform = glm::translate(glm::mat4{1.0f}, projectile.position) *
                             glm::rotate(glm::mat4{1.0f}, yaw + glm::radians(tower->facingYawOffsetDegrees),
                                         glm::vec3(0.0f, 1.0f, 0.0f)) *
                             glm::scale(glm::mat4{1.0f}, glm::vec3(std::max(0.01f, tower->renderScale)));
        instance.prototypeIndex = prototypeIndex;
        instance.debugGroup = "tower-projectile:" + std::to_string(projectile.runtimeId);
        instance.debugLabel = projectile.towerId;
        instances.push_back(std::move(instance));
    }
    if (const TowerArchetype* tower = selectedTower()) {
        const int prototypeIndex = towerLoadController_->templatePrototypeIndex(tower->id);
        if (prototypeIndex >= 0 && placementSample_.hit) {
            AnimatedEntityInstanceSet::Instance ghost;
            ghost.transform = buildTowerTransform(*tower, placementSample_.worldPosition + glm::vec3(0.0f, 0.02f, 0.0f));
            ghost.prototypeIndex = prototypeIndex;
            ghost.alpha = kTowerGhostAlpha;
            ghost.debugGroup = "tower-placement-preview";
            ghost.debugLabel = tower->displayName;
            instances.push_back(std::move(ghost));

            GroundCircle rangeCircle;
            rangeCircle.center = placementSample_.worldPosition + glm::vec3(0.0f, 0.03f, 0.0f);
            rangeCircle.radius = std::max(0.01f, tower->attackRange);
            rangeCircle.color = kPlacementRangeFill;
            rangeCircle.outlineColor = kPlacementRangeOutline;
            groundCircles.push_back(rangeCircle);
        }
    }
    worldRenderer_->setGroundCircles(std::move(groundCircles));
    worldRenderer_->setTowerInstanceTransforms(instances);
    const auto selected = std::find_if(placedTowers_.begin(), placedTowers_.end(), [this](const auto& tower) {
        return tower.runtimeId == selectedTowerRuntimeId_;
    });
    const int selectedIndex = selected == placedTowers_.end() ? -1 : static_cast<int>(std::distance(placedTowers_.begin(), selected));
    worldRenderer_->setHighlightedInstances(WorldEntityKind::None, -1, WorldEntityKind::Tower, selectedIndex);
}

void PlayLevelScene::setPauseMenuVisible(bool visible) {
    pauseMenuVisible_ = visible;
    if (mouseLookActive_) {
        SDL_SetWindowRelativeMouseMode(Backend::GetWindow(), false);
        mouseLookActive_ = false;
    }
    if (document_) {
        if (Rml::Element* menu = document_->GetElementById("pause-menu")) {
            menu->SetClass("hidden", !visible);
        }
    }
}

void PlayLevelScene::setAudioValueLabel(const char* id, float value) {
    char label[16];
    std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(value * 100.0f + 0.5f));
    setText(document_, id, label);
}

void PlayLevelScene::populateAudioControls() {
    const struct VolumeControl {
        const char* sliderId;
        const char* valueId;
        float value;
    } controls[] = {{"master-volume-slider", "master-volume-value", settings_.masterVolume},
                    {"music-volume-slider", "music-volume-value", settings_.musicVolume},
                    {"sfx-volume-slider", "sfx-volume-value", settings_.sfxVolume}};
    for (const auto& control : controls) {
        if (Rml::Element* slider = document_->GetElementById(control.sliderId)) {
            slider->SetAttribute("value", control.value);
        }
        setAudioValueLabel(control.valueId, control.value);
    }
}

void PlayLevelScene::refreshHud() {
    if (!document_) {
        return;
    }

    if (snapshot_.phase == PlayLevelUiPhase::Running || gameplayState_.matchStatus == MatchStatus::Victory ||
        gameplayState_.matchStatus == MatchStatus::Defeat) {
        PlayLevelUiStateInput input;
        input.phase = gameplayState_.matchStatus == MatchStatus::Victory
                          ? PlayLevelUiPhase::Victory
                          : gameplayState_.matchStatus == MatchStatus::Defeat ? PlayLevelUiPhase::Defeat
                                                                             : PlayLevelUiPhase::Running;
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
    }

    setText(document_, "level-name", snapshot_.levelName);
    setText(document_, "status-headline", snapshot_.headline);
    setText(document_, "status-copy", snapshot_.supportingText);
    setText(document_, "money-value", "$" + std::to_string(static_cast<int>(gameplayState_.playerMoney)));
    setText(document_, "base-health-value", std::to_string(static_cast<int>(gameplayState_.baseHealth)));
    setText(document_, "wave-value", std::to_string(gameplayState_.currentWave));
    setText(document_, "enemy-count-value", std::to_string(snapshot_.enemiesRemaining));

    if (Rml::Element* countdown = document_->GetElementById("wave-hud")) {
        countdown->SetClass("hidden", !snapshot_.countdownVisible);
    }
    setText(document_, "wave-label", snapshot_.countdownLabel);
    setText(document_, "wave-seconds", std::to_string(snapshot_.countdownSeconds));
    if (Rml::Element* countdownProgress = document_->GetElementById("countdown-progress")) {
        countdownProgress->SetProperty("width", std::to_string(snapshot_.countdownProgress * 100.0f) + "%");
    }

    if (Rml::Element* progress = document_->GetElementById("loading-progress")) {
        progress->SetProperty("width", std::to_string(snapshot_.loadingProgress * 100.0f) + "%");
    }
    if (Rml::Element* retry = document_->GetElementById("retry-button")) {
        retry->SetClass("visible", snapshot_.phase == PlayLevelUiPhase::LoadFailed);
    }
    if (Rml::Element* ready = document_->GetElementById("ready-state")) {
        ready->SetClass("visible", snapshot_.phase == PlayLevelUiPhase::WaitingToStart);
        if (snapshot_.phase == PlayLevelUiPhase::WaitingToStart) {
            ready->SetInnerRML(session_.isClient() ? "WAITING FOR HOST TO START" : "WORLD READY");
        }
    }
    if (Rml::Element* role = document_->GetElementById("match-role")) {
        role->SetInnerRML(!onlineMatch_ ? "SOLO" : (session_.isHost() ? "HOST" : "CLIENT"));
    }
    if (Rml::Element* start = document_->GetElementById("start-match-button")) {
        const bool visible = snapshot_.phase == PlayLevelUiPhase::WaitingToStart && !session_.isClient();
        start->SetClass("visible", visible);
        if (visible && onlineMatch_ && !session_.allMembersLoadedReady()) {
            start->SetAttribute("disabled", "");
        } else {
            start->RemoveAttribute("disabled");
        }
    }
    refreshLoadout();
}

void PlayLevelScene::refreshLoadout() {
    if (!document_ || !towerLoadController_) {
        return;
    }
    if (Rml::Element* bar = document_->GetElementById("tower-loadout")) {
        bar->SetClass("hidden", !snapshot_.loadoutVisible);
    }
    for (int slot = 0; slot < 5; ++slot) {
        Rml::Element* button = document_->GetElementById("tower-slot-" + std::to_string(slot));
        if (!button) {
            continue;
        }
        const TowerArchetype* tower = towerLoadController_->archetypeAtLoadoutSlot(slot);
        if (!tower) {
            button->SetInnerRML("<span class=\"slot-key\">" + std::to_string(slot + 1) +
                                "</span><span class=\"slot-name\">EMPTY</span>");
            button->SetAttribute("disabled", "");
            button->SetClass("is-selected", false);
            continue;
        }
        button->SetInnerRML("<span class=\"slot-key\">" + std::to_string(slot + 1) +
                            "</span><span class=\"slot-name\">" +
                            Rml::StringUtilities::EncodeRml(tower->displayName) +
                            "</span><span class=\"slot-cost\">$" + std::to_string(tower->cost) + "</span>");
        if (gameplayState_.playerMoney >= static_cast<float>(tower->cost)) {
            button->RemoveAttribute("disabled");
        } else {
            button->SetAttribute("disabled", "");
        }
        button->SetClass("is-selected", selectedTowerSlot_ == slot);
    }
    if (Rml::Element* feedback = document_->GetElementById("placement-feedback")) {
        feedback->SetInnerRML(Rml::StringUtilities::EncodeRml(placementReason_));
        feedback->SetClass("valid", selectedTower() && placementSample_.hit && placementReason_.empty());
    }
}

} // namespace NodeSpireUi
