#pragma once

#include "ImGuiLayer.hpp"
#include "lua.hpp"
#include "scenes/SceneSharedState.hpp"

#include <SFML/Window/Event.hpp>
#include <algorithm>
#include <string>
#include <utility>
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

struct SceneRequestState {
    bool quitRequested = false;
    bool applySettingsRequested = false;
    bool acceptDisplayChangesRequested = false;
    bool revertDisplayChangesRequested = false;

    bool sceneTransitionRequested = false;
    SceneTransitionRequest sceneTransition{};

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

  private:
    SceneRequestState sceneRequests_{};
};
