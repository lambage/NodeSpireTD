#pragma once

#include "lambui/IScene.hpp"

namespace LambUI {
class UIManager;
}

namespace multiplayer {
class MultiplayerSession;
class PlayerProfileStore;
}

namespace NodeSpireUi {

// TODO(lambui-migration): compile-only placeholder. The RmlUi-era
// LobbyScene (party roster, chat, level/tower loadout selection) has not
// been rebuilt against LambUI/Lua yet; see docs/adr or repo memory for the
// migration's phased plan.
class LobbyScene final : public IScene {
  public:
    LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore,
               PlayLevelLaunchConfig& playLevelLaunchConfig);

    void onEnter(LambUI::UIManager& ui, AudioEngine& audio) override;
    void onExit(LambUI::UIManager& ui) override;
    SceneTransition update(float dt) override;

  private:
    multiplayer::MultiplayerSession& session_;
    multiplayer::PlayerProfileStore& profileStore_;
    PlayLevelLaunchConfig& playLevelLaunchConfig_;
};

} // namespace NodeSpireUi

