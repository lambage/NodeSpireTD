#pragma once

#include "scenes/GameScene.hpp"

class OptionsScene final : public GameScene {
  public:
    ~OptionsScene() override;

    void onEnter(SceneSharedState&) override {}
    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;
};
