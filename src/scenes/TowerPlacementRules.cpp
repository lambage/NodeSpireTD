#include "scenes/TowerPlacementRules.hpp"

#include "utility/WorldRenderer.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace {

constexpr float kPathCorridorVerticalMargin = 1.25f;
constexpr float kPathCorridorEndExtension = 1.25f;

bool pointInRouteSegmentCorridor(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, float halfWidth) {
    const glm::vec2 a2(a.x, a.z);
    const glm::vec2 b2(b.x, b.z);
    const glm::vec2 p2(point.x, point.z);
    const glm::vec2 ab = b2 - a2;
    const float len = glm::length(ab);
    const float minY = std::min(a.y, b.y) - kPathCorridorVerticalMargin;
    const float maxY = std::max(a.y, b.y) + kPathCorridorVerticalMargin;
    if (point.y < minY || point.y > maxY) {
        return false;
    }

    if (len < 1e-4f) {
        return glm::length(p2 - a2) <= halfWidth;
    }

    const glm::vec2 dir = ab / len;
    const glm::vec2 rel = p2 - a2;
    const float along = rel.x * dir.x + rel.y * dir.y;
    if (along < -kPathCorridorEndExtension || along > len + kPathCorridorEndExtension) {
        return false;
    }
    const float across = rel.x * (-dir.y) + rel.y * dir.x;
    return std::abs(across) <= halfWidth;
}

} // namespace

void TowerPlacementRules::buildRouteSegmentCorridorBoxCorners(const glm::vec3& a, const glm::vec3& b, float halfWidth,
                                                              std::array<glm::vec3, 8>& outCorners) {
    glm::vec3 forward = b - a;
    forward.y = 0.0f;
    const float len = glm::length(forward);
    forward = (len > 1e-4f) ? (forward / len) : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right(forward.z, 0.0f, -forward.x);

    const glm::vec3 extendedA = a - forward * kPathCorridorEndExtension;
    const glm::vec3 extendedB = b + forward * kPathCorridorEndExtension;
    const float minY = std::min(a.y, b.y) - kPathCorridorVerticalMargin;
    const float maxY = std::max(a.y, b.y) + kPathCorridorVerticalMargin;

    const glm::vec3 base[4] = {
        extendedA - right * halfWidth,
        extendedA + right * halfWidth,
        extendedB + right * halfWidth,
        extendedB - right * halfWidth,
    };
    for (int i = 0; i < 4; ++i) {
        outCorners[i] = glm::vec3(base[i].x, minY, base[i].z);
        outCorners[static_cast<std::size_t>(i) + 4] = glm::vec3(base[i].x, maxY, base[i].z);
    }
}

bool TowerPlacementRules::isPointInPlacementRegion(const Context& context, const glm::vec3& worldPos,
                                                   const TowerPlacementRegion*& outRegion) {
    outRegion = nullptr;
    for (const TowerPlacementRegion& region : context.placementRegions) {
        if (worldPos.x >= region.boundsMin.x && worldPos.x <= region.boundsMax.x &&
            worldPos.y >= region.boundsMin.y && worldPos.y <= region.boundsMax.y &&
            worldPos.z >= region.boundsMin.z && worldPos.z <= region.boundsMax.z) {
            outRegion = &region;
            return true;
        }
    }
    return false;
}

bool TowerPlacementRules::isPointOnPath(const Context& context, const glm::vec3& worldPos) {
    if (!context.worldRenderer) {
        return false;
    }

    const std::vector<glm::vec3>& route = context.worldRenderer->routePoints();
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        if (pointInRouteSegmentCorridor(worldPos, route[i], route[i + 1], context.pathCorridorHalfWidth)) {
            return true;
        }
    }

    return false;
}

