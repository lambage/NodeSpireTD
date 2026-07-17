#pragma once

#include "scenes/GameScene.hpp"

#include "lua.hpp"

class SplashScene final : public GameScene {
  public:
    SplashScene(lua_State* L) : GameScene(L) {}
    ~SplashScene() override;

    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;

  private:
    static constexpr float kMinimumSplashSeconds = 3.0f;
    float elapsedSeconds_ = 0.0f;
};