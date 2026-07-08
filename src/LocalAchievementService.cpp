#include "LocalAchievementService.hpp"

#include <spdlog/spdlog.h>

bool LocalAchievementService::unlockAchievement(std::string_view achievementId) {
    if (achievementId.empty()) {
        return false;
    }

    const auto [_, inserted] = unlocked_.insert(std::string(achievementId));
    if (inserted) {
        spdlog::info("[Achievement Unlocked: {}]", achievementId);
    }

    return inserted;
}
