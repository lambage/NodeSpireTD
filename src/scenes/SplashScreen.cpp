#include "scenes/SplashScreen.hpp"

#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"
#include "VulkanContext.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <string>

SplashScene::~SplashScene() = default;

void SplashScene::onEnter(SceneSharedState& state) {
    elapsedSeconds_ = 0.0f;

    // Expose fonts as lightuserdata globals (nil if unavailable)
    scriptRef_ = loadLuaScript(state, "assets/scenes/SplashScreen.lua");

    luaOnEnter(scriptRef_);
}

void SplashScene::onExit(SceneSharedState& state) {
    luaOnExit(state, scriptRef_);
}

SceneFrameResult SplashScene::render(SceneSharedState& state, float dt) {
    return luaOnRender(state, scriptRef_, dt);
}
