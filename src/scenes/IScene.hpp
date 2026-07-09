#pragma once

#include "ImGuiLayer.hpp"
#include "scenes/SceneSharedState.hpp"

#include <SFML/Window/Event.hpp>
#include <string>

enum class SceneId {
    Splash,
    MainMenu,
    Options,
    LevelSelection,
    PlayLevel
};

struct SceneFrameResult {
    bool requestQuit = false;
    bool requestApplySettings = false;
    bool requestAcceptDisplayChanges = false;
    bool requestRevertDisplayChanges = false;

    bool requestTransition = false;
    SceneId transitionTarget = SceneId::MainMenu;
    std::string transitionMessage{};
    float transitionMinDurationSeconds = 0.6f;
};

class IScene {
  public:
    virtual ~IScene() = default;

    virtual void onEnter(SceneSharedState&) = 0;
    virtual void onExit(SceneSharedState&) = 0;

    virtual void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) = 0;
    virtual SceneFrameResult render(SceneSharedState& state) = 0;
};