PlacementTerrainSample TowerPlacementRules::sampleTerrainAtCursor(const Context& context, const glm::mat4& viewMatrix,
                                                                  const glm::vec3& cameraPosition) {
    PlacementTerrainSample sample;
    if (!context.worldRenderer || !context.worldRenderer->isLoaded()) {
        return sample;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return sample;
    }

    const ImVec2 mousePos = io.MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return sample;
    }

    const float ndcX = (2.0f * mousePos.x) / displaySize.x - 1.0f;
    const float ndcY = (2.0f * mousePos.y) / displaySize.y - 1.0f;

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    constexpr float kFovYRadians = glm::radians(60.0f);
    glm::mat4 proj = glm::perspective(kFovYRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;

    const glm::mat4 invVP = glm::inverse(proj * viewMatrix);
    const glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec4 nearWorld = invVP * nearClip;
    glm::vec4 farWorld = invVP * farClip;
    if (std::abs(nearWorld.w) < 1e-6f || std::abs(farWorld.w) < 1e-6f) {
        return sample;
    }
    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;

    glm::vec3 rayDir = glm::vec3(farWorld - nearWorld);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return sample;
    }
    rayDir = glm::normalize(rayDir);

    WorldPickHit pickHit;
    if (!context.worldRenderer->raycastStaticGeometry(cameraPosition, rayDir, pickHit)) {
        return sample;
    }

    sample.hit = true;
    sample.worldPosition = pickHit.worldPosition;
    sample.surfaceNormal = pickHit.worldNormal;

    const float dotProduct = glm::dot(sample.surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    const float slopeClamped = glm::clamp(dotProduct, -1.0f, 1.0f);
    sample.slope = glm::degrees(std::acos(slopeClamped));
    sample.onPath = isPointOnPath(context, sample.worldPosition);

    sample.towerPlacementType = TowerPlacementRegionType::Ground;
    const TowerPlacementRegion* region = nullptr;
    if (isPointInPlacementRegion(context, sample.worldPosition, region) && region) {
        sample.towerPlacementType = region->type;
    }

    return sample;
}

std::string TowerPlacementRules::validatePlacement(const Context& context, const TowerArchetype& archetype,
                                                   const glm::vec3& worldPos, int footprintSampleCount,
                                                   const PlacementTerrainSample& terrainSample,
                                                   float availableFunds) {
    if (context.gameplayState.matchStatus != MatchStatus::Running) {
        return "match is not running";
    }
    if (!context.worldRenderer || !context.worldRenderer->isLoaded()) {
        return "world is still loading";
    }
    if (availableFunds < static_cast<float>(archetype.cost)) {
        return "insufficient funds";
    }

    constexpr float kMinTowerSpacing = 1.7f;
    for (const playlevel::PlacedTower& tower : context.placedTowers) {
        const glm::vec3 delta = worldPos - tower.position;
        const float dist2 = glm::dot(delta, delta);
        if (dist2 < (kMinTowerSpacing * kMinTowerSpacing)) {
            return "too close to another tower";
        }
    }

    if (isPointOnPath(context, worldPos)) {
        return "cannot place towers on the path";
    }

    constexpr float kSlopeMatchEpsilon = 0.01f;
    if (glm::distance(terrainSample.worldPosition, worldPos) <= kSlopeMatchEpsilon &&
        terrainSample.slope > context.maxTowerPlacementSlopeDegrees) {
        return "surface is too steep to place a tower";
    }

    if (!isFootprintClearForPlacement(context, worldPos, archetype, footprintSampleCount)) {
        return "tower base does not fit on this surface";
    }

    return {};
}

bool TowerPlacementRules::isFootprintClearForPlacement(const Context& context, const glm::vec3& worldPos,
                                                       const TowerArchetype& archetype, int footprintSampleCount) {
    if (!context.worldRenderer) {
        return true;
    }

    constexpr float kTowerFootprintBaseRadius = 0.6f;
    constexpr float kFootprintProbeHeight = 50.0f;
    constexpr float kFootprintMaxHeightDelta = 0.6f;
    constexpr float kTwoPi = 6.2831853071795864769f;
    const int sampleCount = std::max(1, footprintSampleCount);
    const float minUpDot = std::cos(glm::radians(context.maxTowerPlacementSlopeDegrees));
    const float footprintRadius = kTowerFootprintBaseRadius * std::max(0.01f, archetype.renderScale);

    for (int i = 0; i < sampleCount; ++i) {
        const float angle = (kTwoPi * static_cast<float>(i)) / static_cast<float>(sampleCount);
        const glm::vec3 offset(std::cos(angle) * footprintRadius, 0.0f, std::sin(angle) * footprintRadius);
        const glm::vec3 samplePos = worldPos + offset;
        const glm::vec3 probeOrigin = samplePos + glm::vec3(0.0f, kFootprintProbeHeight, 0.0f);

        WorldPickHit hit;
        if (!context.worldRenderer->raycastStaticGeometry(probeOrigin, glm::vec3(0.0f, -1.0f, 0.0f), hit)) {
            return false;
        }

        const float dotProduct = glm::dot(hit.worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (dotProduct < minUpDot) {
            return false;
        }

        if (std::abs(hit.worldPosition.y - worldPos.y) > kFootprintMaxHeightDelta) {
            return false;
        }
    }

    return true;
}
