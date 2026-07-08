#pragma once

#include "IAchievementService.hpp"
#include "ISaveSystem.hpp"

#include <memory>

class PlatformRuntime {
public:
    PlatformRuntime();
    ~PlatformRuntime();
    void tick();

    [[nodiscard]] IAchievementService& achievements();
    [[nodiscard]] ISaveSystem& saves();

private:
    std::unique_ptr<IAchievementService> achievementService_;
    std::unique_ptr<ISaveSystem> saveSystem_;
};
