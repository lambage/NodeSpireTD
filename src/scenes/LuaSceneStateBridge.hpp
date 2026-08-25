#pragma once

#include "scenes/SceneSharedState.hpp"

struct lua_State;

class LuaSceneStateBridge {
  public:
    static void apply(lua_State* L, int stateTableIndex, SceneSharedState& state);
    static void push(lua_State* L, const SceneSharedState& state);
};
