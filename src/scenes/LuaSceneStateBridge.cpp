#include "scenes/LuaSceneStateBridge.hpp"

#include "lua.hpp"

#include <algorithm>

namespace {

bool readLuaBooleanField(lua_State* L, int tableIndex, const char* fieldName, bool& outValue) {
    lua_getfield(L, tableIndex, fieldName);
    const bool hasValue = !lua_isnil(L, -1);
    if (hasValue) {
        outValue = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    return hasValue;
}

bool readLuaIntegerField(lua_State* L, int tableIndex, const char* fieldName, int& outValue) {
    lua_getfield(L, tableIndex, fieldName);
    const bool hasValue = lua_isinteger(L, -1);
    if (hasValue) {
        outValue = static_cast<int>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return hasValue;
}

bool readLuaNumberField(lua_State* L, int tableIndex, const char* fieldName, float& outValue) {
    lua_getfield(L, tableIndex, fieldName);
    const bool hasValue = lua_isnumber(L, -1);
    if (hasValue) {
        outValue = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
    return hasValue;
}

} // namespace

void LuaSceneStateBridge::apply(lua_State* L, int stateTableIndex, SceneSharedState& state) {
    const int stateIndex = lua_absindex(L, stateTableIndex);
    if (!lua_istable(L, stateIndex)) {
        return;
    }

    int selectedDisplayModeIndex = state.selectedDisplayModeIndex;
    if (readLuaIntegerField(L, stateIndex, "selectedDisplayModeIndex", selectedDisplayModeIndex)) {
        if (!state.displayModes.empty()) {
            selectedDisplayModeIndex =
                std::clamp(selectedDisplayModeIndex, 0, static_cast<int>(state.displayModes.size() - 1));
        } else {
            selectedDisplayModeIndex = 0;
        }
        state.selectedDisplayModeIndex = selectedDisplayModeIndex;
    }

    lua_getfield(L, stateIndex, "settings");
    if (lua_istable(L, -1)) {
        const int settingsIndex = lua_gettop(L);

        bool boolValue = false;
        int intValue = 0;
        float floatValue = 0.0f;

        if (readLuaBooleanField(L, settingsIndex, "fullscreen", boolValue)) {
            state.settings.fullscreen = boolValue;
        }
        if (readLuaBooleanField(L, settingsIndex, "exclusiveFullscreen", boolValue)) {
            state.settings.exclusiveFullscreen = boolValue;
        }
        if (readLuaBooleanField(L, settingsIndex, "vSyncEnabled", boolValue)) {
            state.settings.vSyncEnabled = boolValue;
        }
        if (readLuaIntegerField(L, settingsIndex, "displayWidth", intValue)) {
            state.settings.displayWidth = intValue;
        }
        if (readLuaIntegerField(L, settingsIndex, "displayHeight", intValue)) {
            state.settings.displayHeight = intValue;
        }
        if (readLuaIntegerField(L, settingsIndex, "refreshRate", intValue)) {
            state.settings.refreshRate = intValue;
        }
        if (readLuaIntegerField(L, settingsIndex, "graphicsQuality", intValue)) {
            state.settings.graphicsQuality = intValue;
        }
        if (readLuaNumberField(L, settingsIndex, "masterVolume", floatValue)) {
            state.settings.masterVolume = floatValue;
        }
        if (readLuaNumberField(L, settingsIndex, "musicVolume", floatValue)) {
            state.settings.musicVolume = floatValue;
        }
        if (readLuaNumberField(L, settingsIndex, "sfxVolume", floatValue)) {
            state.settings.sfxVolume = floatValue;
        }
        if (readLuaBooleanField(L, settingsIndex, "muteWhenUnfocused", boolValue)) {
            state.settings.muteWhenUnfocused = boolValue;
        }
    }
    lua_pop(L, 1);
}

void LuaSceneStateBridge::push(lua_State* L, const SceneSharedState& state) {
    lua_newtable(L);

    lua_pushboolean(L, state.loadingComplete);
    lua_setfield(L, -2, "loadingComplete");

    lua_pushstring(L, state.activeLevelName.c_str());
    lua_setfield(L, -2, "activeLevelName");

    lua_pushstring(L, state.activeLevelAssetPath.c_str());
    lua_setfield(L, -2, "activeLevelAssetPath");

    lua_pushstring(L, state.activeLevelScriptPath.c_str());
    lua_setfield(L, -2, "activeLevelScriptPath");

    lua_pushboolean(L, state.displayConfirmationActive);
    lua_setfield(L, -2, "displayConfirmationActive");

    lua_pushnumber(L, state.displayConfirmationSecondsRemaining);
    lua_setfield(L, -2, "displayConfirmationSecondsRemaining");

    lua_pushinteger(L, state.selectedDisplayModeIndex);
    lua_setfield(L, -2, "selectedDisplayModeIndex");

    lua_newtable(L);
    for (std::size_t i = 0; i < state.displayModes.size(); ++i) {
        const auto& mode = state.displayModes[i];
        lua_newtable(L);
        lua_pushinteger(L, mode.width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, mode.height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, mode.refreshRate);
        lua_setfield(L, -2, "refreshRate");
        lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "displayModes");

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

    lua_newtable(L);
    lua_pushboolean(L, state.settings.fullscreen);
    lua_setfield(L, -2, "fullscreen");
    lua_pushboolean(L, state.settings.exclusiveFullscreen);
    lua_setfield(L, -2, "exclusiveFullscreen");
    lua_pushboolean(L, state.settings.vSyncEnabled);
    lua_setfield(L, -2, "vSyncEnabled");
    lua_pushinteger(L, state.settings.displayWidth);
    lua_setfield(L, -2, "displayWidth");
    lua_pushinteger(L, state.settings.displayHeight);
    lua_setfield(L, -2, "displayHeight");
    lua_pushinteger(L, state.settings.refreshRate);
    lua_setfield(L, -2, "refreshRate");
    lua_pushinteger(L, state.settings.graphicsQuality);
    lua_setfield(L, -2, "graphicsQuality");
    lua_pushnumber(L, state.settings.masterVolume);
    lua_setfield(L, -2, "masterVolume");
    lua_pushnumber(L, state.settings.musicVolume);
    lua_setfield(L, -2, "musicVolume");
    lua_pushnumber(L, state.settings.sfxVolume);
    lua_setfield(L, -2, "sfxVolume");
    lua_pushboolean(L, state.settings.muteWhenUnfocused);
    lua_setfield(L, -2, "muteWhenUnfocused");
    lua_setfield(L, -2, "settings");
}
