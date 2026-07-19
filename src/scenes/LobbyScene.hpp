#pragma once

#include "scenes/GameScene.hpp"

#include <filesystem>
#include <string>
#include <vector>

class LobbyScene final : public GameScene {
  public:
    LobbyScene() = default;
    ~LobbyScene();

    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override;

    void render(SceneSharedState& state, float dt) override;

  private:
    struct LevelEntry {
      std::string name;
      std::filesystem::path assetPath;
      std::filesystem::path scriptPath;
    };

    std::vector<LevelEntry> availableLevels_;
    int selectedLevelIndex_ = 0;

  void registerLuaGameplayApi();
};
