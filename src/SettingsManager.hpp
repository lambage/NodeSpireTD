#pragma once

#include "AppSettings.hpp"

#include <filesystem>

class SettingsManager {
  public:
    explicit SettingsManager(std::filesystem::path settingsFilePath = "config/settings.json");

    AppSettings loadOrCreateDefaults() const;
    bool save(const AppSettings& settings) const;

  private:
    std::filesystem::path settingsFilePath_;
};
