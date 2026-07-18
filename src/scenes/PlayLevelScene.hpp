#pragma once
#include "scenes/GameScene.hpp"
#include "scenes/PlayLevelState.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <filesystem>
#include <memory>
#include <string>
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
      std::string modelPath = "assets/models/goblin1.glb";
      int health = 35;
      float moveSpeed = 2.8f;
      int rewardMoney = 8;
      float spawnIntervalSeconds = 0.9f;
      float defeatIntervalSeconds = 1.2f;
      int baseDamage = 5;
      float renderScale = 1.0f;
      float facingYawOffsetDegrees = 0.0f;
    };

    struct WaveDefinition {
      std::string enemyId = "goblin1";
      int count = 5;
      float spawnIntervalSeconds = 0.9f;
      int baseDamage = 5;
    };

    struct ActiveEnemy {
      float distanceAlongPath = 0.0f;
      float moveSpeed = 1.0f;
      int baseDamage = 1;
    };

    enum class GameplayCommandType {
      SpendMoney,
      DamageBase,
      StartWave
    };

    struct GameplayCommand {
      GameplayCommandType type;
      int amount;
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
    EnemyArchetype enemyArchetype_{};
    std::vector<WaveDefinition> waveDefinitions_;
    std::vector<ActiveEnemy> activeEnemies_;
    int activeWaveIndex_ = -1;
    std::filesystem::path selectedMapAssetPath_;
    std::string selectedWavesScriptPath_ = "assets/scenes/PlayLevelWaves.lua";
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

    bool requestSpendMoney(int amount);
    bool requestDamageBase(int amount);
    bool requestStartWave();
    bool loadLevelDefinition(SceneSharedState& state);
    bool loadEnemyArchetype(const std::string& scriptPath);
    bool loadWaveDefinitions(const std::string& scriptPath);
    bool updateRouteFromWorld();
    glm::vec3 sampleRoutePosition(float distanceAlongPath) const;
    float sampleRouteYaw(float distanceAlongPath) const;
    void syncEnemyInstanceTransforms();
    std::string validateStartWaveRequest() const;
    void applyPendingGameplayCommands();
    void updateWaveSimulation(float dt);
    void registerLuaGameplayApi();
    bool pickModelAtScreen(float screenX, float screenY, DebugSelection& outSelection) const;
    bool pickModelAtCursor(DebugSelection& outSelection) const;
    void updateDebugPickFromMouse();

    glm::mat4 buildViewMatrix() const;
    void updateCamera(float dt);
};
