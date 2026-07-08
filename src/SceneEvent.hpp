#pragma once

#include <string>

enum class SceneEventType {
    None,
    RequestLoadScene,
    RequestQuit,
    UnlockAchievement
};

struct SceneEvent {
    SceneEventType type = SceneEventType::None;
    std::string payload;
};
