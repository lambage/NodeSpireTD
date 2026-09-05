#pragma once

#include "AppSettings.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class AudioChannel {
    Music,
    Sfx
};

// Owns all live SDL3_mixer audio playback (music + sfx) and the sfx audio
// cache. MIX_Audio*/MIX_Track* handles are kept out of this header (behind
// shared_ptr<void> with a type-erased deleter, and a void* for the mixer
// device) so UI consumers don't need SDL3_mixer on their
// include path -- see AudioEngine.cpp for the real types.
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
        std::shared_ptr<void> audio; // MIX_Audio*, freed via MIX_DestroyAudio
        std::shared_ptr<void> track; // MIX_Track*, freed via MIX_DestroyTrack
    };

    struct SfxPlayback {
        std::string path;
        float gain = 1.0f;
        float ageSeconds = 0.0f;
        std::shared_ptr<void> audio; // MIX_Audio*, freed via MIX_DestroyAudio
        std::shared_ptr<void> track; // MIX_Track*, freed via MIX_DestroyTrack
    };

    std::shared_ptr<void> getOrLoadSfxAudio(const std::string& path);
    void refreshActiveAssetKeys();

    bool audioReady_ = false;
    void* mixer_ = nullptr; // MIX_Mixer*, freed via MIX_DestroyMixer
    AppSettings effectiveSettings_{};
    std::vector<MusicPlayback> musicPlaybacks_;
    std::vector<SfxPlayback> sfxPlaybacks_;
    std::unordered_map<std::string, std::shared_ptr<void>> sfxBufferCache_;
    std::unordered_set<std::string> activeAssetKeys_;
};
