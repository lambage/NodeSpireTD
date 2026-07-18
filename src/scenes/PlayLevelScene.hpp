#pragma once
#include "scenes/GameScene.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <memory>
#include <string>

class WorldRenderer;

class PlayLevelScene final : public GameScene {
  public:
    PlayLevelScene();
    ~PlayLevelScene() override;

    void onEnter(SceneSharedState&) override;
    void onExit(SceneSharedState&) override;

    SceneFrameResult render(SceneSharedState& state, float dt) override;
    void renderWorld(VkCommandBuffer cmd, VkExtent2D extent) override;

  private:
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::string loadStatus_;

    // Flying camera state
    glm::vec3 camPos_{0.0f, 5.0f, 20.0f};
    float camYaw_   = 3.14159f;
    float camPitch_ = -0.25f;

    glm::mat4 buildViewMatrix() const;
    void updateCamera(float dt);
};
