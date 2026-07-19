#pragma once
#include "scenes/GameScene.hpp"
#include "scenes/PlayLevelState.hpp"
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

    struct WaveSpawnDefinition {
      std::string enemyId = "goblin1";
      int count = 5;
      float spawnIntervalSeconds = 0.9f;
    };

    struct WaveDefinition {
      std::vector<WaveSpawnDefinition> spawns;
      float roundDurationSeconds = 30.0f;
    };

    struct ActiveEnemy {
      std::string enemyId = "goblin1";
      std::uint64_t runtimeId = 0;
      float distanceAlongPath = 0.0f;
      float health = 1.0f;
      float moveSpeed = 1.0f;
      float rewardMoney = 0.0f;
      float baseDamage = 1.0f;
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

    struct PlacedTower {
      std::string towerId;
      glm::vec3 position{0.0f};
      float attackDamage = 1.0f;
      float attackRange = 0.0f;
      float attackIntervalSeconds = 1.0f;
      float attackCooldownRemainingSeconds = 0.0f;
      float projectileSpeed = 16.0f;
      int cost = 0;
    };

    struct ActiveProjectile {
      std::string towerId;
      glm::vec3 position{0.0f};
      glm::vec3 velocity{0.0f};
      float damage = 1.0f;
      float remainingLifeSeconds = 0.0f;
      std::uint64_t targetEnemyRuntimeId = 0;
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

    struct DebugSelection {
      bool valid = false;
      std::string group;
      std::string label;
      int meshIndex = -1;
      int nodeIndex = -1;
      int skinIndex = -1;
      int instanceIndex = -1;
      float distance = 0.0f;
      glm::vec3 hitPosition{0.0f};
      glm::vec3 hitNormal{0.0f, 1.0f, 0.0f};
    };

    struct DebugOverlayStats {
      int sphereTotal = 0;
      int sphereDrawn = 0;
      int rejectBehindCamera = 0;
      int rejectClipW = 0;
      int rejectNdcZ = 0;
      int rejectRadius = 0;
      int hoveredSphereFound = 0;
      int hoveredRejectReason = 0;
      float hoveredDepth = 0.0f;
      float hoveredRadiusPixels = 0.0f;
      float displayWidth = 0.0f;
      float displayHeight = 0.0f;
      float renderWidth = 0.0f;
      float renderHeight = 0.0f;
    };

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
    int selectedTowerLoadoutIndex_ = -1;
    std::vector<PlacedTower> placedTowers_;
    std::vector<ActiveProjectile> activeProjectiles_;
    std::uint64_t nextEnemyRuntimeId_ = 1;
    bool towerPlacementHasHit_ = false;
    bool towerPlacementCanPlace_ = false;
    glm::vec3 towerPlacementWorldPos_{0.0f};
    std::vector<WaveDefinition> waveDefinitions_;
    std::vector<ActiveEnemy> activeEnemies_;
    int activeWaveIndex_ = -1;
    int activeWaveSpawnIndex_ = 0;
    int activeWaveSpawnedFromCurrent_ = 0;
    std::filesystem::path selectedMapAssetPath_;
    std::filesystem::path selectedLevelScriptPath_;
    std::string selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
    WorldAssetSpec worldAssetSpec_{};
    std::vector<glm::vec3> routePoints_;
    std::vector<float> routeSegmentLengths_;
    float routeTotalLength_ = 0.0f;
    std::vector<GameplayCommand> pendingCommands_;
    std::string loadStatus_;
    bool debugPickEnabled_ = true;
    bool debugDrawPickSpheres_ = false;
    bool debugDrawHoverHighlight_ = false;
    DebugSelection debugSelection_{};
    DebugSelection hoverSelection_{};
    int hoveredInstanceIndex_ = -1;
    int selectedInstanceIndex_ = -1;
    std::string debugPickStatus_;

    // Flying camera state
    glm::vec3 camPos_{0.0f, 5.0f, 20.0f};
    float camYaw_   = 3.14159f;
    float camPitch_ = -0.25f;
    VkExtent2D lastRenderExtent_{};
    mutable DebugOverlayStats debugOverlayStats_{};

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
    bool beginWaveCountdown();
    bool beginWaveSpawning();
    void completeWaveAndAdvance();
    void applyPendingGameplayCommands();
    void updateWaveSimulation(float dt);
    void registerLuaGameplayApi();
    bool pickModelAtScreen(float screenX, float screenY, DebugSelection& outSelection) const;
    bool pickModelAtCursor(DebugSelection& outSelection) const;
    bool updateDebugHoverFromMouse();
    void updateDebugPickFromMouse();
    void drawDebugPickSpheresOverlay() const;
    void drawHoverHighlightOverlay() const;

    glm::mat4 buildViewMatrix() const;
    void updateCamera(float dt);
};
