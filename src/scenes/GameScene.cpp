#include "scenes/GameScene.hpp"

#include "ImGuiLayer.hpp"
#include "LuaStateBootstrap.hpp"
#include "VulkanContext.hpp"

#include <SFML/Window/Event.hpp>
#include <spdlog/spdlog.h>

GameScene::~GameScene() = default;

void GameScene::handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) {
    imguiLayer.processEvent(event);
}

// Builds a Lua table representing the current SceneSharedState.
// Leaves the table on the top of the stack.
static void pushSceneState(lua_State* L, const SceneSharedState& state) {
    lua_newtable(L);

    lua_pushboolean(L, state.loadingComplete);
    lua_setfield(L, -2, "loadingComplete");

    lua_pushstring(L, state.activeLevelName.c_str());
    lua_setfield(L, -2, "activeLevelName");

    lua_pushstring(L, state.activeLevelAssetPath.c_str());
    lua_setfield(L, -2, "activeLevelAssetPath");

    lua_pushboolean(L, state.displayConfirmationActive);
    lua_setfield(L, -2, "displayConfirmationActive");

    lua_pushnumber(L, state.displayConfirmationSecondsRemaining);
    lua_setfield(L, -2, "displayConfirmationSecondsRemaining");

    lua_pushinteger(L, state.selectedDisplayModeIndex);
    lua_setfield(L, -2, "selectedDisplayModeIndex");

    if (state.headingFont) {
        lua_pushlightuserdata(L, state.headingFont);
    } else {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "headingFont");

    if (state.titleFont) {
        lua_pushlightuserdata(L, state.titleFont);
    } else {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "titleFont");

    // Nested settings table
    lua_newtable(L);
    lua_pushboolean(L, state.settings.fullscreen);       lua_setfield(L, -2, "fullscreen");
    lua_pushboolean(L, state.settings.vSyncEnabled);     lua_setfield(L, -2, "vSyncEnabled");
    lua_pushinteger(L, state.settings.displayWidth);     lua_setfield(L, -2, "displayWidth");
    lua_pushinteger(L, state.settings.displayHeight);    lua_setfield(L, -2, "displayHeight");
    lua_pushinteger(L, state.settings.refreshRate);      lua_setfield(L, -2, "refreshRate");
    lua_pushinteger(L, state.settings.graphicsQuality);  lua_setfield(L, -2, "graphicsQuality");
    lua_pushnumber(L, state.settings.masterVolume);      lua_setfield(L, -2, "masterVolume");
    lua_pushnumber(L, state.settings.musicVolume);       lua_setfield(L, -2, "musicVolume");
    lua_pushnumber(L, state.settings.sfxVolume);         lua_setfield(L, -2, "sfxVolume");
    lua_pushboolean(L, state.settings.muteWhenUnfocused); lua_setfield(L, -2, "muteWhenUnfocused");
    lua_setfield(L, -2, "settings");
}

