#pragma once

#include <string>

struct AppSettings {
    bool fullscreen = false;
    bool exclusiveFullscreen = false;
    bool vSyncEnabled = true;
    int displayWidth = 1280;
    int displayHeight = 720;
    int refreshRate = 60;
    int graphicsQuality = 2;
    float masterVolume = 0.8f;
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    std::string audioDevice;
    bool muteWhenUnfocused = true;
};

struct StartupWindowConfig {
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool exclusiveFullscreen = false;
    int refreshRate = 60;
};

inline StartupWindowConfig resolveStartupWindowConfig(const AppSettings& settings) {
    StartupWindowConfig startup{};
    startup.width = settings.displayWidth > 0 ? settings.displayWidth : 1280;
    startup.height = settings.displayHeight > 0 ? settings.displayHeight : 720;
    startup.fullscreen = settings.fullscreen;
    startup.exclusiveFullscreen = settings.exclusiveFullscreen;
    startup.refreshRate = settings.refreshRate > 0 ? settings.refreshRate : 60;
    return startup;
}
