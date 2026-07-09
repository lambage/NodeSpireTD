#include "scenes/GameScene.hpp"

#include "ImGuiLayer.hpp"

#include <SFML/Window/Event.hpp>

GameScene::~GameScene() = default;

void GameScene::handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) {
    imguiLayer.processEvent(event);
}
