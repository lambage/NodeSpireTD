#include "lambui/SceneManager.hpp"

#include "lambui/scenes/LobbyScene.hpp"
#include "lambui/scenes/MainMenuScene.hpp"
#include "lambui/scenes/OptionsScene.hpp"
#include "lambui/scenes/PlayLevelScene.hpp"
#include "lambui/scenes/SplashScene.hpp"

namespace NodeSpireUi {

namespace {
std::unique_ptr<IScene> createScene(SceneId id, multiplayer::MultiplayerSession& multiplayerSession,
                                    multiplayer::PlayerProfileStore& playerProfileStore, VulkanContext& vulkanContext,
                                    PlayLevelLaunchConfig& playLevelLaunchConfig,
                                    lambui_backend::VulkanUiRenderer& renderer) {
    switch (id) {
    case SceneId::Splash:
        return std::make_unique<SplashScene>(renderer);
    case SceneId::MainMenu:
        return std::make_unique<MainMenuScene>(renderer);
    case SceneId::Lobby:
        return std::make_unique<LobbyScene>(multiplayerSession, playerProfileStore, playLevelLaunchConfig);
    case SceneId::Options:
        return std::make_unique<OptionsScene>();
    case SceneId::PlayLevel:
        return std::make_unique<PlayLevelScene>(vulkanContext, multiplayerSession, playLevelLaunchConfig);
    }
    return nullptr;
}
} // namespace

SceneManager::SceneManager(LambUI::UIManager& ui, SceneId initialScene, AudioEngine& audio,
                                                     VulkanContext& vulkanContext,
                                                     multiplayer::MultiplayerSession& multiplayerSession,
                                                     multiplayer::PlayerProfileStore& playerProfileStore,
                                                     lambui_backend::VulkanUiRenderer& renderer)
        : ui_(ui), audio_(audio), vulkanContext_(vulkanContext), renderer_(renderer), multiplayerSession_(multiplayerSession), playerProfileStore_(playerProfileStore),
            activeSceneId_(initialScene) {
    enterScene(initialScene);
}

void SceneManager::enterScene(SceneId id) {
    activeSceneId_ = id;
    activeScene_ = createScene(id, multiplayerSession_, playerProfileStore_, vulkanContext_, playLevelLaunchConfig_, renderer_);
    activeScene_->onEnter(ui_, audio_);
}

void SceneManager::applyTransition(const SceneTransition& transition) {
    if (!transition.has_value() || *transition == activeSceneId_) {
        return;
    }

    activeScene_->onExit(ui_);
    enterScene(*transition);
}

void SceneManager::update(float dt) {
    applyTransition(activeScene_->update(dt));
}

void SceneManager::renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent) {
    activeScene_->renderWorld(commandBuffer, extent);
}

void SceneManager::renderOverlay(VkCommandBuffer commandBuffer, VkExtent2D extent) {
    activeScene_->renderOverlay(commandBuffer, extent);
}

bool SceneManager::handleKeyDown(uint32_t scanCode) {
    const bool handled = activeScene_->handleShortcut(scanCode);
    const SceneId previousSceneId = activeSceneId_;
    applyTransition(activeScene_->onKeyDown(scanCode));
    return handled || activeSceneId_ != previousSceneId;
}

void SceneManager::reloadActiveScene() {
    const SceneId id = activeSceneId_;
    activeScene_->onExit(ui_);
    enterScene(id);
}

void SceneManager::shutdown() {
    if (!activeScene_) {
        return;
    }
    activeScene_->onExit(ui_);
    activeScene_.reset();
}

} // namespace NodeSpireUi
