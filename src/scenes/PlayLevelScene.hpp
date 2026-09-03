#pragma once
#include "multiplayer/IMatchTransport.hpp"
#include "multiplayer/LanMatchClient.hpp"
#include "multiplayer/LanMatchTransport.hpp"
#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/MatchSimulation.hpp"
#include "multiplayer/MatchSnapshotBuilder.hpp"

#include <boost/asio/io_context.hpp>
#include "scenes/EnemyLoadController.hpp"
#include "scenes/PlayLevelBootstrap.hpp"
#include "scenes/GameScene.hpp"
#include "scenes/PlayLevelCameraController.hpp"
#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/PlayLevelFrameCoordinator.hpp"
#include "scenes/PlayLevelPickingController.hpp"
#include "scenes/PlayLevelRouteController.hpp"
#include "scenes/PlayLevelState.hpp"
#include "scenes/PlayLevelTowerPlacementController.hpp"
#include "scenes/TowerPlacementPreviewResolver.hpp"
#include "scenes/PlayLevelWaveController.hpp"
#include "scenes/TowerLoadController.hpp"
#include "utility/WorldAssetLoader.hpp"
#include "utility/WorldRenderer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare tower placement types
enum class TowerPlacementRegionType;
struct TowerPlacementRegion;

class PlayLevelScene final : public GameScene {
  public:
    PlayLevelScene();
    ~PlayLevelScene() override;

    void onEnter(SceneSharedState&) override;
    void onExit(SceneSharedState&) override;

    void render(SceneSharedState& state, float dt) override;
    void renderWorld(VkCommandBuffer cmd, VkExtent2D extent) override;

    // Starts (or restarts, on a new port) listening for LAN peers. Safe to leave uncalled for
    // single-player. Returns false if the port could not be bound.
    bool startHostingOnPort(unsigned short port);
    void stopHosting();
    unsigned short hostingPort() const;
    bool disconnectRemotePlayer(multiplayer::TransportPeerId peerId);

    // Connects to a remote host as a co-op client instead of becoming the authoritative host.
    // Blocks briefly on TCP connect/resolve (LAN-scale, acceptable). Returns false immediately if
    // the connection itself fails; join acceptance/rejection arrives asynchronously afterward (see
    // isRemoteClientJoinPending()/hasRemoteClientJoinFailed()).
    bool startJoiningHost(const std::string& hostAddress, unsigned short port, const std::string& displayName);
    bool isRemoteClient() const { return isRemoteClient_; }
    bool isRemoteClientJoinPending() const { return isRemoteClient_ && remoteJoinPending_; }
    const std::string& remoteClientJoinFailureReason() const { return remoteJoinFailureReason_; }

  private:
    enum class GameplayCommandType {
        SpendMoney,
        DamageBase,
        StartWave
    };

    struct GameplayCommand {
        GameplayCommandType type;
        float amount;
    };

    using ActiveEnemy = playlevel::ActiveEnemy;
    using PlacedTower = playlevel::PlacedTower;
    using ActiveProjectile = playlevel::ActiveProjectile;

