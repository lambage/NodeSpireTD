#pragma once

#include "scenes/GameScene.hpp"

class MainMenuScene final : public GameScene {
  public:
    ~MainMenuScene() override;
    void onEnter(SceneSharedState&) override {}
    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;
};