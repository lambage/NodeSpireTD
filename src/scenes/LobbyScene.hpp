#pragma once

#include "multiplayer/MultiplayerSession.hpp"
#include "multiplayer/PlayerProfileStore.hpp"
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

    // Cached each render() frame from state.multiplayerSession so Lua closures (which only carry
    // a LobbyScene* upvalue, not the SceneSharedState) can reach the persistent session.
    multiplayer::MultiplayerSession* session_ = nullptr;
    // Cached each render() frame from state.playerProfileStore for the same reason; owns the
    // local player's persistent username + hidden UUID (see PlayerProfileStore.hpp).
    multiplayer::PlayerProfileStore* profileStore_ = nullptr;
    unsigned short lastHostPort_ = 47321;
    std::string lastJoinAddress_ = "127.0.0.1";
    // True for the one render() frame in which a client just consumed the host's match-start
    // announcement; Lua polls this via checkPartyMatchStart() to trigger requestScene(PlayLevel).
    bool matchStartJustAnnounced_ = false;

  void registerLuaGameplayApi();
};

