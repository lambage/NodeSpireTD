#pragma once

#include "lambui/IScene.hpp"
#include "lambui/SceneTypes.hpp"

#include <cstdint>
#include <memory>

namespace LambUI {
class UIManager;
}

class AudioEngine;
class VulkanContext;
struct AppSettings;
struct SDL_Window;

namespace lambui_backend {
class VulkanUiRenderer;
}

namespace multiplayer {
class MultiplayerSession;
class PlayerProfileStore;
}

namespace NodeSpireUi {

// Owns the currently active scene and switches between scenes on request.
// Only one scene (and its LambUI widget subtree) exists at a time; scenes
// are constructed lazily on entry and destroyed on exit.
class SceneManager {
  public:
    SceneManager(LambUI::UIManager& ui, SceneId initialScene, AudioEngine& audio,
          VulkanContext& vulkanContext,
           multiplayer::MultiplayerSession& multiplayerSession,
           multiplayer::PlayerProfileStore& playerProfileStore,
           lambui_backend::VulkanUiRenderer& renderer, AppSettings& settings, SDL_Window* window);

    void update(float dt);
    void renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent);
    void renderOverlay(VkCommandBuffer commandBuffer, VkExtent2D extent);
    bool handleKeyDown(uint32_t scanCode);
    void shutdown();

    // Tears down and rebuilds the active scene in place (same scene id), so
    // a Lua-scripted scene's UI gets rebuilt from its .lua script from
    // scratch. Driven by the app-level /reload dev hotkey.
    void reloadActiveScene();

    SceneId activeSceneId() const { return activeSceneId_; }

  private:
    void enterScene(SceneId id);
    void applyTransition(const SceneTransition& transition);

    LambUI::UIManager& ui_;
    AudioEngine& audio_;
    VulkanContext& vulkanContext_;
    lambui_backend::VulkanUiRenderer& renderer_;
    AppSettings& settings_;
    SDL_Window* window_;
    multiplayer::MultiplayerSession& multiplayerSession_;
    multiplayer::PlayerProfileStore& playerProfileStore_;
    PlayLevelLaunchConfig playLevelLaunchConfig_;
    SceneId activeSceneId_;
    std::unique_ptr<IScene> activeScene_;
};

} // namespace NodeSpireUi
