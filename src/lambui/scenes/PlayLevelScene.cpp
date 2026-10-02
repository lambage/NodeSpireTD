#include "lambui/scenes/PlayLevelScene.hpp"

namespace NodeSpireUi {

// TODO(lambui-migration): compile-only placeholder; see PlayLevelScene.hpp.
PlayLevelScene::PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                               const PlayLevelLaunchConfig& launchConfig)
    : vulkanContext_(vulkanContext), session_(session), launchConfig_(launchConfig) {}

PlayLevelScene::~PlayLevelScene() = default;

void PlayLevelScene::onEnter(LambUI::UIManager& /*ui*/, AudioEngine& /*audio*/) {}

void PlayLevelScene::onExit(LambUI::UIManager& /*ui*/) {}

SceneTransition PlayLevelScene::update(float /*dt*/) {
    return std::nullopt;
}

} // namespace NodeSpireUi
