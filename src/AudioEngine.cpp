#include "AudioEngine.hpp"

#include <SFML/Audio.hpp>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace {

constexpr float kAudioStartGraceSeconds = 0.05f;

std::string makeAudioAssetKey(const std::string& path, AudioChannel channel) {
    return (channel == AudioChannel::Music ? "music:" : "sfx:") + path;
}

float computeMusicVolumePercent(const AppSettings& settings, float gain) {
    const float mixed = settings.masterVolume * settings.musicVolume * std::clamp(gain, 0.0f, 1.0f);
    return std::clamp(mixed, 0.0f, 1.0f) * 100.0f;
}

float computeSfxVolumePercent(const AppSettings& settings, float gain) {
    const float mixed = settings.masterVolume * settings.sfxVolume * std::clamp(gain, 0.0f, 1.0f);
    return std::clamp(mixed, 0.0f, 1.0f) * 100.0f;
}

#if SFML_VERSION_MAJOR >= 3
void setMusicLoopEnabled(sf::Music& music, bool enabled) {
    music.setLooping(enabled);
}
#else
void setMusicLoopEnabled(sf::Music& music, bool enabled) {
    music.setLoop(enabled);
}
#endif

bool isMusicStopped(const sf::Music& music) {
    return music.getStatus() == sf::SoundSource::Status::Stopped;
}

#if SFML_VERSION_MAJOR >= 3
void setSoundLoopEnabled(sf::Sound& sound, bool enabled) {
    sound.setLooping(enabled);
}
#else
void setSoundLoopEnabled(sf::Sound& sound, bool enabled) {
    sound.setLoop(enabled);
}
#endif

bool isSoundStopped(const sf::Sound& sound) {
    return sound.getStatus() == sf::SoundSource::Status::Stopped;
}

} // namespace

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() = default;

void AudioEngine::setEffectiveSettings(const AppSettings& effectiveSettings) {
    effectiveSettings_ = effectiveSettings;
}

std::shared_ptr<sf::SoundBuffer> AudioEngine::getOrLoadSfxBuffer(const std::string& path) {
    if (auto it = sfxBufferCache_.find(path); it != sfxBufferCache_.end()) {
        return it->second;
    }

    auto loadedBuffer = std::make_shared<sf::SoundBuffer>();
    if (!loadedBuffer->loadFromFile(path)) {
        spdlog::warn("AudioEngine: failed to load sfx file: {}", path);
        return nullptr;
    }

    sfxBufferCache_.emplace(path, loadedBuffer);
    return loadedBuffer;
}

void AudioEngine::preload(const std::string& path, AudioChannel channel) {
    if (path.empty()) {
        spdlog::warn("AudioEngine: rejected empty preload request path.");
        return;
    }

    if (channel == AudioChannel::Sfx) {
        getOrLoadSfxBuffer(path);
    }
}

void AudioEngine::release(const std::string& path, AudioChannel channel) {
    if (path.empty()) {
        return;
    }

    if (channel == AudioChannel::Sfx) {
        sfxBufferCache_.erase(path);
    }
}

void AudioEngine::play(const std::string& path, AudioChannel channel, bool loop, float gain) {
    if (path.empty()) {
        spdlog::warn("AudioEngine: rejected empty playback request path.");
        return;
    }

    if (channel == AudioChannel::Music) {
        auto music = std::make_unique<sf::Music>();
        if (!music->openFromFile(path)) {
            spdlog::warn("AudioEngine: failed to open music file: {}", path);
            return;
        }

        setMusicLoopEnabled(*music, loop);
        music->setVolume(computeMusicVolumePercent(effectiveSettings_, gain));

        // Music is single-instance: replace currently playing track.
        for (auto& existing : musicPlaybacks_) {
            if (existing.music) {
                existing.music->stop();
            }
        }
        musicPlaybacks_.clear();

        music->play();

        MusicPlayback playback;
        playback.path = path;
        playback.gain = gain;
        playback.music = std::move(music);
        musicPlaybacks_.push_back(std::move(playback));
        refreshActiveAssetKeys();
        return;
    }

    std::shared_ptr<sf::SoundBuffer> buffer = getOrLoadSfxBuffer(path);
    if (!buffer) {
        return;
    }

    SfxPlayback playback;
    playback.path = path;
    playback.gain = gain;
    playback.buffer = buffer;
    playback.sound = std::make_unique<sf::Sound>(*playback.buffer);
    setSoundLoopEnabled(*playback.sound, loop);
    playback.sound->setVolume(computeSfxVolumePercent(effectiveSettings_, gain));
    playback.sound->play();
    sfxPlaybacks_.push_back(std::move(playback));
    refreshActiveAssetKeys();
}

void AudioEngine::update(float dt) {
    for (auto& playback : musicPlaybacks_) {
        if (playback.music) {
            playback.ageSeconds += dt;
            playback.music->setVolume(computeMusicVolumePercent(effectiveSettings_, playback.gain));
        }
    }

    musicPlaybacks_.erase(std::remove_if(musicPlaybacks_.begin(), musicPlaybacks_.end(),
                                         [](const MusicPlayback& playback) {
                                             return !playback.music ||
                                                    (playback.ageSeconds >= kAudioStartGraceSeconds &&
                                                     isMusicStopped(*playback.music));
                                         }),
                          musicPlaybacks_.end());

    for (auto& playback : sfxPlaybacks_) {
        if (playback.sound) {
            playback.ageSeconds += dt;
            playback.sound->setVolume(computeSfxVolumePercent(effectiveSettings_, playback.gain));
        }
    }

    sfxPlaybacks_.erase(std::remove_if(sfxPlaybacks_.begin(), sfxPlaybacks_.end(),
                                       [](const SfxPlayback& playback) {
                                           return !playback.sound ||
                                                  (playback.ageSeconds >= kAudioStartGraceSeconds &&
                                                   isSoundStopped(*playback.sound));
                                       }),
                        sfxPlaybacks_.end());

    refreshActiveAssetKeys();
}

void AudioEngine::refreshActiveAssetKeys() {
    activeAssetKeys_.clear();
    for (const auto& playback : musicPlaybacks_) {
        if (playback.music) {
            activeAssetKeys_.insert(makeAudioAssetKey(playback.path, AudioChannel::Music));
        }
    }
    for (const auto& playback : sfxPlaybacks_) {
        if (playback.sound) {
            activeAssetKeys_.insert(makeAudioAssetKey(playback.path, AudioChannel::Sfx));
        }
    }
}
