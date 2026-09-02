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

    // Multiplayer host/join UI state, pushed into SceneSharedState every frame in render() (same
    // pattern as activeLevelName/AssetPath/ScriptPath above) so PlayLevelScene::onEnter() can
    // read the player's choice once it transitions in.
    bool hostMultiplayerMatch_ = false;
    unsigned short multiplayerPort_ = 47321;
    std::string joinRemoteHostAddress_;

  void registerLuaGameplayApi();
};
