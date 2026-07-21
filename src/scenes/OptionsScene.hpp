#pragma once

#include "scenes/GameScene.hpp"

class OptionsScene final : public GameScene {
  public:
    OptionsScene() = default;
    ~OptionsScene() override;

    void onEnter(SceneSharedState& state) override;
    void onExit(SceneSharedState& state) override;

    void render(SceneSharedState& state, float dt) override;
};
