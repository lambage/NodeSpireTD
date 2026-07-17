#include "scenes/MainMenuScene.hpp"

#include "scenes/SceneSharedState.hpp"

MainMenuScene::~MainMenuScene() = default;

void MainMenuScene::onEnter(SceneSharedState& state) {
    scriptRef_ = loadLuaScript(state, "assets/scenes/MainMenu.lua");
    luaOnEnter(scriptRef_);
}

void MainMenuScene::onExit(SceneSharedState& state) {
    luaOnExit(state, scriptRef_);
}

SceneFrameResult MainMenuScene::render(SceneSharedState& state, float dt) {
    return luaOnRender(state, scriptRef_, dt);
}