int GameScene::loadLuaScript(SceneSharedState& state, const std::string& scriptPath) {
    LuaStateBootstrap::initializeEngineState(L_, state.vulkanContext);

    if (state.titleFont) {
        lua_pushlightuserdata(L_, state.titleFont);
    } else {
        lua_pushnil(L_);
    }
    lua_setglobal(L_, "TitleFont");

    if (state.headingFont) {
        lua_pushlightuserdata(L_, state.headingFont);
    } else {
        lua_pushnil(L_);
    }
    lua_setglobal(L_, "HeadingFont");

    // Load and execute the script — it must return a module table
    if (luaL_loadfile(L_, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("GameScene: failed to load script: {}", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return LUA_NOREF;
    }
    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("GameScene: script error: {}", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return LUA_NOREF;
    }
    if (!lua_istable(L_, -1)) {
        spdlog::error("GameScene: script did not return a module table");
        lua_pop(L_, 1);
        return LUA_NOREF;
    }
    return luaL_ref(L_, LUA_REGISTRYINDEX);
}

void GameScene::luaOnEnter(int scriptRef) {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, scriptRef);
    lua_getfield(L_, -1, "onEnter");
    lua_remove(L_, -2);
    if (lua_isfunction(L_, -1)) {
        if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            spdlog::error("GameScene onEnter error: {}", lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }
    } else {
        lua_pop(L_, 1);
    }
}

void GameScene::luaOnExit(SceneSharedState& state, int scriptRef) {
    if (scriptRef == LUA_NOREF) {
        return;
    }

    // GPU must be idle before Lua GC destroys texture resources
    if (state.vulkanContext) {
        state.vulkanContext->waitIdle();
    }

    // Call Lua onExit()
    lua_rawgeti(L_, LUA_REGISTRYINDEX, scriptRef);
    lua_getfield(L_, -1, "onExit");
    lua_remove(L_, -2);
    if (lua_isfunction(L_, -1)) {
        if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            spdlog::error("GameScene onExit error: {}", lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }
    } else {
        lua_pop(L_, 1);
    }

    // Force GC so __gc on texture userdata runs now, while Vulkan is still alive
    lua_gc(L_, LUA_GCCOLLECT, 0);

    luaL_unref(L_, LUA_REGISTRYINDEX, scriptRef);
}

SceneFrameResult GameScene::luaOnRender(SceneSharedState& state, int scriptRef, float dt) {
    SceneFrameResult result;

    elapsedSeconds_ += dt;

    if (scriptRef == LUA_NOREF) {
        return result;
    }

    lua_rawgeti(L_, LUA_REGISTRYINDEX, scriptRef);
    lua_getfield(L_, -1, "render");
    lua_remove(L_, -2);

    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return result;
    }

    pushSceneState(L_, state); // push state table as first argument
    lua_pushnumber(L_, dt);
    lua_pushnumber(L_, elapsedSeconds_);

    if (lua_pcall(L_, 3, 1, 0) != LUA_OK) {
        spdlog::error("GameScene render error: {}", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return result;
    }

    // Parse optional result table returned by Lua
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "requestQuit");
        if (lua_toboolean(L_, -1)) { result.requestQuit = true; }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "requestApplySettings");
        if (lua_toboolean(L_, -1)) { result.requestApplySettings = true; }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "requestAcceptDisplayChanges");
        if (lua_toboolean(L_, -1)) { result.requestAcceptDisplayChanges = true; }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "requestRevertDisplayChanges");
        if (lua_toboolean(L_, -1)) { result.requestRevertDisplayChanges = true; }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "requestTransition");
        if (lua_toboolean(L_, -1)) {
            result.requestTransition = true;

            lua_pop(L_, 1);
            lua_getfield(L_, -1, "target");
            if (lua_isstring(L_, -1)) {
                result.transitionTarget = SceneIdFromString(lua_tostring(L_, -1));
            }

            lua_pop(L_, 1);
            lua_getfield(L_, -1, "message");
            if (lua_isstring(L_, -1)) {
                result.transitionMessage = lua_tostring(L_, -1);
            }

            lua_pop(L_, 1);
            lua_getfield(L_, -1, "duration");
            if (lua_isnumber(L_, -1)) {
                result.transitionMinDurationSeconds = static_cast<float>(lua_tonumber(L_, -1));
            }
            lua_pop(L_, 1);
        } else {
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1); // pop result table or nil
    return result;
}

SceneId SceneIdFromString(const std::string& name) {
    const std::string n(name);
    if (n == "MainMenu") {
        return SceneId::MainMenu;
    }
    if (n == "Options") {
        return SceneId::Options;
    }
    if (n == "LevelSelection") {
        return SceneId::LevelSelection;
    }
    if (n == "PlayLevel") {
        return SceneId::PlayLevel;
    }
    return SceneId::Splash;
}