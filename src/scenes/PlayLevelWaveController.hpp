#pragma once

#include "scenes/PlayLevelState.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct lua_State;

class PlayLevelWaveController {
  public:
    struct WaveSpawnDefinition {
      std::string enemyId = "goblin1";
      int count = 5;
      float spawnIntervalSeconds = 0.9f;
    };

    struct WaveDefinition {
      std::vector<WaveSpawnDefinition> spawns;
      float roundDurationSeconds = 30.0f;
    };

    struct EnemyWaveDefaults {
      float spawnIntervalSeconds = 0.9f;
    };

    using ResolveEnemyDefaultsFn = std::function<std::optional<EnemyWaveDefaults>(const std::string& enemyId)>;
    using SpawnEnemyFn = std::function<void(const std::string& enemyId)>;

    void clearAll();
    void clearDefinitions();
    void resetRuntimeState();

    bool hasDefinitions() const;
    std::size_t waveCount() const;

    const std::vector<WaveDefinition>& definitions() const;
    std::vector<WaveDefinition>& definitionsMutable();

    bool loadWaveDefinitions(lua_State* L,
                             const std::string& scriptPath,
                             const std::string& defaultEnemyId,
                             const ResolveEnemyDefaultsFn& resolveEnemyDefaults);

    bool registerWaveFromLua(lua_State* L,
                             int waveTableIndex,
                             const std::string& defaultEnemyId,
                             const ResolveEnemyDefaultsFn& resolveEnemyDefaults,
                             float overrideRoundDurationSeconds,
                             std::string& outError);

    std::string validateStartWaveRequest(const PlayLevelState& gameplayState,
                                         bool worldLoaded,
                                         bool hasAnimatedEntityTemplate,
                                         bool hasRoute) const;

    bool beginWaveCountdown(PlayLevelState& gameplayState,
                            bool worldLoaded,
                            bool hasAnimatedEntityTemplate,
                            bool hasRoute);

    bool beginWaveSpawning(PlayLevelState& gameplayState, int enemiesAliveCount);
    void completeWaveAndAdvance(PlayLevelState& gameplayState);

    void updateWaveSpawning(PlayLevelState& gameplayState,
                            float dt,
                            int enemiesAliveCount,
                            const SpawnEnemyFn& spawnEnemy);

  private:
    bool parseWaveSpawnEntry(lua_State* L,
                             int spawnTableIdx,
                             const std::string& defaultEnemyId,
                             const ResolveEnemyDefaultsFn& resolveEnemyDefaults,
                             WaveSpawnDefinition& outSpawn);

    std::vector<WaveDefinition> waveDefinitions_;
    int activeWaveIndex_ = -1;
    int activeWaveSpawnIndex_ = 0;
    int activeWaveSpawnedFromCurrent_ = 0;
};
