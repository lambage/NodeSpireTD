#include "scenes/OptionsScene.hpp"

#include "scenes/SceneSharedState.hpp"

OptionsScene::~OptionsScene() = default;

void OptionsScene::onEnter(SceneSharedState& state) {
    scriptRef_ = loadLuaScript(state, "assets/scenes/Options.lua");
    luaOnEnter(scriptRef_);
}

void OptionsScene::onExit(SceneSharedState& state) {
    luaOnExit(state, scriptRef_);
}

void OptionsScene::render(SceneSharedState& state, float dt) {
    luaOnRender(state, scriptRef_, dt);
}
