#pragma once

#include "lambui/LuaUiScene.hpp"
#include "AppSettings.hpp"
#include "GameplayTuningManager.hpp"
#include "multiplayer/IMatchTransport.hpp"
#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/MatchSimulation.hpp"
#include "multiplayer/MatchSnapshotBuilder.hpp"
#include "lambui/playlevel/PlayLevelUiContract.hpp"
#include "scenes/TowerPlacementPreviewResolver.hpp"
#include "scenes/TowerPlacementRules.hpp"
#include "utility/WorldRenderer.hpp"

class VulkanContext;
class TowerLoadController;
class EnemyLoadController;
struct SDL_Window;

namespace multiplayer {
class MultiplayerSession;
}

namespace NodeSpireUi {

class PlayLevelScene final : public LuaUiScene {
  public:
    PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                   const PlayLevelLaunchConfig& launchConfig, lambui_backend::VulkanUiRenderer& renderer,
                   AppSettings& settings, SDL_Window* window);
    ~PlayLevelScene() override;

    void onEnter(LambUI::UIManager& ui, AudioEngine& audio) override;
    void onExit(LambUI::UIManager& ui) override;
    SceneTransition update(float dt) override;
    SceneTransition onKeyDown(uint32_t scanCode) override;
    bool handleShortcut(uint32_t scanCode) override;
    void renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent) override;
    void renderOverlay(VkCommandBuffer commandBuffer, VkExtent2D extent) override;

  protected:
    void bindSceneApi(lua_State* lua, AudioEngine& audio) override;

  private:
    int pushState(lua_State* lua);
    static int dispatch(lua_State* lua);
    void beginWorldLoad();
    bool loadWaveDefinitions();
    void restartMatch();
    void updateCamera(float dt);
    void refreshHud();
    void consumeChat();
    void updateWorldHover();
    void updateWorldSelection();
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
    glm::mat4 buildTowerTransform(const glm::vec3& position, float facingYawOffsetDegrees, float renderScale) const;
    void setPauseMenuVisible(bool visible);
    void pollExternalSettings(float dt);
    void pollGameplayTuning(float dt);
    void applyGameplayTuning();

    VulkanContext& vulkanContext_;
    multiplayer::MultiplayerSession& session_;
    PlayLevelLaunchConfig launchConfig_;
    AppSettings& settings_;
    SDL_Window* window_ = nullptr;
    LambUI::UIManager* ui_ = nullptr;
    lua_State* lua_ = nullptr;
    AudioEngine* audio_ = nullptr;
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
    std::uint64_t selectedEnemyRuntimeId_ = 0;
    multiplayer::TowerRuntimeId hoveredTowerRuntimeId_ = 0;
    std::uint64_t hoveredEnemyRuntimeId_ = 0;
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
    std::vector<TowerPreviewPanel> towerPreviewPanels_;
    std::vector<std::string> chat_;
    float towerPreviewSpinRadians_ = 0.0f;
    std::filesystem::file_time_type settingsLastWriteTime_{};
    float settingsPollAccumulator_ = 0.0f;
    GameplayTuningManager gameplayTuningManager_;
    GameplayTuningConfig gameplayTuning_;
    bool gameplayTuningEnabled_ = false;
    std::filesystem::file_time_type gameplayTuningLastWriteTime_{};
    float gameplayTuningPollAccumulator_ = 0.0f;
};

} // namespace NodeSpireUi

