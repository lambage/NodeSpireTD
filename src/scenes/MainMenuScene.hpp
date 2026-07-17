#pragma once

#include "scenes/GameScene.hpp"

#include "lua.hpp"

class MainMenuScene final : public GameScene {
  public:
    MainMenuScene(lua_State* L) : GameScene(L) {}
    ~MainMenuScene() override;
    void onEnter(SceneSharedState&) override {}
    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;
};