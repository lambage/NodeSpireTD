#include "lambui/scenes/SplashScene.hpp"

namespace NodeSpireUi {

SplashScene::SplashScene() : LuaUiScene("assets/scenes/Splash.lua") {}

void SplashScene::onSceneEnter(AudioEngine& /*audio*/) {
    elapsedSeconds_ = 0.0f;
}

void SplashScene::onUpdateScene(float dt) {
    elapsedSeconds_ += dt;
    if (elapsedSeconds_ >= kMinimumSplashSeconds) {
        requestTransition(SceneId::MainMenu);
    }
}

SceneTransition SplashScene::onKeyDown(uint32_t /*scanCode*/) {
    return SceneId::MainMenu;
}

} // namespace NodeSpireUi

