#include "scenes/GameScene.hpp"

#include "ImGuiLayer.hpp"
#include "LuaStateBootstrap.hpp"
#include "VulkanContext.hpp"

#include <SFML/Window/Event.hpp>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace {

GameScene* luaSceneSelf(lua_State* L) {
    return static_cast<GameScene*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int pushCommandResult(lua_State* L, bool ok, const char* reason) {
    lua_newtable(L);
    lua_pushboolean(L, ok);
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, reason);
    lua_setfield(L, -2, "reason");
    return 1;
}

bool tryParseSceneId(const std::string& name, SceneId& outSceneId) {
    if (name == "Splash") {
        outSceneId = SceneId::Splash;
        return true;
    }
    if (name == "MainMenu") {
        outSceneId = SceneId::MainMenu;
        return true;
    }
    if (name == "Options") {
        outSceneId = SceneId::Options;
        return true;
    }
    if (name == "Lobby") {
        outSceneId = SceneId::Lobby;
        return true;
    }
    if (name == "PlayLevel") {
        outSceneId = SceneId::PlayLevel;
        return true;
    }
    return false;
}

} // namespace

GameScene::~GameScene() = default;

void GameScene::clearPendingAudioCallbacks() {
    if (!L_) {
        pendingAudioCallbacks_.clear();
        return;
    }

    for (const PendingAudioCallback& pending : pendingAudioCallbacks_) {
        if (pending.callbackRef != LUA_NOREF) {
            luaL_unref(L_, LUA_REGISTRYINDEX, pending.callbackRef);
        }
    }
    pendingAudioCallbacks_.clear();
}

