#include "lambui/LuaUiScene.hpp"

#include "lambui/LuaAudioBindings.hpp"
#include "lambui/LuaSceneBindings.hpp"
#include "lua.hpp"

#include <LambUI/UIManager.h>
#include <LambUI/UIWidget.h>
#include <LambUILua/LuaBindings.h>

#include <cstdio>

namespace NodeSpireUi {

LuaUiScene::LuaUiScene(std::string scriptPath) : scriptPath_(std::move(scriptPath)) {}

LuaUiScene::~LuaUiScene() = default;

void LuaUiScene::onEnter(LambUI::UIManager& ui, AudioEngine& audio) {
    pendingTransition_.reset();
    ui.GetRoot().DestroyChildren();

    lua_ = luaL_newstate();
    luaL_openlibs(lua_);
    bindings_ = std::make_unique<LambUILua::LuaUIBindings>(lua_, ui);
    BindAudioEngine(lua_, audio);
    BindSceneControl(lua_, *this);
    registerEngineBindings(lua_, audio);

    if (luaL_dofile(lua_, scriptPath_.c_str()) != LUA_OK) {
        const char* error = lua_tostring(lua_, -1);
        std::fprintf(stderr, "LuaUiScene: failed to run '%s': %s\n", scriptPath_.c_str(),
                     error ? error : "unknown error");
        lua_pop(lua_, 1);
    }

    onSceneEnter(audio);
}

void LuaUiScene::onExit(LambUI::UIManager& ui) {
    bindings_.reset();
    if (lua_) {
        lua_close(lua_);
        lua_ = nullptr;
    }
    ui.GetRoot().DestroyChildren();
}

SceneTransition LuaUiScene::update(float dt) {
    if (lua_) {
        lua_getglobal(lua_, "OnUpdate");
        if (lua_isfunction(lua_, -1)) {
            lua_pushnumber(lua_, dt);
            if (lua_pcall(lua_, 1, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(lua_, -1);
                std::fprintf(stderr, "LuaUiScene: OnUpdate failed: %s\n", error ? error : "unknown error");
                lua_pop(lua_, 1);
            }
        } else {
            lua_pop(lua_, 1);
        }
    }
    onUpdateScene(dt);
    return pendingTransition_;
}

} // namespace NodeSpireUi
