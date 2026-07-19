#pragma once

#include "AppSettings.hpp"

#include <imgui.h>
#include <string>
#include <vector>

class VulkanContext;

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
    std::string& activeLevelAssetPath;
    std::string& activeLevelScriptPath;
    VulkanContext* vulkanContext = nullptr;
    ImFont* headingFont = nullptr;
    ImFont* titleFont = nullptr;
};

std::string modeLabel(const DisplayModeOption& mode);
