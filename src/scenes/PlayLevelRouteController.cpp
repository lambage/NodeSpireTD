#include "scenes/PlayLevelRouteController.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

void PlayLevelRouteController::clear() {
    routePoints_.clear();
    routeSegmentLengths_.clear();
    routeTotalLength_ = 0.0f;
}

bool PlayLevelRouteController::updateFromRoutePoints(const std::vector<glm::vec3>& rendererRoute) {
    if (rendererRoute.size() < 2) {
        return false;
    }

    if (rendererRoute == routePoints_ && routeTotalLength_ > 0.0f) {
        return true;
    }

    routePoints_ = rendererRoute;
    routeSegmentLengths_.clear();
    routeTotalLength_ = 0.0f;
    routeSegmentLengths_.reserve(routePoints_.size() - 1);

    for (std::size_t i = 0; i + 1 < routePoints_.size(); ++i) {
        const float segmentLen = glm::distance(routePoints_[i], routePoints_[i + 1]);
        routeSegmentLengths_.push_back(segmentLen);
        routeTotalLength_ += segmentLen;
    }

    return routeTotalLength_ > 0.0f;
}

bool PlayLevelRouteController::hasValidRoute() const {
    return routePoints_.size() >= 2 && routeTotalLength_ > 0.0f;
}

std::size_t PlayLevelRouteController::pointCount() const {
    return routePoints_.size();
}

float PlayLevelRouteController::totalLength() const {
    return routeTotalLength_;
}

glm::vec3 PlayLevelRouteController::samplePosition(float distanceAlongPath) const {
    if (routePoints_.empty()) {
        return glm::vec3(0.0f);
    }
    if (routePoints_.size() == 1 || routeTotalLength_ <= 0.0f) {
        return routePoints_.front();
    }

    float remaining = std::clamp(distanceAlongPath, 0.0f, routeTotalLength_);
    for (std::size_t i = 0; i + 1 < routePoints_.size(); ++i) {
        const float segmentLen = routeSegmentLengths_[i];
        if (remaining <= segmentLen || i + 2 == routePoints_.size()) {
            const float t = segmentLen > 1e-5f ? (remaining / segmentLen) : 0.0f;
            return glm::mix(routePoints_[i], routePoints_[i + 1], t);
        }
        remaining -= segmentLen;
    }

    return routePoints_.back();
}

float PlayLevelRouteController::sampleYaw(float distanceAlongPath) const {
    const float probeAhead = 0.2f;
    const glm::vec3 a = samplePosition(distanceAlongPath);
    const glm::vec3 b = samplePosition(std::min(routeTotalLength_, distanceAlongPath + probeAhead));
    glm::vec3 dir = b - a;
    dir.y = 0.0f;
    if (glm::dot(dir, dir) < 1e-6f) {
        return 0.0f;
    }
    dir = glm::normalize(dir);
    return std::atan2(dir.x, dir.z);
}
