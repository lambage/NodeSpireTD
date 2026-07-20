#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

class PlayLevelRouteController {
  public:
    void clear();

    bool updateFromRoutePoints(const std::vector<glm::vec3>& rendererRoute);

    bool hasValidRoute() const;
    std::size_t pointCount() const;
    float totalLength() const;

    glm::vec3 samplePosition(float distanceAlongPath) const;
    float sampleYaw(float distanceAlongPath) const;

  private:
    std::vector<glm::vec3> routePoints_;
    std::vector<float> routeSegmentLengths_;
    float routeTotalLength_ = 0.0f;
};
