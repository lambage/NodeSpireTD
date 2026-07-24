#pragma once

#include "AppSettings.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sf {
class Music;
class Sound;
class SoundBuffer;
} // namespace sf

enum class AudioChannel {
    Music,
    Sfx
};

// Owns all live SFML audio playback (music + sfx) and the sfx buffer cache.
// Effective settings (post master/mute mixing) must be refreshed once per
// frame via setEffectiveSettings() before calling play()/update() so that
// volumes stay in sync with the current focus/mute state.
class AudioEngine {
  public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    void setEffectiveSettings(const AppSettings& effectiveSettings);

    void preload(const std::string& path, AudioChannel channel);
    void release(const std::string& path, AudioChannel channel);
    void play(const std::string& path, AudioChannel channel, bool loop = false, float gain = 1.0f);

    // Advances playback age, refreshes volumes, and prunes finished sounds.
    // Call once per frame.
    void update(float dt);

    [[nodiscard]] const std::unordered_set<std::string>& activeAssetKeys() const {
        return activeAssetKeys_;
    }

  private:
    struct MusicPlayback {
        std::string path;
        float gain = 1.0f;
        float ageSeconds = 0.0f;
        std::unique_ptr<sf::Music> music;
    };

    struct SfxPlayback {
        std::string path;
        float gain = 1.0f;
        float ageSeconds = 0.0f;
        std::shared_ptr<sf::SoundBuffer> buffer;
        std::unique_ptr<sf::Sound> sound;
    };

    std::shared_ptr<sf::SoundBuffer> getOrLoadSfxBuffer(const std::string& path);
    void refreshActiveAssetKeys();

    AppSettings effectiveSettings_{};
    std::vector<MusicPlayback> musicPlaybacks_;
    std::vector<SfxPlayback> sfxPlaybacks_;
    std::unordered_map<std::string, std::shared_ptr<sf::SoundBuffer>> sfxBufferCache_;
    std::unordered_set<std::string> activeAssetKeys_;
};
