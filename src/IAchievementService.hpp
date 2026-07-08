#pragma once

#include <string_view>

class IAchievementService {
public:
    virtual ~IAchievementService() = default;

    virtual bool unlockAchievement(std::string_view achievementId) = 0;
};
