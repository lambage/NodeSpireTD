#include "lambui/scenes/OptionsScene.hpp"

#include "AudioEngine.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"
extern "C" {
#include <lua.h>
}

#include <SDL3/SDL.h>
#include <LambUI/UITypes.h>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace NodeSpireUi {
namespace {

constexpr std::pair<const char*, bool AppSettings::*> boolFields[] = {
    {"fullscreen", &AppSettings::fullscreen}, {"exclusiveFullscreen", &AppSettings::exclusiveFullscreen},
    {"vSyncEnabled", &AppSettings::vSyncEnabled}, {"muteWhenUnfocused", &AppSettings::muteWhenUnfocused}
};
constexpr std::pair<const char*, int AppSettings::*> intFields[] = {
    {"displayWidth", &AppSettings::displayWidth}, {"displayHeight", &AppSettings::displayHeight},
    {"refreshRate", &AppSettings::refreshRate}, {"graphicsQuality", &AppSettings::graphicsQuality}
};
constexpr std::pair<const char*, float AppSettings::*> volumeFields[] = {
    {"masterVolume", &AppSettings::masterVolume}, {"musicVolume", &AppSettings::musicVolume},
    {"sfxVolume", &AppSettings::sfxVolume}
};

int pushSettings(lua_State* lua, const AppSettings& settings) {
    lua_newtable(lua);
    for (const auto& [name, member] : boolFields) {
        lua_pushboolean(lua, settings.*member);
        lua_setfield(lua, -2, name);
    }
    for (const auto& [name, member] : intFields) {
        lua_pushinteger(lua, settings.*member);
        lua_setfield(lua, -2, name);
    }
    for (const auto& [name, member] : volumeFields) {
        lua_pushnumber(lua, settings.*member);
        lua_setfield(lua, -2, name);
    }
    lua_pushstring(lua, settings.audioDevice.c_str());
    lua_setfield(lua, -2, "audioDevice");
    return 1;
}

AppSettings readSettings(lua_State* lua) {
    if (!lua_istable(lua, 1)) throw std::runtime_error("Invalid settings");
    AppSettings settings;
    for (const auto& [name, member] : boolFields) {
        lua_getfield(lua, 1, name);
        if (!lua_isboolean(lua, -1)) throw std::runtime_error("Invalid setting: " + std::string(name));
        settings.*member = lua_toboolean(lua, -1) != 0;
        lua_pop(lua, 1);
    }
    for (const auto& [name, member] : intFields) {
        lua_getfield(lua, 1, name);
        if (!lua_isinteger(lua, -1) || lua_tointeger(lua, -1) < 0 || lua_tointeger(lua, -1) > 32768)
            throw std::runtime_error("Invalid setting: " + std::string(name));
        settings.*member = static_cast<int>(lua_tointeger(lua, -1));
        lua_pop(lua, 1);
    }
    for (const auto& [name, member] : volumeFields) {
        lua_getfield(lua, 1, name);
        const auto value = lua_tonumber(lua, -1);
        if (!lua_isnumber(lua, -1) || !std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Invalid volume");
        settings.*member = static_cast<float>(value);
        lua_pop(lua, 1);
    }
    lua_getfield(lua, 1, "audioDevice");
    if (lua_type(lua, -1) != LUA_TSTRING) throw std::runtime_error("Invalid audio device");
    settings.audioDevice = lua_tostring(lua, -1);
    lua_pop(lua, 1);
    if (settings.displayWidth < 640 || settings.displayHeight < 480 || settings.refreshRate < 1)
        throw std::runtime_error("Invalid display mode");
    return settings;
}

bool displayChanged(const AppSettings& before, const AppSettings& after) {
    return before.fullscreen != after.fullscreen || before.exclusiveFullscreen != after.exclusiveFullscreen ||
           before.displayWidth != after.displayWidth || before.displayHeight != after.displayHeight ||
           before.refreshRate != after.refreshRate;
}

void applyDisplay(SDL_Window* window, const AppSettings& settings) {
    SDL_DisplayMode mode{};
    const bool exclusive = settings.fullscreen && settings.exclusiveFullscreen;
    if (exclusive && !SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window), settings.displayWidth,
                         settings.displayHeight, static_cast<float>(settings.refreshRate), true, &mode))
        throw std::runtime_error("Display mode unavailable");
    if (!SDL_SetWindowFullscreen(window, false) ||
        !SDL_SetWindowFullscreenMode(window, exclusive ? &mode : nullptr) ||
        !SDL_SetWindowSize(window, settings.displayWidth, settings.displayHeight) ||
        !SDL_SetWindowFullscreen(window, settings.fullscreen))
        throw std::runtime_error("Unable to change display mode: " + std::string(SDL_GetError()));
}

} // namespace

