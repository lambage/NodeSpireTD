#pragma once

#include "scenes/IScene.hpp"

#include "lua.hpp"

#include <SFML/Window/Event.hpp>
#include <imgui.h>
#include <memory>
#include <unordered_map>

struct SceneIdHash {
    std::size_t operator()(SceneId value) const noexcept {
        return static_cast<std::size_t>(value);
    }
};

using SceneGraph = std::unordered_map<SceneId, std::unique_ptr<IScene>, SceneIdHash>;

SceneGraph createDefaultScenes();
