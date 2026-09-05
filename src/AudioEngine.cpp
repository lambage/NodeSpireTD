#include "AudioEngine.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <spdlog/spdlog.h>

// NOTE: SDL3_mixer 3.x replaced the classic SDL2_mixer-style API
// (Mix_OpenAudio/Mix_Music/Mix_Chunk/Mix_PlayChannel) with a new
// MIX_Mixer/MIX_Audio/MIX_Track object model (see vendored
// build/_deps/sdl3_mixer-src/include/SDL3_mixer/SDL_mixer.h). There is no
// Mix_MAX_VOLUME-style integer volume anymore -- MIX_SetTrackGain() takes a
// float where 1.0f is unity gain.

namespace {

constexpr float kAudioStartGraceSeconds = 0.05f;

std::string makeAudioAssetKey(const std::string& path, AudioChannel channel) {
    return (channel == AudioChannel::Music ? "music:" : "sfx:") + path;
}

float computeMusicGain(const AppSettings& settings, float gain) {
    return std::clamp(settings.masterVolume * settings.musicVolume * std::clamp(gain, 0.0f, 1.0f), 0.0f, 1.0f);
}

float computeSfxGain(const AppSettings& settings, float gain) {
    return std::clamp(settings.masterVolume * settings.sfxVolume * std::clamp(gain, 0.0f, 1.0f), 0.0f, 1.0f);
}

std::shared_ptr<void> wrapAudio(MIX_Audio* audio) {
    return std::shared_ptr<void>(audio, [](void* handle) { MIX_DestroyAudio(static_cast<MIX_Audio*>(handle)); });
}

std::shared_ptr<void> wrapTrack(MIX_Track* track) {
    return std::shared_ptr<void>(track, [](void* handle) { MIX_DestroyTrack(static_cast<MIX_Track*>(handle)); });
}

// Starts (or restarts) a track, optionally set to loop indefinitely.
bool playTrack(MIX_Track* track, bool loop) {
    if (!loop) {
        return MIX_PlayTrack(track, 0);
    }

    SDL_PropertiesID options = SDL_CreateProperties();
    if (options == 0) {
        spdlog::warn("AudioEngine: SDL_CreateProperties failed: {}", SDL_GetError());
        return MIX_PlayTrack(track, 0);
    }
    SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    const bool started = MIX_PlayTrack(track, options);
    SDL_DestroyProperties(options);
    return started;
}

} // namespace

