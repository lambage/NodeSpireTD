#pragma once
#include "scenes/GameScene.hpp"
#include "scenes/PlayLevelState.hpp"
#include "utility/WorldAssetLoader.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <filesystem>
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

    SceneFrameResult render(SceneSharedState& state, float dt) override;
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
      float distanceAlongPath = 0.0f;
      float moveSpeed = 1.0f;
      float baseDamage = 1.0f;
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

    struct DebugSelection {
      bool valid = false;
      std::string group;
      std::string label;
      int meshIndex = -1;
      int nodeIndex = -1;
      int skinIndex = -1;
      int enemyInstanceIndex = -1;
      float distance = 0.0f;
      glm::vec3 hitPosition{0.0f};
      glm::vec3 hitNormal{0.0f, 1.0f, 0.0f};
    };

    std::unique_ptr<WorldRenderer> worldRenderer_;
    PlayLevelState gameplayState_{};
    std::unordered_map<std::string, EnemyArchetype> enemyArchetypes_;
    std::string defaultEnemyId_ = "goblin1";
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
    DebugSelection debugSelection_{};
    std::string debugPickStatus_;

    // Flying camera state
    glm::vec3 camPos_{0.0f, 5.0f, 20.0f};
    float camYaw_   = 3.14159f;
    float camPitch_ = -0.25f;

    bool requestSpendMoney(float amount);
    bool requestDamageBase(float amount);
    bool requestStartWave();
    bool loadLevelDefinition(SceneSharedState& state);
    bool loadEnemyArchetype(const std::string& scriptPath);
    bool parseEnemyArchetypeScript(const std::string& scriptPath, EnemyArchetype& outArchetype);
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
    void updateDebugPickFromMouse();

    glm::mat4 buildViewMatrix() const;
    void updateCamera(float dt);
};
