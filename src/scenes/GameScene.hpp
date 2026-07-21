#pragma once

#include "lua.hpp"
#include "scenes/IScene.hpp"

#include <SFML/Window/Event.hpp>
#include <vector>

class ImGuiLayer;

class GameScene : public IScene {
  public:
    GameScene() = default;

    ~GameScene() override;

    void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) override;

  protected:
    struct PendingAudioCallback {
        int handle = 0;
        int callbackRef = LUA_NOREF;
        bool observedPlaying = false;
        int settleFramesRemaining = 2;
    };

    float elapsedSeconds_ = 0.0f;
    int scriptRef_ = LUA_NOREF;
    std::vector<PendingAudioCallback> pendingAudioCallbacks_;

    int loadLuaScript(SceneSharedState& state, const std::string& scriptPath);

    void luaOnEnter(int scriptRef);
    void luaOnExit(SceneSharedState& state, int scriptRef);
    void luaOnRender(SceneSharedState& state, int scriptRef, float dt);
    void registerCoreGameplayApi();
    void processPendingAudioCallbacks();
    void clearPendingAudioCallbacks();
};

SceneId SceneIdFromString(const std::string& name);
