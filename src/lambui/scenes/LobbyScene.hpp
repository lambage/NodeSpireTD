#pragma once

#include "lambui/LuaUiScene.hpp"

namespace multiplayer {
class MultiplayerSession;
class PlayerProfileStore;
}

namespace NodeSpireUi {

class LobbyScene final : public LuaUiScene {
  public:
    LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore,
               PlayLevelLaunchConfig& playLevelLaunchConfig, lambui_backend::VulkanUiRenderer& renderer);

  protected:
    void bindSceneApi(lua_State* lua, AudioEngine& audio) override;
    void onUpdateScene(float dt) override;

  private:
    struct LevelEntry {
        PlayLevelLaunchConfig launch;
        std::string description;
        std::string threat;
        std::string waves;
        std::string players;
        std::string thumbnail;
    };
    struct TowerEntry {
        std::string id;
        std::string name;
        std::string portrait;
        std::string bio;
        int cost = 0;
    };

    void loadCatalogs(lua_State* lua);
    bool canStart() const;
    void configureLaunch(std::size_t index);
    bool configureActiveMatch();
    int pushState(lua_State* lua) const;
    static int dispatch(lua_State* lua);

    multiplayer::MultiplayerSession& session_;
    multiplayer::PlayerProfileStore& profileStore_;
    PlayLevelLaunchConfig& playLevelLaunchConfig_;
    std::vector<LevelEntry> levels_;
    std::vector<TowerEntry> towers_;
    std::vector<std::string> chat_;
    std::size_t selectedLevel_ = 0;
    std::string status_;
};

} // namespace NodeSpireUi

