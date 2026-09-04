#pragma once

#include <optional>

namespace NodeSpireUi {

// Every screen the RmlUi-based app can show. Mirrors the legacy SceneId enum
// in Scenes.hpp, but grown independently as scenes are ported so the two
// apps (NodeSpireTD-imgui and NodeSpireTD) never have to agree on values.
enum class SceneId {
    Splash,
    MainMenu,
    Lobby,
    Options,
};

// A scene's per-frame update returns one of these to request a transition.
using SceneTransition = std::optional<SceneId>;

} // namespace NodeSpireUi
