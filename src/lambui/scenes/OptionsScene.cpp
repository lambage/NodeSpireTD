#include "lambui/scenes/OptionsScene.hpp"

namespace NodeSpireUi {

// TODO(lambui-migration): rebuild GFX/Audio/Gameplay tabs via LambUI/Lua,
// backed by SettingsManager/AppSettings as before. See the removed RmlUi
// implementation (git history, src/lambui/scenes/OptionsScene.cpp prior to
// this commit) for the settings/live-apply logic to port over.
void OptionsScene::onEnter(LambUI::UIManager& /*ui*/, AudioEngine& /*audio*/) {}

void OptionsScene::onExit(LambUI::UIManager& /*ui*/) {}

SceneTransition OptionsScene::update(float /*dt*/) {
    return std::nullopt;
}

} // namespace NodeSpireUi


