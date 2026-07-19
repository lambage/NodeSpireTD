#pragma once

#include "scenes/GameScene.hpp"

class OptionsScene final : public GameScene {
  public:
    OptionsScene() = default;
    ~OptionsScene() override;

    void onEnter(SceneSharedState&) override {}
    void onExit(SceneSharedState&) override {}

    void render(SceneSharedState& state, float dt) override;
};
