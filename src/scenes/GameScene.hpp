#pragma once

#include "scenes/IScene.hpp"

#include "lua.hpp"

#include <SFML/Window/Event.hpp>

class ImGuiLayer;

class GameScene : public IScene {
  public:
    GameScene(lua_State* L) : IScene(L) {}

    ~GameScene() override;

    void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) override;
};
