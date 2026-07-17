#pragma once

#include "lua.hpp"
#include "scenes/GameScene.hpp"


class MainMenuScene final : public GameScene {
  public:
    MainMenuScene(lua_State* L) : GameScene(L) {}
    ~MainMenuScene() override;
    void onEnter(SceneSharedState&) override;
    void onExit(SceneSharedState&) override;

    SceneFrameResult render(SceneSharedState& state, float dt) override;
};