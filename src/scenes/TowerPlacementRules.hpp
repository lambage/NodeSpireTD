#pragma once

#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/PlayLevelState.hpp"
#include "scenes/TowerLoadController.hpp"
#include "utility/WorldAssetLoader.hpp"

#include <array>

class WorldRenderer;

struct PlacementTerrainSample {
    bool hit = false;
    glm::vec3 worldPosition{0.0f};
    glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
    float slope = 0.0f;
    TowerPlacementRegionType towerPlacementType = TowerPlacementRegionType::Ground;
    bool onPath = false;
};

class TowerPlacementRules {
  public:
    struct Context {
        const PlayLevelState& gameplayState;
        const std::vector<playlevel::PlacedTower>& placedTowers;
        const WorldRenderer* worldRenderer = nullptr;
        const std::vector<TowerPlacementRegion>& placementRegions;
        float maxTowerPlacementSlopeDegrees = 30.0f;
        float pathCorridorHalfWidth = 2.0f;
    };

    static void buildRouteSegmentCorridorBoxCorners(const glm::vec3& a, const glm::vec3& b, float halfWidth,
                                                    std::array<glm::vec3, 8>& outCorners);
    static bool isPointInPlacementRegion(const Context& context, const glm::vec3& worldPos,
                                         const TowerPlacementRegion*& outRegion);
    static bool isPointOnPath(const Context& context, const glm::vec3& worldPos);
    static PlacementTerrainSample sampleTerrainAtCursor(const Context& context, const glm::mat4& viewMatrix,
                                                        const glm::vec3& cameraPosition);
    static std::string validatePlacement(const Context& context, const TowerArchetype& archetype,
                                         const glm::vec3& worldPos, int footprintSampleCount,
                                         const PlacementTerrainSample& terrainSample);

  private:
    static bool isFootprintClearForPlacement(const Context& context, const glm::vec3& worldPos,
                                             const TowerArchetype& archetype, int footprintSampleCount);
};
