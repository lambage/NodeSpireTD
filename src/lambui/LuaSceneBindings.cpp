#include "lambui/LuaSceneBindings.hpp"

#include "lambui/LuaUiScene.hpp"
#include "lambui/AppControl.hpp"
#include "lua.hpp"

#include <LambUI/UITypes.h>

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

void BindScancodes(lua_State* L) {
    lua_newtable(L);
    const auto set = [&](const char* name, uint32_t value) {
        lua_pushinteger(L, static_cast<lua_Integer>(value));
        lua_setfield(L, -2, name);
    };

    set("BACKSPACE", LambUI::ScanCode::Backspace);
    set("TAB", LambUI::ScanCode::Tab);
    set("ENTER", LambUI::ScanCode::Enter);
    set("ESCAPE", LambUI::ScanCode::Escape);
    set("SPACE", LambUI::ScanCode::Space);
    set("LEFT_SHIFT", LambUI::ScanCode::LeftShift);
    set("RIGHT_SHIFT", LambUI::ScanCode::RightShift);
    set("LEFT", LambUI::ScanCode::Left);
    set("RIGHT", LambUI::ScanCode::Right);
    set("UP", LambUI::ScanCode::Up);
    set("DOWN", LambUI::ScanCode::Down);
    set("HOME", LambUI::ScanCode::Home);
    set("END", LambUI::ScanCode::End);
    set("DELETE", LambUI::ScanCode::Delete);
    set("LEFT_CONTROL", LambUI::ScanCode::LeftControl);
    set("RIGHT_CONTROL", LambUI::ScanCode::RightControl);
    set("A", LambUI::ScanCode::A);
    set("C", LambUI::ScanCode::C);
    set("V", LambUI::ScanCode::V);
    set("X", LambUI::ScanCode::X);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "keys");
    lua_pushvalue(L, -1);
    lua_setglobal(L, "Keys");
    lua_pop(L, 1);
}

} // namespace

void BindSceneControl(lua_State* L, LuaUiScene& scene) {
    BindScancodes(L);
    lua_newtable(L);
    lua_pushlightuserdata(L, &scene);
    lua_pushcclosure(L, LuaGoTo, 1);
    lua_setfield(L, -2, "GoTo");
    lua_pushcfunction(L, LuaQuit);
    lua_setfield(L, -2, "Quit");
    lua_setglobal(L, "Scene");
}

} // namespace NodeSpireUi
