#include "scenes/PlayLevelWaveController.hpp"

#include "lua.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>


namespace {

constexpr float kDefaultWaveRoundDurationSeconds = 30.0f;

} // namespace

void PlayLevelWaveController::clearAll() {
    waveDefinitions_.clear();
    resetRuntimeState();
}

void PlayLevelWaveController::clearDefinitions() {
    waveDefinitions_.clear();
}

void PlayLevelWaveController::resetRuntimeState() {
    activeWaveIndex_ = -1;
    activeWaveSpawnIndex_ = 0;
    activeWaveSpawnedFromCurrent_ = 0;
}

bool PlayLevelWaveController::hasDefinitions() const {
    return !waveDefinitions_.empty();
}

std::size_t PlayLevelWaveController::waveCount() const {
    return waveDefinitions_.size();
}

const std::vector<PlayLevelWaveController::WaveDefinition>& PlayLevelWaveController::definitions() const {
    return waveDefinitions_;
}

std::vector<PlayLevelWaveController::WaveDefinition>& PlayLevelWaveController::definitionsMutable() {
    return waveDefinitions_;
}

bool PlayLevelWaveController::parseWaveSpawnEntry(lua_State* L, int spawnTableIdx, const std::string& defaultEnemyId,
                                                  const ResolveEnemyDefaultsFn& resolveEnemyDefaults,
                                                  WaveSpawnDefinition& outSpawn) {
    outSpawn.enemyId = defaultEnemyId;
    if (const std::optional<EnemyWaveDefaults> defaults = resolveEnemyDefaults(outSpawn.enemyId)) {
        outSpawn.spawnIntervalSeconds = defaults->spawnIntervalSeconds;
    }

    lua_getfield(L, spawnTableIdx, "entity");
    if (lua_isstring(L, -1)) {
        outSpawn.enemyId = lua_tostring(L, -1);
    } else if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "id");
        if (lua_isstring(L, -1)) {
            outSpawn.enemyId = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, spawnTableIdx, "enemyId");
    if (lua_isstring(L, -1)) {
        outSpawn.enemyId = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    std::optional<EnemyWaveDefaults> defaults = resolveEnemyDefaults(outSpawn.enemyId);
    if (!defaults) {
        spdlog::warn("PlayLevelWaveController: wave spawn references unknown enemy '{}', using '{}' fallback.",
                     outSpawn.enemyId, defaultEnemyId);
        outSpawn.enemyId = defaultEnemyId;
        defaults = resolveEnemyDefaults(outSpawn.enemyId);
    }

    lua_getfield(L, spawnTableIdx, "count");
    if (lua_isinteger(L, -1)) {
        outSpawn.count = static_cast<int>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, spawnTableIdx, "spawnIntervalSeconds");
    if (lua_isnumber(L, -1)) {
        outSpawn.spawnIntervalSeconds = static_cast<float>(lua_tonumber(L, -1));
    } else if (defaults) {
        outSpawn.spawnIntervalSeconds = defaults->spawnIntervalSeconds;
    }
    lua_pop(L, 1);

    outSpawn.count = std::max(1, outSpawn.count);
    outSpawn.spawnIntervalSeconds = std::max(0.05f, outSpawn.spawnIntervalSeconds);
    return true;
}

