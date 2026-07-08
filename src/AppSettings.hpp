#pragma once

struct AppSettings {
    bool fullscreen = false;
    bool vSyncEnabled = true;
    int displayWidth = 1280;
    int displayHeight = 720;
    int refreshRate = 60;
    int graphicsQuality = 2;
    float masterVolume = 0.8f;
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    bool muteWhenUnfocused = true;
};
