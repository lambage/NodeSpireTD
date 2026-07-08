#pragma once

#include "IAchievementService.hpp"

#include <string>
#include <unordered_set>

class LocalAchievementService : public IAchievementService {
public:
    bool unlockAchievement(std::string_view achievementId) override;

private:
    std::unordered_set<std::string> unlocked_;
};
