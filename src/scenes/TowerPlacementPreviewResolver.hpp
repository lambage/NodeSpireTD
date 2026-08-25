#pragma once

#include "scenes/TowerPlacementRules.hpp"

#include <functional>

struct TowerArchetype;

class TowerPlacementPreviewResolver {
  public:
    struct ResolvedPlacement {
        bool hasHit = false;
        bool canPlace = false;
        glm::vec3 worldPos{0.0f};
        std::string reason;
    };

    using SampleTerrainFn = std::function<PlacementTerrainSample()>;
    using ValidateFn = std::function<std::string(const glm::vec3&, int, const PlacementTerrainSample&)>;

    void reset();
    ResolvedPlacement resolve(const TowerArchetype* selected, const SampleTerrainFn& sampleTerrain,
                              const ValidateFn& validatePlacement);
    bool canPlaceAt(const TowerArchetype* selected, const glm::vec3& worldPos, const ValidateFn& validatePlacement,
                    std::string& outReason);
    void cacheValidationResult(const std::string& towerId, const glm::vec3& worldPos, bool canPlace,
                               std::string reason);

    const PlacementTerrainSample& lastTerrainSample() const { return lastTerrainSample_; }
    bool bungeeInvalidActive() const { return bungeeInvalidActive_; }
    bool hasValidPlacementAnchor() const { return hasValidPlacementAnchor_; }
    const glm::vec3& bungeeAnchorPos() const { return bungeeAnchorPos_; }
    const glm::vec3& bungeeResolvedPos() const { return bungeeResolvedPos_; }
    bool hasLastRawPlacementCandidate() const { return hasLastRawPlacementCandidate_; }
    const glm::vec3& lastRawPlacementCandidatePos() const { return lastRawPlacementCandidatePos_; }
    bool lastRawPlacementCandidateValid() const { return lastRawPlacementCandidateValid_; }

  private:
    PlacementTerrainSample lastTerrainSample_{};
    bool hasLastPlacementValidation_ = false;
    std::string lastPlacementValidationTowerId_;
    glm::vec3 lastPlacementValidationPos_{0.0f};
    bool lastPlacementCanPlace_ = false;
    std::string lastPlacementValidationCachedReason_;
    glm::vec3 lastValidPlacementPos_{0.0f};
    bool hasValidPlacementAnchor_ = false;
    bool bungeeInvalidActive_ = false;
    glm::vec3 bungeeAnchorPos_{0.0f};
    glm::vec3 bungeeResolvedPos_{0.0f};
    bool hasLastRawPlacementCandidate_ = false;
    glm::vec3 lastRawPlacementCandidatePos_{0.0f};
    bool lastRawPlacementCandidateValid_ = false;
};
