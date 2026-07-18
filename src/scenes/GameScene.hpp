#pragma once

#include "lua.hpp"
#include "scenes/IScene.hpp"

#include <SFML/Window/Event.hpp>

class ImGuiLayer;

class GameScene : public IScene {
  public:
    GameScene() = default;

    ~GameScene() override;

    void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) override;

  protected:
    float elapsedSeconds_ = 0.0f;
    int scriptRef_ = LUA_NOREF;

    int loadLuaScript(SceneSharedState& state, const std::string& scriptPath);

    void luaOnEnter(int scriptRef);
    void luaOnExit(SceneSharedState& state, int scriptRef);
    SceneFrameResult luaOnRender(SceneSharedState& state, int scriptRef, float dt);
};

SceneId SceneIdFromString(const std::string& name);
