#include "lambui/scenes/LobbyScene.hpp"

namespace NodeSpireUi {

// TODO(lambui-migration): compile-only placeholder. Party roster, chat, and
// level/tower loadout selection have not been rebuilt against LambUI/Lua
// yet; see the removed RmlUi implementation (git history) for the logic to
// port over.
LobbyScene::LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore,
                       PlayLevelLaunchConfig& playLevelLaunchConfig)
    : session_(session), profileStore_(profileStore), playLevelLaunchConfig_(playLevelLaunchConfig) {}

void LobbyScene::onEnter(LambUI::UIManager& /*ui*/, AudioEngine& /*audio*/) {}

void LobbyScene::onExit(LambUI::UIManager& /*ui*/) {}

SceneTransition LobbyScene::update(float /*dt*/) {
    return std::nullopt;
}

} // namespace NodeSpireUi
