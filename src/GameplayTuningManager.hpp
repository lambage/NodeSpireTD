#pragma once

#include <filesystem>

struct GameplayTuningConfig {
    float enemyHealthMultiplier = 1.0f;
    float enemySpeedMultiplier = 1.0f;
    float enemyRewardMultiplier = 1.0f;
    float enemyBaseDamageMultiplier = 1.0f;
    float hostMoneyOverride = -1.0f;
    float baseHealthOverride = -1.0f;
    float waveCountdownSecondsOverride = -1.0f;
};

class GameplayTuningManager {
  public:
    GameplayTuningManager(std::filesystem::path tuningFilePath = "config/gameplay_tuning.json",
                          std::filesystem::path devtoolsFilePath = "config/devtools.json");

    GameplayTuningConfig loadOrCreateDefaults() const;
    bool save(const GameplayTuningConfig& tuning) const;

    bool isRuntimeTuningEnabled() const;
    const std::filesystem::path& tuningFilePath() const;

  private:
    std::filesystem::path tuningFilePath_;
    std::filesystem::path devtoolsFilePath_;
};
