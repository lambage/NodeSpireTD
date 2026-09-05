#pragma once

#include "AppSettings.hpp"
#include "SettingsManager.hpp"
#include "rmlui/IScene.hpp"
#include "rmlui/playlevel/PlayLevelUiContract.hpp"

#include <RmlUi/Core/EventListener.h>
#include <glm/vec3.hpp>
#include <memory>

class VulkanContext;
class WorldRenderer;

namespace multiplayer {
class MultiplayerSession;
}

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

class PlayLevelScene final : public IScene, public Rml::EventListener {
  public:
    PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                   const PlayLevelLaunchConfig& launchConfig);
    ~PlayLevelScene() override;

    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;
    void renderWorld(VkCommandBuffer commandBuffer, VkExtent2D extent) override;
    SceneTransition onKeyDown(Rml::Input::KeyIdentifier key) override;
    void ProcessEvent(Rml::Event& event) override;

  private:
    void beginWorldLoad();
    void updateCamera(float dt);
    void refreshHud();
    void setPauseMenuVisible(bool visible);
    void populateAudioControls();
    void setAudioValueLabel(const char* id, float value);

    VulkanContext& vulkanContext_;
    multiplayer::MultiplayerSession& session_;
    PlayLevelLaunchConfig launchConfig_;
    Rml::ElementDocument* document_ = nullptr;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    PlayLevelUiSnapshot snapshot_;
    SceneTransition pendingTransition_;
    glm::vec3 cameraPosition_{0.0f, 5.0f, 20.0f};
    float cameraYaw_ = 3.14159f;
    float cameraPitch_ = -0.25f;
    bool mouseLookActive_ = false;
    bool pauseMenuVisible_ = false;
    bool onlineMatch_ = false;
    bool loadedReadySignaled_ = false;
    AudioEngine* audio_ = nullptr;
    SettingsManager settingsManager_;
    AppSettings settings_;
};

} // namespace NodeSpireUi
