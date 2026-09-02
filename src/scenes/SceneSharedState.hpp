#pragma once

#include "AppSettings.hpp"

#include <imgui.h>
#include <string>
#include <unordered_set>
#include <vector>

class VulkanContext;
class AudioEngine;

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
    const std::unordered_set<std::string>& activeAudioAssetKeys;
    // Multiplayer connection intent set by LobbyScene before requesting a transition to
    // PlayLevel; consumed (read-only) by PlayLevelScene::onEnter(). If joinRemoteHostAddress is
    // non-empty, PlayLevelScene connects as a client instead of becoming host/single-player.
    bool& hostMultiplayerMatch;
    unsigned short& multiplayerPort;
    std::string& joinRemoteHostAddress;
    VulkanContext* vulkanContext = nullptr;
    AudioEngine* audioEngine = nullptr;
    ImFont* headingFont = nullptr;
    ImFont* titleFont = nullptr;
};

std::string modeLabel(const DisplayModeOption& mode);

