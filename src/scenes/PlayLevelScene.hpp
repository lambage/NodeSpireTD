#pragma once
#include "scenes/EnemyLoadController.hpp"
#include "scenes/GameScene.hpp"
#include "scenes/PlayLevelCameraController.hpp"
#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/PlayLevelPickingController.hpp"
#include "scenes/PlayLevelRouteController.hpp"
#include "scenes/PlayLevelState.hpp"
#include "scenes/PlayLevelTowerPlacementController.hpp"
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
    // Terrain sampling result from raycast
    struct TerrainSample {
        bool hit = false;
        glm::vec3 worldPosition{0.0f};
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
        float slope = 0.0f;  // angle in degrees from horizontal
        TowerPlacementRegionType towerPlacementType = TowerPlacementRegionType::Ground;  // defaults to ground
        bool onPath = false;
    };
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
    std::uint64_t nextEnemyRuntimeId_ = 1;
    PlayLevelWaveController waveController_{};
    std::vector<ActiveEnemy> activeEnemies_;
    std::filesystem::path selectedMapAssetPath_;
    std::filesystem::path selectedLevelScriptPath_;
    std::string selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
    WorldAssetSpec worldAssetSpec_{};
    PlayLevelRouteController routeController_{};
    std::vector<GameplayCommand> pendingCommands_;
    std::string loadStatus_;
    PlayLevelPickingController pickingController_{};
    std::uint64_t selectedEnemyRuntimeId_ = 0;
    std::vector<TowerPreviewPanel> towerPreviewPanels_;
    float towerPreviewSpinRadians_ = 0.0f;

    // Flying camera state
    PlayLevelCameraController cameraController_{};
    VkExtent2D lastRenderExtent_{};
    TowerLoadController towerLoadController_;
    EnemyLoadController enemyLoadController_;

    // Tower placement regions and validation
    std::vector<TowerPlacementRegion> placementRegions_;
    std::vector<glm::vec3> forbiddenPathZones_;
    float maxTowerPlacementSlopeDegrees_ = 30.0f;

    bool requestSpendMoney(float amount);
    bool requestDamageBase(float amount);
    bool requestStartWave();
    bool loadLevelDefinition(SceneSharedState& state);

    const TowerArchetype* selectedTowerArchetype() const;
    bool raycastGroundAtCursor(glm::vec3& outHit) const;
    TerrainSample sampleTerrainAtCursor() const;
    bool isPointInPlacementRegion(const glm::vec3& worldPos, const TowerPlacementRegion*& outRegion) const;
    bool isPointOnPath(const glm::vec3& worldPos) const;
    std::string validateTowerPlacement(const TowerArchetype& archetype, const glm::vec3& worldPos) const;
    void clearActiveSelectionForTowerPlacement(const char* reason);
    void updateTowerPlacementFromInput();
    glm::mat4 buildTowerModelTransform(const TowerArchetype& archetype, const glm::vec3& worldPos) const;
    void syncPlacedTowerModels();
    void syncTowerInstanceTransforms();
    void drawTowerPlacementOverlay() const;
    bool loadWaveDefinitions(const std::string& scriptPath);
    bool updateRouteFromWorld();
    glm::vec3 sampleRoutePosition(float distanceAlongPath) const;
    float sampleRouteYaw(float distanceAlongPath) const;
    void syncEnemyInstanceTransforms();
    std::string validateStartWaveRequest() const;
    void applyPendingGameplayCommands();
    void updateWaveSimulation(float dt);
    void reconcileSelectedEnemyAfterSimulation();
    void registerLuaGameplayApi();

    glm::mat4 buildViewMatrix() const;
    void updateCamera(float dt);
};
