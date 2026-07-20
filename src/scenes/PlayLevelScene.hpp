#pragma once
#include "scenes/PlayLevelCameraController.hpp"
#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/GameScene.hpp"
#include "scenes/PlayLevelPickingController.hpp"
#include "scenes/PlayLevelState.hpp"
#include "scenes/PlayLevelRouteController.hpp"
#include "scenes/PlayLevelTowerPlacementController.hpp"
#include "scenes/PlayLevelWaveController.hpp"
#include "utility/WorldAssetLoader.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class WorldRenderer;

class PlayLevelScene final : public GameScene {
  public:
    PlayLevelScene();
    ~PlayLevelScene() override;

    void onEnter(SceneSharedState&) override;
    void onExit(SceneSharedState&) override;

    void render(SceneSharedState& state, float dt) override;
    void renderWorld(VkCommandBuffer cmd, VkExtent2D extent) override;

  private:
    struct EnemyArchetype {
      std::string id = "goblin1";
      std::string displayName = "Goblin";
      std::string modelPath = "assets/models/enemy/goblin1.glb";
      float health = 35.0f;
      float moveSpeed = 2.8f;
      float rewardMoney = 8.0f;
      float spawnIntervalSeconds = 0.9f;
      float defeatIntervalSeconds = 1.2f;
      float baseDamage = 5.0f;
      float renderScale = 1.0f;
      float facingYawOffsetDegrees = 0.0f;
    };

    struct TowerArchetype {
      std::string id = "tower";
      std::string displayName = "Tower";
      std::string modelPath;
      std::string projectileModelPath;
      std::string previewImagePath;
      int cost = 100;
      float attackDamage = 1.0f;
      float attackRange = 5.0f;
      float attackSpeed = 1.0f;
      float projectileSpeed = 16.0f;
      float renderScale = 1.0f;
      float facingYawOffsetDegrees = 0.0f;
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
    std::unordered_map<std::string, EnemyArchetype> enemyArchetypes_;
    std::string defaultEnemyId_ = "goblin1";
    std::unordered_map<std::string, TowerArchetype> towerArchetypes_;
    std::unordered_map<std::string, int> towerTemplatePrototypeById_;
    std::unordered_map<std::string, int> projectileTemplatePrototypeByTowerId_;
    std::vector<std::string> towerLoadoutIds_;
    std::unordered_map<std::string, std::vector<std::string>> towerPoolGroupsById_;
    std::unordered_map<std::string, std::string> towerGhostGroupById_;
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

    // Flying camera state
    PlayLevelCameraController cameraController_{};
    VkExtent2D lastRenderExtent_{};

    bool requestSpendMoney(float amount);
    bool requestDamageBase(float amount);
    bool requestStartWave();
    bool loadLevelDefinition(SceneSharedState& state);
    bool loadEnemyArchetype(const std::string& scriptPath);
    bool parseEnemyArchetypeScript(const std::string& scriptPath, EnemyArchetype& outArchetype);
    bool parseTowerArchetypeScript(const std::string& scriptPath, TowerArchetype& outArchetype);
    bool loadTowerArchetype(const std::string& scriptPath);
    void discoverTowerArchetypes();
    const TowerArchetype* findTowerArchetype(const std::string& towerId) const;
    const TowerArchetype* selectedTowerArchetype() const;
    bool raycastGroundAtCursor(glm::vec3& outHit) const;
    std::string validateTowerPlacement(const TowerArchetype& archetype, const glm::vec3& worldPos) const;
    void updateTowerPlacementFromInput();
    glm::mat4 buildTowerModelTransform(const TowerArchetype& archetype, const glm::vec3& worldPos) const;
    void syncPlacedTowerModels();
    void syncTowerInstanceTransforms();
    void drawTowerPlacementOverlay() const;
    bool loadWaveDefinitions(const std::string& scriptPath);
    const EnemyArchetype* findEnemyArchetype(const std::string& enemyId) const;
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
