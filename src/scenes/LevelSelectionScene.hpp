#pragma once

#include "scenes/GameScene.hpp"

#include <filesystem>
#include <string>
#include <vector>

class LevelSelectionScene final : public GameScene {
  public:
    ~LevelSelectionScene();

    void onEnter(SceneSharedState&) override;

    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;

  private:
    struct LevelEntry {
      std::string name;
      std::filesystem::path assetPath;
    };

    std::vector<LevelEntry> availableLevels_ = {
        {"Pine Forest",      "assets/pine_forest"},             // directory — scanned for .glb/.gltf
        {"Forest Outskirts", "assets/terrain/Terrain003_4K.obj"},
        {"Iron Ridge",       "assets/terrain/Terrain003_4K.obj"},
        {"Delta Relay",      "assets/terrain/Terrain003_4K.obj"},
    };
    int selectedLevelIndex_ = 0;
};
