#pragma once

#include "scenes/GameScene.hpp"

#include "lua.hpp"

#include <filesystem>
#include <string>
#include <vector>

class LevelSelectionScene final : public GameScene {
  public:
    LevelSelectionScene(lua_State* L) : GameScene(L) {}
    ~LevelSelectionScene();

    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state, float dt) override;

  private:
    struct LevelEntry {
      std::string name;
      std::filesystem::path assetPath;
    };

    std::vector<LevelEntry> availableLevels_;
    int selectedLevelIndex_ = 0;
};
