#pragma once

#include "scenes/GameScene.hpp"

#include <string>
#include <vector>

class LevelSelectionScene final : public GameScene {
  public:
    ~LevelSelectionScene();
    
    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;

  private:
    std::vector<std::string> availableLevels_ = {"Forest Outskirts", "Iron Ridge", "Delta Relay"};
    int selectedLevelIndex_ = 0;
};
