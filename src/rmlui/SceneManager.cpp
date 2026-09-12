#include "rmlui/SceneManager.hpp"

#include "rmlui/scenes/LobbyScene.hpp"
#include "rmlui/scenes/MainMenuScene.hpp"
#include "rmlui/scenes/OptionsScene.hpp"
#include "rmlui/scenes/PlayLevelScene.hpp"
#include "rmlui/scenes/SplashScene.hpp"

namespace NodeSpireUi {

namespace {
std::unique_ptr<IScene> createScene(SceneId id, multiplayer::MultiplayerSession& multiplayerSession,
                                    multiplayer::PlayerProfileStore& playerProfileStore, VulkanContext& vulkanContext,
                                    PlayLevelLaunchConfig& playLevelLaunchConfig) {
    switch (id) {
    case SceneId::Splash:
        return std::make_unique<SplashScene>();
    case SceneId::MainMenu:
        return std::make_unique<MainMenuScene>();
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

SceneManager::SceneManager(Rml::Context& context, SceneId initialScene, AudioEngine& audio,
                                                     VulkanContext& vulkanContext,
                                                     multiplayer::MultiplayerSession& multiplayerSession,
                                                     multiplayer::PlayerProfileStore& playerProfileStore)
        : context_(context), audio_(audio), vulkanContext_(vulkanContext), multiplayerSession_(multiplayerSession), playerProfileStore_(playerProfileStore),
            activeSceneId_(initialScene) {
    enterScene(initialScene);
}

void SceneManager::enterScene(SceneId id) {
    activeSceneId_ = id;
    activeScene_ = createScene(id, multiplayerSession_, playerProfileStore_, vulkanContext_, playLevelLaunchConfig_);
    activeScene_->onEnter(context_, audio_);
}

void SceneManager::applyTransition(const SceneTransition& transition) {
    if (!transition.has_value() || *transition == activeSceneId_) {
        return;
    }

    activeScene_->onExit(context_);
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

bool SceneManager::handleKeyDown(Rml::Input::KeyIdentifier key) {
    const bool handled = activeScene_->handleShortcut(key);
    const SceneId previousSceneId = activeSceneId_;
    applyTransition(activeScene_->onKeyDown(key));
    return handled || activeSceneId_ != previousSceneId;
}

void SceneManager::shutdown() {
    if (!activeScene_) {
        return;
    }
    activeScene_->onExit(context_);
    activeScene_.reset();
}

} // namespace NodeSpireUi