OptionsScene::OptionsScene(lambui_backend::VulkanUiRenderer& renderer, AppSettings& settings,
                           SDL_Window* window, VulkanContext& vulkanContext)
    : LuaUiScene("assets/scenes/Options.lua", renderer), settings_(settings), window_(window),
      vulkanContext_(vulkanContext) {}

OptionsScene& OptionsScene::fromLua(lua_State* lua) {
    return *static_cast<OptionsScene*>(lua_touserdata(lua, lua_upvalueindex(1)));
}

void OptionsScene::bindSceneApi(lua_State* lua, AudioEngine& audio) {
    audio_ = &audio;
    lua_newtable(lua);
    const auto bind = [&](const char* name, lua_CFunction function) {
        lua_pushlightuserdata(lua, this);
        lua_pushcclosure(lua, function, 1);
        lua_setfield(lua, -2, name);
    };
    bind("Get", [](lua_State* state) { return pushSettings(state, fromLua(state).settings_); });
    bind("Defaults", [](lua_State* state) { return pushSettings(state, AppSettings{}); });
    bind("SetVolume", [](lua_State* state) {
        std::string message;
        bool success = false;
        try {
            if (lua_type(state, 1) != LUA_TSTRING || lua_type(state, 2) != LUA_TNUMBER)
                throw std::runtime_error("Invalid volume setting");
            const std::string field = lua_tostring(state, 1);
            const auto value = lua_tonumber(state, 2);
            if (!std::isfinite(value) || value < 0 || value > 1)
                throw std::runtime_error("Invalid volume");
            auto& settings = fromLua(state).settings_;
            bool found = false;
            for (const auto& [name, member] : volumeFields) {
                if (field != name) continue;
                settings.*member = static_cast<float>(value);
                found = true;
                break;
            }
            if (!found) throw std::runtime_error("Unknown volume setting");
            success = SettingsManager().save(settings);
            if (!success) message = "Volume changed, but unable to save config/settings.json";
        } catch (const std::exception& error) {
            message = error.what();
        }
        lua_pushboolean(state, success);
        lua_pushstring(state, message.c_str());
        return 2;
    });
    bind("AudioDevices", [](lua_State* state) {
        lua_newtable(state);
        int index = 1;
        for (const auto& name : fromLua(state).audio_->playbackDeviceNames()) {
            lua_pushstring(state, name.c_str());
            lua_rawseti(state, -2, index++);
        }
        return 1;
    });
    bind("DisplayModes", [](lua_State* state) {
        lua_newtable(state);
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(fromLua(state).window_), &count);
        int outputIndex = 1;
        for (int index = 0; modes && index < count; ++index) {
            if (modes[index]->w < 640 || modes[index]->h < 480) continue;
            lua_newtable(state);
            lua_pushinteger(state, modes[index]->w);
            lua_setfield(state, -2, "width");
            lua_pushinteger(state, modes[index]->h);
            lua_setfield(state, -2, "height");
            lua_pushinteger(state, static_cast<int>(std::round(modes[index]->refresh_rate)));
            lua_setfield(state, -2, "refreshRate");
            lua_rawseti(state, -2, outputIndex++);
        }
        SDL_free(modes);
        return 1;
    });
    bind("Apply", [](lua_State* state) {
        std::string message;
        bool success = false;
        try {
            const auto settings = readSettings(state);
            success = fromLua(state).apply(settings, message);
        } catch (const std::exception& error) {
            message = error.what();
        }
        lua_pushboolean(state, success);
        lua_pushstring(state, success ? "Settings applied" : message.c_str());
        return 2;
    });
    lua_setglobal(lua, "Settings");
}

bool OptionsScene::apply(const AppSettings& settings, std::string& error) {
    const AppSettings previous = settings_;
    const std::string previousDevice = audio_->playbackDeviceName();
    const bool changeDisplay = displayChanged(previous, settings);
    try {
        if (changeDisplay) applyDisplay(window_, settings);
        if (settings.audioDevice != previousDevice && !audio_->setPlaybackDevice(settings.audioDevice))
            throw std::runtime_error("Audio device unavailable");
        vulkanContext_.setVSyncEnabled(settings.vSyncEnabled);
        if (!SettingsManager().save(settings)) throw std::runtime_error("Unable to save config/settings.json");
        settings_ = settings;
        audio_->setEffectiveSettings(settings_);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        try {
            if (changeDisplay) applyDisplay(window_, previous);
            vulkanContext_.setVSyncEnabled(previous.vSyncEnabled);
            if (audio_->playbackDeviceName() != previousDevice && !audio_->setPlaybackDevice(previousDevice))
                error += "; unable to restore audio device";
        } catch (const std::exception& rollbackFailure) {
            error += "; restore failed: " + std::string(rollbackFailure.what());
        }
        return false;
    }
}

SceneTransition OptionsScene::onKeyDown(uint32_t scanCode) {
    return scanCode == LambUI::ScanCode::Escape ? SceneTransition{SceneId::MainMenu} : std::nullopt;
}

} // namespace NodeSpireUi