AudioEngine::AudioEngine() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        spdlog::warn("AudioEngine: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}", SDL_GetError());
        return;
    }

    if (!MIX_Init()) {
        spdlog::warn("AudioEngine: MIX_Init failed: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    MIX_Mixer* mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!mixer) {
        spdlog::warn("AudioEngine: MIX_CreateMixerDevice failed: {}", SDL_GetError());
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    mixer_ = mixer;
    audioReady_ = true;
}

AudioEngine::~AudioEngine() {
    if (!audioReady_) {
        return;
    }

    // Drop playback handles (stopping/freeing the underlying MIX_Track and
    // MIX_Audio via their shared_ptr deleters) before destroying the mixer.
    musicPlaybacks_.clear();
    sfxPlaybacks_.clear();
    sfxBufferCache_.clear();

    MIX_DestroyMixer(static_cast<MIX_Mixer*>(mixer_));
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioEngine::setEffectiveSettings(const AppSettings& effectiveSettings) {
    effectiveSettings_ = effectiveSettings;
}

std::shared_ptr<void> AudioEngine::getOrLoadSfxAudio(const std::string& path) {
    if (auto it = sfxBufferCache_.find(path); it != sfxBufferCache_.end()) {
        return it->second;
    }

    MIX_Audio* loadedAudio = MIX_LoadAudio(static_cast<MIX_Mixer*>(mixer_), path.c_str(), /*predecode=*/true);
    if (!loadedAudio) {
        spdlog::warn("AudioEngine: failed to load sfx file: {} ({})", path, SDL_GetError());
        return nullptr;
    }

    std::shared_ptr<void> audio = wrapAudio(loadedAudio);
    sfxBufferCache_.emplace(path, audio);
    return audio;
}

void AudioEngine::preload(const std::string& path, AudioChannel channel) {
    if (path.empty()) {
        spdlog::warn("AudioEngine: rejected empty preload request path.");
        return;
    }
    if (!audioReady_) {
        return;
    }

    if (channel == AudioChannel::Sfx) {
        getOrLoadSfxAudio(path);
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
    if (!audioReady_) {
        return;
    }

    MIX_Mixer* mixer = static_cast<MIX_Mixer*>(mixer_);

    if (channel == AudioChannel::Music) {
        // Music is single-instance: stop/destroy the currently playing
        // track (via shared_ptr deleters) before starting the new one.
        musicPlaybacks_.clear();

        MIX_Audio* audio = MIX_LoadAudio(mixer, path.c_str(), /*predecode=*/false);
        if (!audio) {
            spdlog::warn("AudioEngine: failed to open music file: {} ({})", path, SDL_GetError());
            return;
        }

        MIX_Track* track = MIX_CreateTrack(mixer);
        if (!track) {
            spdlog::warn("AudioEngine: MIX_CreateTrack failed for music: {} ({})", path, SDL_GetError());
            MIX_DestroyAudio(audio);
            return;
        }

        MIX_SetTrackAudio(track, audio);
        MIX_SetTrackGain(track, computeMusicGain(effectiveSettings_, gain));
        playTrack(track, loop);

        MusicPlayback playback;
        playback.path = path;
        playback.gain = gain;
        playback.audio = wrapAudio(audio);
        playback.track = wrapTrack(track);
        musicPlaybacks_.push_back(std::move(playback));
        refreshActiveAssetKeys();
        return;
    }

    std::shared_ptr<void> audio = getOrLoadSfxAudio(path);
    if (!audio) {
        return;
    }

    MIX_Track* track = MIX_CreateTrack(mixer);
    if (!track) {
        spdlog::warn("AudioEngine: MIX_CreateTrack failed for sfx: {} ({})", path, SDL_GetError());
        return;
    }

    MIX_SetTrackAudio(track, static_cast<MIX_Audio*>(audio.get()));
    MIX_SetTrackGain(track, computeSfxGain(effectiveSettings_, gain));
    playTrack(track, loop);

    SfxPlayback playback;
    playback.path = path;
    playback.gain = gain;
    playback.audio = audio;
    playback.track = wrapTrack(track);
    sfxPlaybacks_.push_back(std::move(playback));
    refreshActiveAssetKeys();
}

void AudioEngine::update(float dt) {
    if (!audioReady_) {
        return;
    }

    for (auto& playback : musicPlaybacks_) {
        if (playback.track) {
            playback.ageSeconds += dt;
            MIX_SetTrackGain(static_cast<MIX_Track*>(playback.track.get()), computeMusicGain(effectiveSettings_, playback.gain));
        }
    }

    musicPlaybacks_.erase(std::remove_if(musicPlaybacks_.begin(), musicPlaybacks_.end(),
                                         [](const MusicPlayback& playback) {
                                             return !playback.track ||
                                                    (playback.ageSeconds >= kAudioStartGraceSeconds &&
                                                     !MIX_TrackPlaying(static_cast<MIX_Track*>(playback.track.get())));
                                         }),
                          musicPlaybacks_.end());

    for (auto& playback : sfxPlaybacks_) {
        if (playback.track) {
            playback.ageSeconds += dt;
            MIX_SetTrackGain(static_cast<MIX_Track*>(playback.track.get()), computeSfxGain(effectiveSettings_, playback.gain));
        }
    }

    sfxPlaybacks_.erase(std::remove_if(sfxPlaybacks_.begin(), sfxPlaybacks_.end(),
                                       [](const SfxPlayback& playback) {
                                           return !playback.track ||
                                                  (playback.ageSeconds >= kAudioStartGraceSeconds &&
                                                   !MIX_TrackPlaying(static_cast<MIX_Track*>(playback.track.get())));
                                       }),
                        sfxPlaybacks_.end());

    refreshActiveAssetKeys();
}

void AudioEngine::refreshActiveAssetKeys() {
    activeAssetKeys_.clear();
    for (const auto& playback : musicPlaybacks_) {
        if (playback.track) {
            activeAssetKeys_.insert(makeAudioAssetKey(playback.path, AudioChannel::Music));
        }
    }
    for (const auto& playback : sfxPlaybacks_) {
        if (playback.track) {
            activeAssetKeys_.insert(makeAudioAssetKey(playback.path, AudioChannel::Sfx));
        }
    }
}
