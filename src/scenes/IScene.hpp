#pragma once

#include "ImGuiLayer.hpp"
#include "lua.hpp"
#include "scenes/SceneSharedState.hpp"

#include <SFML/Window/Event.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>
#include <volk.h>


enum class SceneId {
    Splash,
    MainMenu,
    Options,
    Lobby,
    PlayLevel
};

struct SceneTransitionRequest {
    SceneId target = SceneId::MainMenu;
    std::string message{};
    float minDurationSeconds = 0.0f;
};

enum class AudioChannel {
    Music,
    Sfx
};

struct AudioPlayRequest {
    std::string path{};
    AudioChannel channel = AudioChannel::Music;
    bool loop = false;
    float gain = 1.0f;
};

struct AudioPreloadRequest {
    std::string path{};
    AudioChannel channel = AudioChannel::Sfx;
};

struct AudioReleaseRequest {
    std::string path{};
    AudioChannel channel = AudioChannel::Sfx;
};

struct SceneRequestState {
    bool quitRequested = false;
    bool applySettingsRequested = false;
    bool acceptDisplayChangesRequested = false;
    bool revertDisplayChangesRequested = false;

    bool sceneTransitionRequested = false;
    SceneTransitionRequest sceneTransition{};

    std::vector<AudioPreloadRequest> audioPreloadRequests{};
    std::vector<AudioReleaseRequest> audioReleaseRequests{};
    std::vector<AudioPlayRequest> audioPlayRequests{};
};

class IScene {
  public:
    IScene() : L_(luaL_newstate()) {
        if (L_) {
            luaL_openlibs(L_);
        }
    }

    virtual ~IScene() {
        if (L_) {
            lua_close(L_);
        }
    }

    virtual void onEnter(SceneSharedState&) = 0;
    virtual void onExit(SceneSharedState&) = 0;

    virtual void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) = 0;
    virtual void render(SceneSharedState& state, float dt) = 0;

    SceneRequestState consumeSceneRequests() {
        SceneRequestState consumed = std::move(sceneRequests_);
        sceneRequests_ = {};
        return consumed;
    }

    // Called each frame with the active command buffer (inside vkCmdBeginRendering)
    // before ImGui is rendered. Override in 3D game scenes.
    virtual void renderWorld(VkCommandBuffer /*cmd*/, VkExtent2D /*extent*/) {}

  protected:
    lua_State* L_ = nullptr;

    void requestQuit() {
        sceneRequests_.quitRequested = true;
    }

    void requestApplySettings() {
        sceneRequests_.applySettingsRequested = true;
    }

    void requestAcceptDisplayChanges() {
        sceneRequests_.acceptDisplayChangesRequested = true;
    }

    void requestRevertDisplayChanges() {
        sceneRequests_.revertDisplayChangesRequested = true;
    }

    void requestScene(SceneId target, std::string message = {}, float minDurationSeconds = 0.0f) {
        sceneRequests_.sceneTransitionRequested = true;
        sceneRequests_.sceneTransition.target = target;
        sceneRequests_.sceneTransition.message = std::move(message);
        sceneRequests_.sceneTransition.minDurationSeconds = minDurationSeconds;
    }

    void requestPlayAudio(std::string path,
                        AudioChannel channel = AudioChannel::Music,
                        bool loop = false,
                        float gain = 1.0f) {
        if (path.empty()) {
            return;
        }

        AudioPlayRequest request;
        request.path = std::move(path);
        request.channel = channel;
        request.loop = loop;
        request.gain = std::clamp(gain, 0.0f, 1.0f);
        sceneRequests_.audioPlayRequests.push_back(std::move(request));
    }

    void requestPreloadAudio(std::string path, AudioChannel channel = AudioChannel::Sfx) {
        if (path.empty()) {
            return;
        }

        AudioPreloadRequest request;
        request.path = std::move(path);
        request.channel = channel;
        sceneRequests_.audioPreloadRequests.push_back(std::move(request));
    }

    void requestReleaseAudio(std::string path, AudioChannel channel = AudioChannel::Sfx) {
        if (path.empty()) {
            return;
        }

        AudioReleaseRequest request;
        request.path = std::move(path);
        request.channel = channel;
        sceneRequests_.audioReleaseRequests.push_back(std::move(request));
    }

    int loadAudioHandle(std::string path, AudioChannel channel = AudioChannel::Sfx) {
        if (path.empty()) {
            return 0;
        }

        const std::string assetKey = makeAudioAssetKey(path, channel);
        if (const auto existing = audioAssetKeyToHandle_.find(assetKey); existing != audioAssetKeyToHandle_.end()) {
            auto handleIt = audioHandles_.find(existing->second);
            if (handleIt != audioHandles_.end()) {
                return handleIt->first;
            }
            audioAssetKeyToHandle_.erase(existing);
        }

        const int handle = nextAudioHandleId_++;
        LoadedAudioHandle loaded{};
        loaded.path = std::move(path);
        loaded.channel = channel;
        loaded.assetKey = assetKey;
        audioHandles_[handle] = std::move(loaded);
        audioAssetKeyToHandle_[assetKey] = handle;
        requestPreloadAudio(audioHandles_[handle].path, channel);
        return handle;
    }

    bool playAudioHandle(int handle, bool loop, float gain, std::string& outReason) {
        const auto it = audioHandles_.find(handle);
        if (it == audioHandles_.end()) {
            outReason = "invalid audio handle";
            return false;
        }

        requestPlayAudio(it->second.path, it->second.channel, loop, gain);
        outReason = "queued";
        return true;
    }

    bool releaseAudioHandle(int handle, std::string& outReason) {
        const auto it = audioHandles_.find(handle);
        if (it == audioHandles_.end()) {
            outReason = "invalid audio handle";
            return false;
        }

        requestReleaseAudio(it->second.path, it->second.channel);
        audioAssetKeyToHandle_.erase(it->second.assetKey);
        audioHandles_.erase(it);
        outReason = "queued";
        return true;
    }

    void releaseAllAudioHandles() {
        for (const auto& [handle, loaded] : audioHandles_) {
            (void)handle;
            requestReleaseAudio(loaded.path, loaded.channel);
        }
        audioHandles_.clear();
        audioAssetKeyToHandle_.clear();
    }

    bool isAudioHandlePlaying(int handle) const {
        const auto it = audioHandles_.find(handle);
        if (it == audioHandles_.end() || !activeAudioAssetKeys_) {
            return false;
        }
        return activeAudioAssetKeys_->contains(it->second.assetKey);
    }

    void setActiveAudioAssetKeys(const std::unordered_set<std::string>* activeAudioAssetKeys) {
        activeAudioAssetKeys_ = activeAudioAssetKeys;
    }

  private:
    static std::string makeAudioAssetKey(const std::string& path, AudioChannel channel) {
        return (channel == AudioChannel::Music ? "music:" : "sfx:") + path;
    }

    struct LoadedAudioHandle {
        std::string path;
        AudioChannel channel = AudioChannel::Sfx;
        std::string assetKey;
    };

    int nextAudioHandleId_ = 1;
    std::unordered_map<int, LoadedAudioHandle> audioHandles_;
    std::unordered_map<std::string, int> audioAssetKeyToHandle_;
    const std::unordered_set<std::string>* activeAudioAssetKeys_ = nullptr;
    SceneRequestState sceneRequests_{};
};