void GameScene::processPendingAudioCallbacks() {
    if (!L_ || pendingAudioCallbacks_.empty()) {
        return;
    }

    for (std::size_t i = 0; i < pendingAudioCallbacks_.size();) {
        PendingAudioCallback& pending = pendingAudioCallbacks_[i];
        const bool playing = isAudioHandlePlaying(pending.handle);
        bool fireCallback = false;

        if (playing) {
            pending.observedPlaying = true;
        } else if (pending.observedPlaying) {
            fireCallback = true;
        } else if (pending.settleFramesRemaining > 0) {
            pending.settleFramesRemaining -= 1;
        } else {
            // If playback never enters a playing state (e.g. decode/load failure),
            // still resolve async callers instead of hanging forever.
            fireCallback = true;
        }

        if (!fireCallback) {
            ++i;
            continue;
        }

        lua_rawgeti(L_, LUA_REGISTRYINDEX, pending.callbackRef);
        if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            spdlog::error("Audio.playAsync callback error: {}", lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }

        luaL_unref(L_, LUA_REGISTRYINDEX, pending.callbackRef);
        pendingAudioCallbacks_.erase(pendingAudioCallbacks_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void GameScene::handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) {
    imguiLayer.processEvent(event);
}

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

void applySceneState(lua_State* L, int stateTableIndex, SceneSharedState& state) {
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
    registerCoreGameplayApi();

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
    clearPendingAudioCallbacks();

    if (scriptRef == LUA_NOREF) {
        releaseAllAudioHandles();
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

    releaseAllAudioHandles();
    luaL_unref(L_, LUA_REGISTRYINDEX, scriptRef);
}

void GameScene::luaOnRender(SceneSharedState& state, int scriptRef, float dt) {
    elapsedSeconds_ += dt;

    setActiveAudioAssetKeys(&state.activeAudioAssetKeys);
    processPendingAudioCallbacks();

    if (scriptRef == LUA_NOREF) {
        setActiveAudioAssetKeys(nullptr);
        return;
    }

    lua_rawgeti(L_, LUA_REGISTRYINDEX, scriptRef);
    lua_getfield(L_, -1, "render");
    lua_remove(L_, -2);

    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        setActiveAudioAssetKeys(nullptr);
        return;
    }

    pushSceneState(L_, state);
    const int stateRef = luaL_ref(L_, LUA_REGISTRYINDEX);

    lua_rawgeti(L_, LUA_REGISTRYINDEX, stateRef);
    lua_pushnumber(L_, dt);
    lua_pushnumber(L_, elapsedSeconds_);

    if (lua_pcall(L_, 3, 0, 0) != LUA_OK) {
        spdlog::error("GameScene render error: {}", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        luaL_unref(L_, LUA_REGISTRYINDEX, stateRef);
        setActiveAudioAssetKeys(nullptr);
        return;
    }

    lua_rawgeti(L_, LUA_REGISTRYINDEX, stateRef);
    applySceneState(L_, -1, state);
    lua_pop(L_, 1);
    luaL_unref(L_, LUA_REGISTRYINDEX, stateRef);

    setActiveAudioAssetKeys(nullptr);
}

void GameScene::registerCoreGameplayApi() {
    if (!L_) {
        return;
    }

    lua_getglobal(L_, "Gameplay");
    if (!lua_istable(L_, -1)) {
        lua_pop(L_, 1);
        lua_newtable(L_);
    }
    const int gameplayTable = lua_gettop(L_);

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            luaSceneSelf(L)->requestQuit();
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestQuit");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            luaSceneSelf(L)->requestApplySettings();
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestApplySettings");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            luaSceneSelf(L)->requestAcceptDisplayChanges();
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestAcceptDisplayChanges");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            luaSceneSelf(L)->requestRevertDisplayChanges();
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestRevertDisplayChanges");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);

            SceneId sceneId = SceneId::MainMenu;
            if (lua_isinteger(L, 1)) {
                const int sceneValue = static_cast<int>(lua_tointeger(L, 1));
                const int maxSceneValue = static_cast<int>(SceneId::PlayLevel);
                if (sceneValue < 0 || sceneValue > maxSceneValue) {
                    return pushCommandResult(L, false, "invalid scene enum value");
                }
                sceneId = static_cast<SceneId>(sceneValue);
            } else if (lua_isstring(L, 1)) {
                const std::string sceneName = lua_tostring(L, 1);
                if (!tryParseSceneId(sceneName, sceneId)) {
                    return pushCommandResult(L, false, "invalid scene name");
                }
            } else {
                return pushCommandResult(L, false, "expected scene enum or scene name");
            }

            const char* message = luaL_optstring(L, 2, "");
            const float duration = std::max(0.0f, static_cast<float>(luaL_optnumber(L, 3, 0.0)));
            self->requestScene(sceneId, message != nullptr ? message : "", duration);
            return pushCommandResult(L, true, "queued");
        },
        1);
    lua_setfield(L_, gameplayTable, "requestScene");

    auto registerPlayAudioFn = [&](const char* fieldName, AudioChannel channel) {
        lua_pushlightuserdata(L_, this);
        lua_pushinteger(L_, static_cast<lua_Integer>(channel));
        lua_pushcclosure(
            L_,
            [](lua_State* L) -> int {
                auto* self = luaSceneSelf(L);
                const AudioChannel channel = static_cast<AudioChannel>(lua_tointeger(L, lua_upvalueindex(2)));
                const std::string path = luaL_checkstring(L, 1);
                if (path.empty()) {
                    return pushCommandResult(L, false, "expected a non-empty audio file path");
                }

                bool loop = false;
                float gain = 1.0f;

                if (lua_gettop(L) >= 2) {
                    if (lua_istable(L, 2)) {
                        lua_getfield(L, 2, "loop");
                        if (!lua_isnil(L, -1)) {
                            loop = lua_toboolean(L, -1) != 0;
                        }
                        lua_pop(L, 1);

                        lua_getfield(L, 2, "gain");
                        if (lua_isnumber(L, -1)) {
                            gain = static_cast<float>(lua_tonumber(L, -1));
                        }
                        lua_pop(L, 1);
                    } else if (lua_isboolean(L, 2)) {
                        loop = lua_toboolean(L, 2) != 0;
                        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) {
                            gain = static_cast<float>(lua_tonumber(L, 3));
                        }
                    } else if (lua_isnumber(L, 2)) {
                        gain = static_cast<float>(lua_tonumber(L, 2));
                    } else {
                        return pushCommandResult(L, false, "expected options table, loop flag, or gain");
                    }
                }

                self->requestPlayAudio(path, channel, loop, gain);
                return pushCommandResult(L, true, "queued");
            },
            2);
        lua_setfield(L_, gameplayTable, fieldName);
    };

    registerPlayAudioFn("playMusic", AudioChannel::Music);
    registerPlayAudioFn("playSfx", AudioChannel::Sfx);

    auto registerPreloadAudioFn = [&](const char* fieldName, AudioChannel channel) {
        lua_pushlightuserdata(L_, this);
        lua_pushinteger(L_, static_cast<lua_Integer>(channel));
        lua_pushcclosure(
            L_,
            [](lua_State* L) -> int {
                auto* self = luaSceneSelf(L);
                const AudioChannel channel = static_cast<AudioChannel>(lua_tointeger(L, lua_upvalueindex(2)));
                const std::string path = luaL_checkstring(L, 1);
                if (path.empty()) {
                    return pushCommandResult(L, false, "expected a non-empty audio file path");
                }

                self->requestPreloadAudio(path, channel);
                return pushCommandResult(L, true, "queued");
            },
            2);
        lua_setfield(L_, gameplayTable, fieldName);
    };

    registerPreloadAudioFn("preloadSfx", AudioChannel::Sfx);

    lua_newtable(L_);
    const int audioTable = lua_gettop(L_);

    auto registerAudioLoadFn = [&](const char* fieldName, AudioChannel channel) {
        lua_pushlightuserdata(L_, this);
        lua_pushinteger(L_, static_cast<lua_Integer>(channel));
        lua_pushcclosure(
            L_,
            [](lua_State* L) -> int {
                auto* self = luaSceneSelf(L);
                const AudioChannel channel = static_cast<AudioChannel>(lua_tointeger(L, lua_upvalueindex(2)));
                const std::string path = luaL_checkstring(L, 1);
                if (path.empty()) {
                    lua_pushnil(L);
                    lua_pushstring(L, "expected a non-empty audio file path");
                    return 2;
                }

                const int handle = self->loadAudioHandle(path, channel);
                if (handle <= 0) {
                    lua_pushnil(L);
                    lua_pushstring(L, "failed to create audio handle");
                    return 2;
                }

                lua_pushinteger(L, static_cast<lua_Integer>(handle));
                return 1;
            },
            2);
        lua_setfield(L_, audioTable, fieldName);
    };

    auto registerAudioPlayFn = [&](const char* fieldName, bool isAsync) {
        lua_pushlightuserdata(L_, this);
        lua_pushboolean(L_, isAsync ? 1 : 0);
        lua_pushcclosure(
            L_,
            [](lua_State* L) -> int {
                auto* self = luaSceneSelf(L);
                const bool isAsync = lua_toboolean(L, lua_upvalueindex(2)) != 0;
                const int argCount = lua_gettop(L);

                if (isAsync) {
                    if (argCount < 2) {
                        return pushCommandResult(L, false, "expected handle and callback");
                    }
                    if (!lua_isfunction(L, argCount)) {
                        return pushCommandResult(L, false, "last argument must be callback function");
                    }
                }

                const int handle = static_cast<int>(luaL_checkinteger(L, 1));

                bool loop = false;
                float gain = 1.0f;
                const int optionsArgCount = isAsync ? (argCount - 2) : (argCount - 1);
                if (optionsArgCount >= 1) {
                    if (lua_istable(L, 2)) {
                        lua_getfield(L, 2, "loop");
                        if (!lua_isnil(L, -1)) {
                            loop = lua_toboolean(L, -1) != 0;
                        }
                        lua_pop(L, 1);

                        lua_getfield(L, 2, "gain");
                        if (lua_isnumber(L, -1)) {
                            gain = static_cast<float>(lua_tonumber(L, -1));
                        }
                        lua_pop(L, 1);
                    } else if (lua_isboolean(L, 2)) {
                        loop = lua_toboolean(L, 2) != 0;
                        if (optionsArgCount >= 2 && lua_isnumber(L, 3)) {
                            gain = static_cast<float>(lua_tonumber(L, 3));
                        }
                    } else if (lua_isnumber(L, 2)) {
                        gain = static_cast<float>(lua_tonumber(L, 2));
                    } else {
                        return pushCommandResult(L, false, "expected options table, loop flag, or gain");
                    }
                }

                std::string reason;
                const bool ok = self->playAudioHandle(handle, loop, gain, reason);
                if (!ok) {
                    return pushCommandResult(L, false, reason.c_str());
                }

                if (!isAsync) {
                    return pushCommandResult(L, true, "queued");
                }

                lua_pushvalue(L, argCount);
                const int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

                GameScene::PendingAudioCallback pending;
                pending.handle = handle;
                pending.callbackRef = callbackRef;
                self->pendingAudioCallbacks_.push_back(std::move(pending));
                return pushCommandResult(L, true, "queued");
            },
            2);
        lua_setfield(L_, audioTable, fieldName);
    };

    registerAudioLoadFn("loadMusic", AudioChannel::Music);
    registerAudioLoadFn("loadSfx", AudioChannel::Sfx);
    registerAudioPlayFn("playMusic", false);
    registerAudioPlayFn("playSfx", false);
    registerAudioPlayFn("playMusicAsync", true);
    registerAudioPlayFn("playSfxAsync", true);

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const int handle = static_cast<int>(luaL_checkinteger(L, 1));

            std::string reason;
            const bool ok = self->releaseAudioHandle(handle, reason);
            return pushCommandResult(L, ok, reason.c_str());
        },
        1);
    lua_setfield(L_, audioTable, "release");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const int handle = static_cast<int>(luaL_checkinteger(L, 1));
            lua_pushboolean(L, self->isAudioHandlePlaying(handle));
            return 1;
        },
        1);
    lua_setfield(L_, audioTable, "isPlaying");

    lua_setglobal(L_, "Audio");

    lua_newtable(L_);
    lua_pushinteger(L_, static_cast<lua_Integer>(SceneId::Splash));
    lua_setfield(L_, -2, "Splash");
    lua_pushinteger(L_, static_cast<lua_Integer>(SceneId::MainMenu));
    lua_setfield(L_, -2, "MainMenu");
    lua_pushinteger(L_, static_cast<lua_Integer>(SceneId::Options));
    lua_setfield(L_, -2, "Options");
    lua_pushinteger(L_, static_cast<lua_Integer>(SceneId::Lobby));
    lua_setfield(L_, -2, "Lobby");
    lua_pushinteger(L_, static_cast<lua_Integer>(SceneId::PlayLevel));
    lua_setfield(L_, -2, "PlayLevel");
    lua_setfield(L_, gameplayTable, "Scene");

    lua_setglobal(L_, "Gameplay");
}

SceneId SceneIdFromString(const std::string& name) {
    SceneId parsed = SceneId::Splash;
    if (tryParseSceneId(name, parsed)) {
        return parsed;
    }
    return SceneId::Splash;
}