#include "lambui/LuaSceneBindings.hpp"

#include "lambui/LuaUiScene.hpp"
#include "lambui/AppControl.hpp"
#include "lua.hpp"

#include <string_view>

namespace NodeSpireUi {

namespace {

LuaUiScene& Self(lua_State* L) {
    return *static_cast<LuaUiScene*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int LuaGoTo(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const std::string_view view{name};
    SceneId id;
    if (view == "Splash") id = SceneId::Splash;
    else if (view == "MainMenu") id = SceneId::MainMenu;
    else if (view == "Lobby") id = SceneId::Lobby;
    else if (view == "Options") id = SceneId::Options;
    else if (view == "PlayLevel") id = SceneId::PlayLevel;
    else return luaL_error(L, "Scene.GoTo: unknown scene '%s'", name);
    Self(L).requestTransitionFromLua(id);
    return 0;
}

int LuaQuit(lua_State* /*L*/) {
    RequestQuit();
    return 0;
}

} // namespace

void BindSceneControl(lua_State* L, LuaUiScene& scene) {
    lua_newtable(L);
    lua_pushlightuserdata(L, &scene);
    lua_pushcclosure(L, LuaGoTo, 1);
    lua_setfield(L, -2, "GoTo");
    lua_pushcfunction(L, LuaQuit);
    lua_setfield(L, -2, "Quit");
    lua_setglobal(L, "Scene");
}

} // namespace NodeSpireUi
