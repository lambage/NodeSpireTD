#include "PlatformRuntime.hpp"

#include "LocalAchievementService.hpp"
#include "LocalFileSaveSystem.hpp"

#include <spdlog/spdlog.h>

PlatformRuntime::PlatformRuntime() {
    achievementService_ = std::make_unique<LocalAchievementService>();
    saveSystem_ = std::make_unique<LocalFileSaveSystem>();

    spdlog::info("Platform runtime initialized with local services.");
}

PlatformRuntime::~PlatformRuntime() {
    saveSystem_.reset();
    achievementService_.reset();
    spdlog::info("Platform runtime shutdown complete.");
}

void PlatformRuntime::tick() {
    // Reserved for per-frame platform callbacks (e.g. SteamAPI_RunCallbacks()).
}

IAchievementService& PlatformRuntime::achievements() {
    return *achievementService_;
}

ISaveSystem& PlatformRuntime::saves() {
    return *saveSystem_;
}