bool PlayLevelWaveController::loadWaveDefinitions(lua_State* L, const std::string& scriptPath,
                                                  const std::string& defaultEnemyId,
                                                  const ResolveEnemyDefaultsFn& resolveEnemyDefaults) {
    waveDefinitions_.clear();
    if (!L) {
        return false;
    }

    if (luaL_loadfile(L, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("PlayLevelWaveController: failed to load wave script {}: {}", scriptPath, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        spdlog::error("PlayLevelWaveController: wave script execution error {}: {}", scriptPath, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (!lua_istable(L, -1)) {
        spdlog::error("PlayLevelWaveController: wave script must return a table: {}", scriptPath);
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, "onLoad");
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            spdlog::error("PlayLevelWaveController: wave onLoad() execution error {}: {}", scriptPath,
                          lua_tostring(L, -1));
            lua_pop(L, 2);
            return false;
        }
    } else {
        lua_pop(L, 1);
    }

    lua_getfield(L, -1, "waves");
    if (waveDefinitions_.empty() && lua_istable(L, -1)) {
        const int waveTable = lua_gettop(L);
        const int waveCount = static_cast<int>(lua_rawlen(L, waveTable));
        for (int i = 1; i <= waveCount; ++i) {
            lua_geti(L, waveTable, i);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                continue;
            }

            WaveDefinition waveDef;

            lua_getfield(L, -1, "roundDurationSeconds");
            if (lua_isnumber(L, -1)) {
                waveDef.roundDurationSeconds = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "spawns");
            if (lua_istable(L, -1)) {
                const int spawnsTable = lua_gettop(L);
                const int spawnCount = static_cast<int>(lua_rawlen(L, spawnsTable));
                for (int spawnIdx = 1; spawnIdx <= spawnCount; ++spawnIdx) {
                    lua_geti(L, spawnsTable, spawnIdx);
                    if (!lua_istable(L, -1)) {
                        lua_pop(L, 1);
                        continue;
                    }
                    WaveSpawnDefinition spawnDef;
                    parseWaveSpawnEntry(L, lua_gettop(L), defaultEnemyId, resolveEnemyDefaults, spawnDef);
                    waveDef.spawns.push_back(std::move(spawnDef));
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);

            if (waveDef.spawns.empty()) {
                WaveSpawnDefinition legacySpawn;
                parseWaveSpawnEntry(L, lua_gettop(L), defaultEnemyId, resolveEnemyDefaults, legacySpawn);
                waveDef.spawns.push_back(std::move(legacySpawn));
            }

            waveDef.roundDurationSeconds = std::max(1.0f, waveDef.roundDurationSeconds);
            waveDefinitions_.push_back(std::move(waveDef));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_pop(L, 1);

    if (waveDefinitions_.empty()) {
        WaveDefinition fallback;
        fallback.spawns.push_back(WaveSpawnDefinition{});
        waveDefinitions_.push_back(std::move(fallback));
    }

    return true;
}

bool PlayLevelWaveController::registerWaveFromLua(lua_State* L, int waveTableIndex, const std::string& defaultEnemyId,
                                                  const ResolveEnemyDefaultsFn& resolveEnemyDefaults,
                                                  float overrideRoundDurationSeconds, std::string& outError) {
    if (!L || !lua_istable(L, waveTableIndex)) {
        outError = "Wave.Register requires a table argument";
        return false;
    }

    WaveDefinition waveDef;
    int spawnEntriesTable = waveTableIndex;

    lua_getfield(L, waveTableIndex, "spawns");
    if (lua_istable(L, -1)) {
        spawnEntriesTable = lua_gettop(L);
    } else {
        lua_pop(L, 1);
    }

    lua_getfield(L, waveTableIndex, "roundDurationSeconds");
    if (lua_isnumber(L, -1)) {
        waveDef.roundDurationSeconds = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    if (overrideRoundDurationSeconds > 0.0f) {
        waveDef.roundDurationSeconds = overrideRoundDurationSeconds;
    }

    const int entryCount = static_cast<int>(lua_rawlen(L, spawnEntriesTable));
    for (int i = 1; i <= entryCount; ++i) {
        lua_geti(L, spawnEntriesTable, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        WaveSpawnDefinition spawn;
        parseWaveSpawnEntry(L, lua_gettop(L), defaultEnemyId, resolveEnemyDefaults, spawn);
        waveDef.spawns.push_back(std::move(spawn));
        lua_pop(L, 1);
    }

    if (spawnEntriesTable != waveTableIndex) {
        lua_pop(L, 1);
    }

    if (waveDef.spawns.empty()) {
        outError = "Wave.Register requires at least one spawn entry";
        return false;
    }

    waveDef.roundDurationSeconds = std::max(1.0f, waveDef.roundDurationSeconds);
    waveDefinitions_.push_back(std::move(waveDef));
    return true;
}

std::string PlayLevelWaveController::validateStartWaveRequest(const PlayLevelState& gameplayState, bool worldLoaded,
                                                              bool hasAnimatedEntityTemplate, bool hasRoute) const {
    if (gameplayState.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }
    if (gameplayState.waveCountdownActive) {
        return "countdown already active";
    }
    if (gameplayState.waveInProgress) {
        return "wave is still spawning";
    }
    if (!worldLoaded) {
        return "world is still loading";
    }
    if (!hasAnimatedEntityTemplate) {
        return "enemy model template not loaded";
    }
    if (!hasRoute) {
        return "path markers not found (need Start/Waypoint_X/End)";
    }
    if (waveDefinitions_.empty()) {
        return "no wave definitions loaded";
    }
    if (gameplayState.currentWave < 1 || gameplayState.currentWave > static_cast<int>(waveDefinitions_.size())) {
        return "no definition for current wave";
    }
    if (waveDefinitions_[gameplayState.currentWave - 1].spawns.empty()) {
        return "current wave has no spawn entries";
    }
    return {};
}

bool PlayLevelWaveController::beginWaveCountdown(PlayLevelState& gameplayState, bool worldLoaded,
                                                 bool hasAnimatedEntityTemplate, bool hasRoute) {
    if (!validateStartWaveRequest(gameplayState, worldLoaded, hasAnimatedEntityTemplate, hasRoute).empty()) {
        return false;
    }

    gameplayState.waveCountdownActive = true;
    gameplayState.waveCountdownRemainingSeconds = gameplayState.waveCountdownDurationSeconds;
    return true;
}

bool PlayLevelWaveController::beginWaveSpawning(PlayLevelState& gameplayState, int enemiesAliveCount) {
    if (gameplayState.currentWave < 1 || gameplayState.currentWave > static_cast<int>(waveDefinitions_.size())) {
        return false;
    }

    activeWaveIndex_ = std::clamp(gameplayState.currentWave - 1, 0, static_cast<int>(waveDefinitions_.size()) - 1);
    const WaveDefinition& waveDef = waveDefinitions_[activeWaveIndex_];

    int totalToSpawn = 0;
    for (const WaveSpawnDefinition& spawn : waveDef.spawns) {
        totalToSpawn += std::max(1, spawn.count);
    }

    gameplayState.enemiesToSpawn = totalToSpawn;
    gameplayState.enemiesAlive = enemiesAliveCount;
    gameplayState.enemiesDefeated = 0;
    gameplayState.spawnAccumulatorSeconds = 0.0f;
    gameplayState.defeatAccumulatorSeconds = 0.0f;
    gameplayState.waveRoundDurationSeconds =
        (waveDef.roundDurationSeconds > 0.0f) ? waveDef.roundDurationSeconds : kDefaultWaveRoundDurationSeconds;
    gameplayState.waveRoundRemainingSeconds = gameplayState.waveRoundDurationSeconds;
    gameplayState.waveCountdownActive = false;
    gameplayState.waveCountdownRemainingSeconds = 0.0f;
    activeWaveSpawnIndex_ = 0;
    activeWaveSpawnedFromCurrent_ = 0;
    gameplayState.waveInProgress = true;
    return true;
}

void PlayLevelWaveController::completeWaveAndAdvance(PlayLevelState& gameplayState) {
    const bool hasNextWave = gameplayState.currentWave < static_cast<int>(waveDefinitions_.size());

    gameplayState.waveInProgress = false;
    gameplayState.waveCountdownActive = hasNextWave;
    gameplayState.waveCountdownRemainingSeconds = hasNextWave ? gameplayState.waveCountdownDurationSeconds : 0.0f;
    gameplayState.waveRoundDurationSeconds = 0.0f;
    gameplayState.waveRoundRemainingSeconds = 0.0f;
    gameplayState.enemiesToSpawn = 0;
    gameplayState.spawnAccumulatorSeconds = 0.0f;
    resetRuntimeState();

    if (gameplayState.currentWave <= static_cast<int>(waveDefinitions_.size())) {
        gameplayState.currentWave += 1;
    }
}

void PlayLevelWaveController::updateWaveSpawning(PlayLevelState& gameplayState, float dt, int enemiesAliveCount,
                                                 const SpawnEnemyFn& spawnEnemy) {
    if (gameplayState.matchStatus != MatchStatus::Running) {
        return;
    }

    if (gameplayState.waveCountdownActive) {
        gameplayState.waveCountdownRemainingSeconds = std::max(0.0f, gameplayState.waveCountdownRemainingSeconds - dt);
        if (gameplayState.waveCountdownRemainingSeconds <= 0.0f) {
            beginWaveSpawning(gameplayState, enemiesAliveCount);
        }
    }

    if (gameplayState.waveInProgress) {
        if (activeWaveIndex_ < 0 || activeWaveIndex_ >= static_cast<int>(waveDefinitions_.size())) {
            completeWaveAndAdvance(gameplayState);
        } else {
            const WaveDefinition& waveDef = waveDefinitions_[activeWaveIndex_];
            if (waveDef.spawns.empty()) {
                completeWaveAndAdvance(gameplayState);
            } else {
                gameplayState.waveRoundRemainingSeconds = std::max(0.0f, gameplayState.waveRoundRemainingSeconds - dt);
                gameplayState.spawnAccumulatorSeconds += dt;

                while (gameplayState.enemiesToSpawn > 0 &&
                       activeWaveSpawnIndex_ < static_cast<int>(waveDef.spawns.size())) {
                    const WaveSpawnDefinition& spawnDef = waveDef.spawns[activeWaveSpawnIndex_];
                    if (gameplayState.spawnAccumulatorSeconds < spawnDef.spawnIntervalSeconds) {
                        break;
                    }

                    gameplayState.spawnAccumulatorSeconds -= spawnDef.spawnIntervalSeconds;
                    gameplayState.enemiesToSpawn -= 1;
                    spawnEnemy(spawnDef.enemyId);

                    ++activeWaveSpawnedFromCurrent_;
                    if (activeWaveSpawnedFromCurrent_ >= std::max(1, spawnDef.count)) {
                        activeWaveSpawnedFromCurrent_ = 0;
                        ++activeWaveSpawnIndex_;
                    }
                }

                const bool waveFullySpawned = gameplayState.enemiesToSpawn == 0;
                const bool noEnemiesAlive = enemiesAliveCount <= 0;
                if ((waveFullySpawned && noEnemiesAlive) || gameplayState.waveRoundRemainingSeconds <= 0.0f) {
                    completeWaveAndAdvance(gameplayState);
                }
            }
        }
    }
}
