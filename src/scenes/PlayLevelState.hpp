#pragma once

enum class MatchStatus {
    Running,
    Paused,
    Victory,
    Defeat
};

struct PlayLevelState {
    int baseHealth = 100;
    int playerMoney = 250;
    int currentWave = 1;
    bool waveInProgress = false;
    int enemiesToSpawn = 0;
    int enemiesAlive = 0;
    int enemiesDefeated = 0;
    float spawnAccumulatorSeconds = 0.0f;
    float defeatAccumulatorSeconds = 0.0f;
    MatchStatus matchStatus = MatchStatus::Running;

    void resetForNewRun() {
        baseHealth = 100;
        playerMoney = 250;
        currentWave = 1;
        waveInProgress = false;
        enemiesToSpawn = 0;
        enemiesAlive = 0;
        enemiesDefeated = 0;
        spawnAccumulatorSeconds = 0.0f;
        defeatAccumulatorSeconds = 0.0f;
        matchStatus = MatchStatus::Running;
    }
};
