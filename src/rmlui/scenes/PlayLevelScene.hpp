#pragma once

#include "AppSettings.hpp"
#include "SettingsManager.hpp"
#include "multiplayer/IMatchTransport.hpp"
#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/MatchSimulation.hpp"
#include "multiplayer/MatchSnapshotBuilder.hpp"
#include "rmlui/IScene.hpp"
#include "rmlui/playlevel/PlayLevelUiContract.hpp"
#include "scenes/TowerPlacementPreviewResolver.hpp"
#include "scenes/TowerPlacementRules.hpp"

#include <RmlUi/Core/EventListener.h>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <unordered_map>

class VulkanContext;
class WorldRenderer;
class TowerLoadController;
class EnemyLoadController;
struct TowerPreviewPanel;

namespace multiplayer {
class MultiplayerSession;
}

namespace Rml {
class Context;
class ElementDocument;
}

namespace NodeSpireUi {

class PlayLevelScene final : public IScene, public Rml::EventListener {
  public:
    PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                   const PlayLevelLaunchConfig& launchConfig);
    ~PlayLevelScene() override;

    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;
    void renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent) override;
    void renderOverlay(VkCommandBuffer commandBuffer, VkExtent2D extent) override;
    SceneTransition onKeyDown(Rml::Input::KeyIdentifier key) override;
    void ProcessEvent(Rml::Event& event) override;

  private:
    void beginWorldLoad();
    bool loadWaveDefinitions();
    void restartMatch();
    void updateCamera(float dt);
    void refreshHud();
    void refreshLoadout();
    void refreshTowerSlotInspector(int slot);
    void updateTowerSelection();
    void refreshTowerProfile();
    void refreshTalentInspector(const std::string& nodeId);
    void updateTowerPlacement();
    void updateMatchSimulation(float dt);
    void updateWaveSimulation(float dt);
    void processIncomingJoinRequests();
    void processRemoteJoinResult();
    void drainRemoteCommands(multiplayer::SimulationTick tick);
    void publishSnapshot(multiplayer::SimulationTick tick);
    void applyRemoteSnapshot(const multiplayer::DecodedMatchSnapshot& snapshot);
    bool submitCommand(multiplayer::PlayerCommandPayload payload);
    std::optional<multiplayer::CommandRejectionReason>
    dispatchAuthoritativeCommand(const multiplayer::PlayerCommandRequest& command);
    void syncTowerInstances();
    void syncEnemyInstances();
    glm::vec3 sampleRoutePosition(float distance) const;
    float sampleRouteYaw(float distance) const;
    float routeLength() const;
    bool pointerIsOverHud() const;
    const TowerArchetype* selectedTower() const;
    glm::mat4 buildTowerTransform(const TowerArchetype& tower, const glm::vec3& position) const;
    void setPauseMenuVisible(bool visible);
    void populateAudioControls();
    void setAudioValueLabel(const char* id, float value);

    VulkanContext& vulkanContext_;
    multiplayer::MultiplayerSession& session_;
    PlayLevelLaunchConfig launchConfig_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::unique_ptr<TowerLoadController> towerLoadController_;
    std::unique_ptr<EnemyLoadController> enemyLoadController_;
    multiplayer::MatchSimulation matchSimulation_;
    PlayLevelState& gameplayState_;
    std::vector<playlevel::PlacedTower>& placedTowers_;
    std::vector<playlevel::ActiveEnemy>& activeEnemies_;
    multiplayer::LocalMatchHost localMatchHost_;
    std::unordered_map<multiplayer::TransportPeerId, multiplayer::PlayerId> remotePlayerByPeer_;
    multiplayer::CommandSequence nextCommandSequence_ = 1;
    multiplayer::PlayerId localPlayerId_ = 1;
    bool remoteJoinSent_ = false;
    bool remoteJoinAccepted_ = false;
    PlacementTerrainSample placementSample_;
    TowerPlacementPreviewResolver towerPlacementPreviewResolver_;
    int selectedTowerSlot_ = -1;
    multiplayer::TowerRuntimeId selectedTowerRuntimeId_ = 0;
    std::string placementReason_;
    bool leftMouseDown_ = false;
    PlayLevelUiSnapshot snapshot_;
    SceneTransition pendingTransition_;
    glm::vec3 cameraPosition_{0.0f, 5.0f, 20.0f};
    float cameraYaw_ = 3.14159f;
    float cameraPitch_ = -0.25f;
    bool mouseLookActive_ = false;
    bool pauseMenuVisible_ = false;
    bool onlineMatch_ = false;
    bool loadedReadySignaled_ = false;
    std::vector<Rml::Element*> upgradeButtonElements_;
    std::vector<TowerPreviewPanel> towerPreviewPanels_;
    float towerPreviewSpinRadians_ = 0.0f;
    multiplayer::TowerRuntimeId renderedTowerProfileRuntimeId_ = 0;
    multiplayer::PlayerId renderedTowerProfileOwnerId_ = 0;
    multiplayer::PlayerId renderedTowerProfileViewerId_ = 0;
    std::string renderedTowerProfileArchetypeId_;
    std::vector<std::string> renderedTowerProfileUpgradeIds_;
    AudioEngine* audio_ = nullptr;
    SettingsManager settingsManager_;
    AppSettings settings_;
};

} // namespace NodeSpireUi
