#pragma once

#include "AppSettings.hpp"

#include <imgui.h>
#include <string>
#include <unordered_set>
#include <vector>

class VulkanContext;
class AudioEngine;

namespace multiplayer {
class MultiplayerSession;
class PlayerProfileStore;
}

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
    // Persistent, scene-independent LAN session (see MultiplayerSession.hpp) owned by the app
    // runtime, not any single scene. LobbyScene hosts/joins/leaves parties through it;
    // PlayLevelScene reuses the same already-established connection for match-level join/command/
    // snapshot traffic instead of creating a new one. Never null.
    multiplayer::MultiplayerSession* multiplayerSession = nullptr;
    // Persistent local player identity (username + hidden UUID), owned by the app runtime. Never
    // null. See PlayerProfileStore.hpp.
    multiplayer::PlayerProfileStore* playerProfileStore = nullptr;
    VulkanContext* vulkanContext = nullptr;
    AudioEngine* audioEngine = nullptr;
    ImFont* headingFont = nullptr;
    ImFont* titleFont = nullptr;
};

std::string modeLabel(const DisplayModeOption& mode);

