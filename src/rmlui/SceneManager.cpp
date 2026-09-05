#include "rmlui/SceneManager.hpp"

#include "rmlui/scenes/LobbyScene.hpp"
#include "rmlui/scenes/MainMenuScene.hpp"
#include "rmlui/scenes/OptionsScene.hpp"
#include "rmlui/scenes/SplashScene.hpp"

namespace NodeSpireUi {

namespace {
std::unique_ptr<IScene> createScene(SceneId id) {
    switch (id) {
    case SceneId::Splash:
        return std::make_unique<SplashScene>();
    case SceneId::MainMenu:
        return std::make_unique<MainMenuScene>();
    case SceneId::Lobby:
        return std::make_unique<LobbyScene>();
    case SceneId::Options:
        return std::make_unique<OptionsScene>();
    }
    return nullptr;
}
} // namespace

SceneManager::SceneManager(Rml::Context& context, SceneId initialScene, AudioEngine& audio)
    : context_(context), audio_(audio), activeSceneId_(initialScene) {
    enterScene(initialScene);
}

void SceneManager::enterScene(SceneId id) {
    activeSceneId_ = id;
    activeScene_ = createScene(id);
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

void SceneManager::handleKeyDown(Rml::Input::KeyIdentifier key) {
    applyTransition(activeScene_->onKeyDown(key));
}

} // namespace NodeSpireUi
