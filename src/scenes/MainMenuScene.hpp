#pragma once

#include "scenes/GameScene.hpp"


class MainMenuScene final : public GameScene {
  public:
    MainMenuScene() = default;
    ~MainMenuScene() override;
    void onEnter(SceneSharedState&) override;
    void onExit(SceneSharedState&) override;

    void render(SceneSharedState& state, float dt) override;
};