#pragma once

#include "scenes/IScene.hpp"

#include <SFML/Window/Event.hpp>

class ImGuiLayer;

class GameScene : public IScene {
  public:
    ~GameScene() override;

    void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) override;
};
