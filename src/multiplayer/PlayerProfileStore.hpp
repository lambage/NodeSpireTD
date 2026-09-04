#pragma once

#include "multiplayer/PlayerProfile.hpp"

#include <filesystem>

namespace multiplayer {

// Loads/creates the local player's profile at construction (generating a fresh UUID the first
// time the game runs) and persists changes to disk. Owned by the app runtime alongside
// SettingsManager, not by any scene -- see SceneSharedState::playerProfileStore.
class PlayerProfileStore {
  public:
    explicit PlayerProfileStore(std::filesystem::path profileFilePath = "config/profile.json");

    const PlayerProfile& profile() const { return profile_; }

    // Trims and validates displayName (matches LocalHostPartyGate's display-name length limit),
    // persists it, and updates the in-memory profile. Returns false and leaves the profile
    // unchanged if the trimmed name is empty or too long.
    bool setDisplayName(const std::string& displayName);

  private:
    void load();
    bool save() const;

    std::filesystem::path profileFilePath_;
    PlayerProfile profile_;
};

} // namespace multiplayer
