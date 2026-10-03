#pragma once

#include "lambui/LuaUiScene.hpp"

namespace NodeSpireUi {

// Splash screen: shows the logo and a status line (built by Splash.lua),
// then hands off to the main menu after a minimum duration (or immediately
// on any keypress).
class SplashScene final : public LuaUiScene {
  public:
    explicit SplashScene(lambui_backend::VulkanUiRenderer& renderer);

    SceneTransition onKeyDown(uint32_t scanCode) override;

  protected:
    void onUpdateScene(float dt) override;
    void onSceneEnter(AudioEngine& audio) override;

  private:
    float elapsedSeconds_ = 0.0f;

    static constexpr float kMinimumSplashSeconds = 2.0f;
};

} // namespace NodeSpireUi

