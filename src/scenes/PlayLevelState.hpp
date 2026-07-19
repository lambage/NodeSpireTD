#pragma once

enum class MatchStatus {
    Running,
    Paused,
    Victory,
    Defeat
};

struct PlayLevelState {
    float baseHealth = 100;
    float playerMoney = 250;
    int currentWave = 1;
    bool waveInProgress = false;
    bool waveCountdownActive = false;
    int enemiesToSpawn = 0;
    int enemiesAlive = 0;
    int enemiesDefeated = 0;
    float spawnAccumulatorSeconds = 0.0f;
    float waveCountdownRemainingSeconds = 0.0f;
    float waveRoundDurationSeconds = 0.0f;
    float waveRoundRemainingSeconds = 0.0f;
    float defeatAccumulatorSeconds = 0.0f;
    MatchStatus matchStatus = MatchStatus::Running;

    void resetForNewRun() {
        baseHealth = 100.0f;
        playerMoney = 250.0f;
        currentWave = 1;
        waveInProgress = false;
        waveCountdownActive = false;
        enemiesToSpawn = 0;
        enemiesAlive = 0;
        enemiesDefeated = 0;
        spawnAccumulatorSeconds = 0.0f;
        waveCountdownRemainingSeconds = 0.0f;
        waveRoundDurationSeconds = 0.0f;
        waveRoundRemainingSeconds = 0.0f;
        defeatAccumulatorSeconds = 0.0f;
        matchStatus = MatchStatus::Running;
    }
};
