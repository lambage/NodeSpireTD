#pragma once

#include "scenes/GameScene.hpp"

#include "lua.hpp"

class OptionsScene final : public GameScene {
  public:
    OptionsScene(lua_State* L) : GameScene(L) {}
    ~OptionsScene() override;

    void onEnter(SceneSharedState&) override {}
    void onExit(SceneSharedState&) override {}

    SceneFrameResult render(SceneSharedState& state) override;
};