    std::unique_ptr<WorldRenderer> worldRenderer_;
    multiplayer::MatchSimulation matchSimulation_{};
    PlayLevelState& gameplayState_;
    PlayLevelTowerPlacementController towerPlacementController_{};
    PlayLevelCombatController& combatController_;
    std::vector<PlacedTower>& placedTowers_;
    std::vector<ActiveProjectile>& activeProjectiles_;
    std::uint64_t& nextTowerRuntimeId_;
    std::uint64_t& nextEnemyRuntimeId_;
    std::uint64_t& nextProjectileRuntimeId_;
    PlayLevelWaveController& waveController_;
    std::vector<ActiveEnemy>& activeEnemies_;
    std::filesystem::path selectedMapAssetPath_;
    std::filesystem::path selectedLevelScriptPath_;
    std::string selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
    WorldAssetSpec worldAssetSpec_{};
    PlayLevelRouteController routeController_{};
    std::vector<GameplayCommand> pendingCommands_;
    multiplayer::LocalMatchHost localMatchHost_{};
    multiplayer::CommandSequence nextLocalCommandSequence_ = 1;
    // Host-side inbound channel for non-host players. networkIoContext_ must be declared before
    // remoteTransport_: LanMatchTransport binds a reference to it at construction, and this class
    // polls the context once per frame in advanceAuthoritativeSimulation().
    boost::asio::io_context networkIoContext_;
    multiplayer::LanMatchTransport remoteTransport_{networkIoContext_};
    std::unordered_map<multiplayer::TransportPeerId, multiplayer::PlayerId> remotePlayerByPeer_{};
    // Digest of every loaded tower/enemy archetype id, sent to joining peers' content manifests
    // for comparison. Computed once per level load, after bootstrap_.configureLevel().
    std::string gameplayContentSha256_;
    // Client-mode state: when isRemoteClient_ is true, this scene instance never ticks
    // matchSimulation_ itself -- it only sends local input through remoteClient_ and applies
    // snapshots the real host publishes. localPlayerId_ defaults to the host/single-player id and
    // is overwritten with whatever id the remote host assigns once a join is accepted.
    multiplayer::LanMatchClient remoteClient_{networkIoContext_};
    bool isRemoteClient_ = false;
    bool remoteJoinPending_ = false;
    std::string remoteJoinFailureReason_;
    multiplayer::PlayerId localPlayerId_ = 1;
    std::string loadStatus_;
    PlayLevelBootstrap bootstrap_{};
    PlayLevelFrameCoordinator frameCoordinator_{};
    PlayLevelPickingController pickingController_{};
    TowerPlacementPreviewResolver towerPlacementPreviewResolver_{};
    std::uint64_t selectedEnemyRuntimeId_ = 0;
    // One-shot: the first time the enemy template's animation data is available, put the shared
    // clip on Idle (if the model has one) before any enemy has spawned. Guarded so it fires once
    // per level entry rather than fighting the debug "Clip List" panel's manual clip selection.
    bool enemyAnimationInitialized_ = false;
    std::vector<TowerPreviewPanel> towerPreviewPanels_;
    float towerPreviewSpinRadians_ = 0.0f;

    // Flying camera state
    PlayLevelCameraController cameraController_{};
    VkExtent2D lastRenderExtent_{};
    TowerLoadController towerLoadController_;
    EnemyLoadController enemyLoadController_;

    // Tower placement regions and validation
    std::vector<TowerPlacementRegion> placementRegions_;
    float maxTowerPlacementSlopeDegrees_ = 30.0f;
    // Half-width (world units) of the no-build corridor auto-generated around the enemy route
    // (Start -> Waypoint_N -> End). Replaces manually-authored "PathPoint"/"path_zone" terrain
    // markers -- map makers only need the required waypoint markers, and the corridor bounding
    // box that blocks tower placement is derived from them automatically. See also
    // kPathCorridorEndExtension/kPathCorridorVerticalMargin (PlayLevelScene.cpp) to fine-tune the
    // other two dimensions of the box.
    float pathCorridorHalfWidth_ = 2.0f;
    // Toggleable via Lua/debug menu: draws wireframe boxes for the auto-generated path corridor
    // and the manually-authored water/cliff placement regions.
    bool placementBoundsVisible_ = false;
    // Cached reason for the current frame's placement verdict. Filled during
    // updateTowerPlacementFromInput() so Lua/UI can read it without re-running
    // expensive placement validation.
    std::string lastPlacementValidationReason_;

    bool requestSpendMoney(float amount);
    bool requestDamageBase(float amount);
    bool requestStartWave();

