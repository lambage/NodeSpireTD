#pragma once

#include <optional>
#include <string>
#include <vector>

namespace NodeSpireUi {

// Every screen the RmlUi-based app can show. Mirrors the legacy SceneId enum
// in Scenes.hpp, but grown independently as scenes are ported so the two
// apps (NodeSpireTD-imgui and NodeSpireTD) never have to agree on values.
enum class SceneId {
    Splash,
    MainMenu,
    Lobby,
    Options,
    PlayLevel,
};

struct PlayLevelLaunchConfig {
    std::string levelId = "grassy";
    std::string displayName = "Grassy";
    std::string definitionPath = "assets/levels/grassy/level.lua";
    std::string mapAssetPath = "assets/levels/grassy/grassy_map.glb";
    std::string startModelPath = "assets/models/base/portal.glb";
    std::string endModelPath = "assets/models/base/base.glb";
    std::vector<std::string> animatedTemplateModelPaths{
        "assets/models/enemy/goblin1.glb",
        "assets/models/enemy/goblin_scout.glb",
    };
    std::vector<std::string> towerLoadoutIds;
    bool towerLoadoutConfigured = false;
};

// A scene's per-frame update returns one of these to request a transition.
using SceneTransition = std::optional<SceneId>;

} // namespace NodeSpireUi
