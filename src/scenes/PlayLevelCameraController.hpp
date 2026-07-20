#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class PlayLevelCameraController {
  public:
    void reset();
    void update(float dt);

    glm::mat4 buildViewMatrix() const;
    bool raycastGroundAtCursor(glm::vec3& outHit) const;

    const glm::vec3& position() const;
    float yaw() const;
    float pitch() const;

  private:
    glm::vec3 camPos_{0.0f, 5.0f, 20.0f};
    float camYaw_ = 3.14159f;
    float camPitch_ = -0.25f;
};