    const TowerArchetype* selectedTowerArchetype() const;
    std::string validateTowerPlacement(const TowerArchetype& archetype, const glm::vec3& worldPos,
                       multiplayer::PlayerId playerId, int footprintSampleCount = 8) const;
    const PlacedTower* findPlacedTowerByPoolKey(const std::string& towerId, int poolIndex) const;
    PlacedTower* findPlacedTowerByPoolKey(const std::string& towerId, int poolIndex);
    const TowerArchetype::UpgradeNode* findUpgradeNodeById(const TowerArchetype& archetype,
                                 const std::string& nodeId) const;
    void applyTowerUpgradeEffects(const TowerArchetype& archetype, const PlacedTower& placedTower,
                                  float& outAttackDamage, float& outAttackRange, float& outAttackSpeed,
                                  float& outProjectileSpeed, float& outSplashRadius, float& outChainRange,
                                  float& outRicochetRange, int& outProjectileCount, int& outChainTargetCount,
                                  int& outRicochetCount) const;
    std::string validateTowerUpgradeUnlock(const TowerArchetype& archetype, const PlacedTower& placedTower,
                                           const std::string& nodeId, multiplayer::PlayerId playerId) const;
    bool unlockTowerUpgrade(PlacedTower& placedTower, const std::string& nodeId, multiplayer::PlayerId playerId,
                            std::string& outReason);
    bool spendPlayerMoney(multiplayer::PlayerId playerId, float amount);
    bool creditPlayerMoney(multiplayer::PlayerId playerId, float amount);
    void syncLocalPlayerMoney();
    void clearActiveSelectionForTowerPlacement(const char* reason);
    void updateTowerPlacementFromInput();
    glm::mat4 buildTowerModelTransform(const TowerArchetype& archetype, const glm::vec3& worldPos) const;
    void syncPlacedTowerModels();
    void syncTowerInstanceTransforms();
    void drawTowerPlacementOverlay() const;
    void drawPlacementBoundsOverlay() const;
    bool loadWaveDefinitions(const std::string& scriptPath);
    bool updateRouteFromWorld();
    glm::vec3 sampleRoutePosition(float distanceAlongPath) const;
    float sampleRouteYaw(float distanceAlongPath) const;
    void syncEnemyInstanceTransforms();
    std::string validateStartWaveRequest() const;
    void applyPendingGameplayCommands();
    void advanceAuthoritativeSimulation(float elapsedSeconds);
    // Single authority boundary: validates and applies one already-sequenced command against the
    // simulation, regardless of whether it originated from the local host player or a remote peer
    // drained off remoteTransport_.
    std::optional<multiplayer::CommandRejectionReason>
    dispatchAuthoritativeCommand(const multiplayer::PlayerCommandRequest& command);
    bool submitLocalCommand(const multiplayer::PlayerCommandRequest& command);
    void drainRemotePlayerCommands(multiplayer::SimulationTick currentTick);
    void publishRemoteSnapshotIfDue(multiplayer::SimulationTick currentTick);
    // Validates every join request queued on remoteTransport_ since the last call (protocol
    // version, content manifest, capacity), registers accepted peers with the simulation, and
    // always replies with a JoinMatchResult -- accepted or rejected.
    void processIncomingJoinRequests();
    // Client-mode only: polls the pending join result once connected, adopting the host-assigned
    // player id on acceptance or recording a failure reason on rejection/decode error.
    void processRemoteJoinResult();
    // Client-mode only: merges a decoded MatchSnapshot into placedTowers_/activeEnemies_/
    // activeProjectiles_/gameplayState_ so the existing (host/single-player-oriented) render-sync
    // code renders remote state without any changes of its own. Preserves per-entity client-only
    // fields (e.g. animation timers) across snapshots by merging on runtime id rather than
    // wiping-and-replacing wholesale.
    void applyRemoteSnapshot(const multiplayer::DecodedMatchSnapshot& snapshot);
    // Client-mode only: locally advances walkAnimElapsedSeconds/deathElapsedSeconds every frame so
    // clips keep playing smoothly between snapshots. Purely cosmetic -- position/health/lifecycle
    // (the state that actually matters for gameplay) still comes entirely from the host's snapshot.
    void advanceClientCosmeticAnimations(float elapsedSeconds);
    void processLocalStartWaveCommand();
    bool processLocalTowerPlacementCommand(const TowerArchetype& archetype, const glm::vec3& worldPos);
    bool processLocalTowerUpgradeCommand(multiplayer::TowerRuntimeId towerRuntimeId, const std::string& nodeId);
    bool processLocalTowerTargetingCommand(multiplayer::TowerRuntimeId towerRuntimeId,
                         playlevel::TowerTargetingMode targetingMode);
    bool processLocalTowerSellCommand(multiplayer::TowerRuntimeId towerRuntimeId);
    void updateWaveSimulation(float dt);
    // One-time initialization: seeds every registered enemy archetype's own template animator
    // with its own Idle clip (only if that model has a clip by that name -- a no-op fallback
    // otherwise). Per-instance Walking and Death playback are both handled per-enemy, in
    // syncTowerInstanceTransforms(), since they must differ per enemy (and per archetype/template).
    void updateEnemyAnimationState();
    // Number of activeEnemies_ entries that are still Alive (i.e. excludes Dying/Dead corpses kept
    // around only so their Death clip can finish rendering). Wave-spawn throttling, the HUD's
    // enemiesAlive count, and next-wave/Victory gating must all use this -- not activeEnemies_.size()
    // or .empty() -- so a lingering corpse doesn't delay wave pacing or Victory.
    int countAliveEnemies() const;
    void reconcileSelectedEnemyAfterSimulation();
    void registerLuaGameplayApi();

    glm::mat4 buildViewMatrix() const;
    void updateCamera(float dt);
};
