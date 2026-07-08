#pragma once

class IAchievementService;
class ISaveSystem;

struct SceneContext {
    IAchievementService* achievementService = nullptr;
    ISaveSystem* saveSystem = nullptr;
};
