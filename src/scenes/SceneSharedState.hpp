#pragma once

#include "AppSettings.hpp"

#include <imgui.h>
#include <string>
#include <vector>

struct DisplayModeOption {
    int width = 1280;
    int height = 720;
    int refreshRate = 60;
};

struct SceneSharedState {
    AppSettings& settings;
    const std::vector<DisplayModeOption>& displayModes;
    int& selectedDisplayModeIndex;
    bool displayConfirmationActive = false;
    float displayConfirmationSecondsRemaining = 0.0f;
    bool loadingComplete = false;
    std::string& activeLevelName;
    ImFont* headingFont = nullptr;
    ImFont* titleFont = nullptr;
    bool hasSplashTexture = false;
    uint32_t splashTextureWidth = 0;
    uint32_t splashTextureHeight = 0;
    ImTextureRef splashTextureRef;
};

std::string modeLabel(const DisplayModeOption& mode);
