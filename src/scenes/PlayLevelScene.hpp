#pragma once
#include "multiplayer/LocalMatchHost.hpp"
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
    PlayLevelState gameplayState_{};
    PlayLevelTowerPlacementController towerPlacementController_{};
    PlayLevelCombatController combatController_{};
    std::vector<PlacedTower> placedTowers_;
    std::vector<ActiveProjectile> activeProjectiles_;
    std::uint64_t nextTowerRuntimeId_ = 1;
    std::uint64_t nextEnemyRuntimeId_ = 1;
    PlayLevelWaveController waveController_{};
    std::vector<ActiveEnemy> activeEnemies_;
    std::filesystem::path selectedMapAssetPath_;
    std::filesystem::path selectedLevelScriptPath_;
    std::string selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
    WorldAssetSpec worldAssetSpec_{};
    PlayLevelRouteController routeController_{};
    std::vector<GameplayCommand> pendingCommands_;
    multiplayer::LocalMatchHost localMatchHost_{};
    multiplayer::CommandSequence nextLocalCommandSequence_ = 1;
    multiplayer::SimulationTick simulationTick_ = 0;
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
                       int footprintSampleCount = 8) const;
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
                         const std::string& nodeId) const;
    bool unlockTowerUpgrade(PlacedTower& placedTower, const std::string& nodeId, std::string& outReason);
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
    void processLocalStartWaveCommand();
    bool processLocalTowerPlacementCommand(const TowerArchetype& archetype, const glm::vec3& worldPos);
    bool processLocalTowerUpgradeCommand(multiplayer::TowerRuntimeId towerRuntimeId, const std::string& nodeId);
    bool processLocalTowerTargetingCommand(multiplayer::TowerRuntimeId towerRuntimeId,
                         playlevel::TowerTargetingMode targetingMode);
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
