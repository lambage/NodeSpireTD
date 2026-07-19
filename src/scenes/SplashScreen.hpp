#pragma once

#include "scenes/GameScene.hpp"

class SplashScene final : public GameScene {
  public:
    SplashScene() = default;
    ~SplashScene() override;

    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override;

    void render(SceneSharedState& state, float dt) override;
};