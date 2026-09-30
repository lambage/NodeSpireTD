#include "GameplayTuningManager.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {

GameplayTuningConfig sanitize(const GameplayTuningConfig& input) {
    GameplayTuningConfig result = input;
    result.enemyHealthMultiplier = std::clamp(result.enemyHealthMultiplier, 0.05f, 20.0f);
    result.enemySpeedMultiplier = std::clamp(result.enemySpeedMultiplier, 0.05f, 20.0f);
    result.enemyRewardMultiplier = std::clamp(result.enemyRewardMultiplier, 0.0f, 20.0f);
    result.enemyBaseDamageMultiplier = std::clamp(result.enemyBaseDamageMultiplier, 0.0f, 20.0f);
    if (result.hostMoneyOverride < 0.0f) result.hostMoneyOverride = -1.0f;
    if (result.baseHealthOverride < 0.0f) result.baseHealthOverride = -1.0f;
    if (result.waveCountdownSecondsOverride < 0.0f) result.waveCountdownSecondsOverride = -1.0f;
    return result;
}

nlohmann::json tuningToJson(const GameplayTuningConfig& tuning) {
    return {
        {"enemyHealthMultiplier", tuning.enemyHealthMultiplier},
        {"enemySpeedMultiplier", tuning.enemySpeedMultiplier},
        {"enemyRewardMultiplier", tuning.enemyRewardMultiplier},
        {"enemyBaseDamageMultiplier", tuning.enemyBaseDamageMultiplier},
        {"hostMoneyOverride", tuning.hostMoneyOverride >= 0.0f ? nlohmann::json(tuning.hostMoneyOverride)
                                                                  : nlohmann::json(nullptr)},
        {"baseHealthOverride", tuning.baseHealthOverride >= 0.0f ? nlohmann::json(tuning.baseHealthOverride)
                                                                    : nlohmann::json(nullptr)},
        {"waveCountdownSecondsOverride",
         tuning.waveCountdownSecondsOverride >= 0.0f ? nlohmann::json(tuning.waveCountdownSecondsOverride)
                                                     : nlohmann::json(nullptr)},
    };
}

GameplayTuningConfig tuningFromJson(const nlohmann::json& json) {
    GameplayTuningConfig tuning;

    tuning.enemyHealthMultiplier = json.value("enemyHealthMultiplier", tuning.enemyHealthMultiplier);
    tuning.enemySpeedMultiplier = json.value("enemySpeedMultiplier", tuning.enemySpeedMultiplier);
    tuning.enemyRewardMultiplier = json.value("enemyRewardMultiplier", tuning.enemyRewardMultiplier);
    tuning.enemyBaseDamageMultiplier = json.value("enemyBaseDamageMultiplier", tuning.enemyBaseDamageMultiplier);

    if (json.contains("hostMoneyOverride") && json["hostMoneyOverride"].is_number()) {
        tuning.hostMoneyOverride = json["hostMoneyOverride"].get<float>();
    }
    if (json.contains("baseHealthOverride") && json["baseHealthOverride"].is_number()) {
        tuning.baseHealthOverride = json["baseHealthOverride"].get<float>();
    }
    if (json.contains("waveCountdownSecondsOverride") && json["waveCountdownSecondsOverride"].is_number()) {
        tuning.waveCountdownSecondsOverride = json["waveCountdownSecondsOverride"].get<float>();
    }

    return sanitize(tuning);
}

} // namespace

GameplayTuningManager::GameplayTuningManager(std::filesystem::path tuningFilePath,
                                             std::filesystem::path devtoolsFilePath)
    : tuningFilePath_(std::move(tuningFilePath)), devtoolsFilePath_(std::move(devtoolsFilePath)) {}

GameplayTuningConfig GameplayTuningManager::loadOrCreateDefaults() const {
    const GameplayTuningConfig defaults;

    if (!std::filesystem::exists(tuningFilePath_)) {
        if (!save(defaults)) {
            spdlog::warn("Failed to create gameplay tuning file at {}", tuningFilePath_.string());
        }
        return defaults;
    }

    std::ifstream input(tuningFilePath_);
    if (!input.is_open()) {
        spdlog::warn("Failed to open gameplay tuning file: {}", tuningFilePath_.string());
        return defaults;
    }

    try {
        nlohmann::json json;
        input >> json;
        return tuningFromJson(json);
    } catch (const std::exception& ex) {
        spdlog::warn("Gameplay tuning parse failed ({}). Using defaults.", ex.what());
        return defaults;
    }
}

bool GameplayTuningManager::save(const GameplayTuningConfig& tuning) const {
    try {
        std::filesystem::create_directories(tuningFilePath_.parent_path());

        std::ofstream output(tuningFilePath_, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << tuningToJson(sanitize(tuning)).dump(4);
        output << '\n';
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save gameplay tuning ({}).", ex.what());
        return false;
    }
}

bool GameplayTuningManager::isRuntimeTuningEnabled() const {
    if (!std::filesystem::exists(devtoolsFilePath_)) {
        return false;
    }

    std::ifstream input(devtoolsFilePath_);
    if (!input.is_open()) {
        return false;
    }

    try {
        nlohmann::json json;
        input >> json;
        return json.value("enableGameplayMcpTuning", false);
    } catch (const std::exception&) {
        return false;
    }
}

const std::filesystem::path& GameplayTuningManager::tuningFilePath() const {
    return tuningFilePath_;
}
