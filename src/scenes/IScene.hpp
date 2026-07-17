#pragma once

#include "ImGuiLayer.hpp"
#include "scenes/SceneSharedState.hpp"

#include "lua.hpp"

#include <SFML/Window/Event.hpp>
#include <lua.h>
#include <string>
#include <volk.h>

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
    float transitionMinDurationSeconds = 0.0f;
};

class IScene {
  public:
    IScene(lua_State* L) : L_(L) {}
    virtual ~IScene() = default;

    virtual void onEnter(SceneSharedState&) = 0;
    virtual void onExit(SceneSharedState&) = 0;

    virtual void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) = 0;
    virtual SceneFrameResult render(SceneSharedState& state, float dt) = 0;

    // Called each frame with the active command buffer (inside vkCmdBeginRendering)
    // before ImGui is rendered. Override in 3D game scenes.
    virtual void renderWorld(VkCommandBuffer /*cmd*/, VkExtent2D /*extent*/) {}

  protected:
    lua_State* L_ = nullptr;
};
