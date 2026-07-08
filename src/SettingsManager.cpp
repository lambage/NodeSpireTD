#include "SettingsManager.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>

namespace {

nlohmann::json settingsToJson(const AppSettings& settings) {
    return {
        {"fullscreen", settings.fullscreen},
        {"vSyncEnabled", settings.vSyncEnabled},
        {"displayWidth", settings.displayWidth},
        {"displayHeight", settings.displayHeight},
        {"refreshRate", settings.refreshRate},
        {"graphicsQuality", settings.graphicsQuality},
        {"masterVolume", settings.masterVolume},
        {"musicVolume", settings.musicVolume},
        {"sfxVolume", settings.sfxVolume},
        {"muteWhenUnfocused", settings.muteWhenUnfocused},
    };
}

AppSettings settingsFromJson(const nlohmann::json& json) {
    AppSettings settings;

    settings.fullscreen = json.value("fullscreen", settings.fullscreen);
    settings.vSyncEnabled = json.value("vSyncEnabled", settings.vSyncEnabled);
    settings.displayWidth = json.value("displayWidth", settings.displayWidth);
    settings.displayHeight = json.value("displayHeight", settings.displayHeight);
    settings.refreshRate = json.value("refreshRate", settings.refreshRate);
    settings.graphicsQuality = json.value("graphicsQuality", settings.graphicsQuality);
    settings.masterVolume = json.value("masterVolume", settings.masterVolume);
    settings.musicVolume = json.value("musicVolume", settings.musicVolume);
    settings.sfxVolume = json.value("sfxVolume", settings.sfxVolume);
    settings.muteWhenUnfocused = json.value("muteWhenUnfocused", settings.muteWhenUnfocused);

    return settings;
}

} // namespace

SettingsManager::SettingsManager(std::filesystem::path settingsFilePath)
    : settingsFilePath_(std::move(settingsFilePath)) {}

AppSettings SettingsManager::loadOrCreateDefaults() const {
    AppSettings defaults;

    if (!std::filesystem::exists(settingsFilePath_)) {
        if (!save(defaults)) {
            spdlog::warn("Failed to create default settings file at {}", settingsFilePath_.string());
        }
        return defaults;
    }

    std::ifstream input(settingsFilePath_);
    if (!input.is_open()) {
        spdlog::warn("Failed to open settings file: {}", settingsFilePath_.string());
        return defaults;
    }

    try {
        nlohmann::json json;
        input >> json;
        return settingsFromJson(json);
    } catch (const std::exception& ex) {
        spdlog::warn("Settings parse failed ({}). Using defaults.", ex.what());
        return defaults;
    }
}

bool SettingsManager::save(const AppSettings& settings) const {
    try {
        std::filesystem::create_directories(settingsFilePath_.parent_path());

        std::ofstream output(settingsFilePath_, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << settingsToJson(settings).dump(4);
        output << '\n';
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save settings ({}).", ex.what());
        return false;
    }
}
