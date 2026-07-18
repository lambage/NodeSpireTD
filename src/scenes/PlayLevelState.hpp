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
    MatchStatus matchStatus = MatchStatus::Running;

    void resetForNewRun() {
        baseHealth = 100;
        playerMoney = 250;
        currentWave = 1;
        waveInProgress = false;
        matchStatus = MatchStatus::Running;
    }
};
