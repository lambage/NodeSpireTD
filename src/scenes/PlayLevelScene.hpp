#pragma once
#include "scenes/GameScene.hpp"

class PlayLevelScene final : public GameScene {
  public:
    ~PlayLevelScene() override;

    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;

  private:
    static constexpr float kSimulationDurationSeconds = 5.0f;
    float remainingSeconds_ = kSimulationDurationSeconds;
};
