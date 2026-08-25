#include "scenes/TowerPlacementPreviewResolver.hpp"

#include "scenes/TowerLoadController.hpp"

#include <limits>

void TowerPlacementPreviewResolver::reset() {
    lastTerrainSample_ = {};
    hasLastPlacementValidation_ = false;
    lastPlacementValidationTowerId_.clear();
    lastPlacementValidationPos_ = glm::vec3(0.0f);
    lastPlacementCanPlace_ = false;
    lastPlacementValidationCachedReason_.clear();
    lastValidPlacementPos_ = glm::vec3(0.0f);
    hasValidPlacementAnchor_ = false;
    bungeeInvalidActive_ = false;
    bungeeAnchorPos_ = glm::vec3(0.0f);
    bungeeResolvedPos_ = glm::vec3(0.0f);
    hasLastRawPlacementCandidate_ = false;
    lastRawPlacementCandidatePos_ = glm::vec3(0.0f);
    lastRawPlacementCandidateValid_ = false;
}

void TowerPlacementPreviewResolver::cacheValidationResult(const std::string& towerId, const glm::vec3& worldPos,
                                                          bool canPlace, std::string reason) {
    hasLastPlacementValidation_ = true;
    lastPlacementValidationTowerId_ = towerId;
    lastPlacementValidationPos_ = worldPos;
    lastPlacementCanPlace_ = canPlace;
    lastPlacementValidationCachedReason_ = std::move(reason);
}

TowerPlacementPreviewResolver::ResolvedPlacement TowerPlacementPreviewResolver::resolve(
    const TowerArchetype* selected, const SampleTerrainFn& sampleTerrain, const ValidateFn& validatePlacement) {
    ResolvedPlacement result;
    if (!selected) {
        reset();
        result.reason = "no tower selected";
        return result;
    }

    lastTerrainSample_ = sampleTerrain ? sampleTerrain() : PlacementTerrainSample{};
    if (!lastTerrainSample_.hit) {
        hasLastRawPlacementCandidate_ = false;
        result.reason = "cursor is not over ground";
        return result;
    }

    result.hasHit = true;
    const glm::vec3 candidatePos = lastTerrainSample_.worldPosition;
    constexpr int kPreviewFootprintSampleCount = 4;
    const std::string candidateReason = validatePlacement
        ? validatePlacement(candidatePos, kPreviewFootprintSampleCount, lastTerrainSample_)
        : std::string{};
    const bool candidateValid = candidateReason.empty();
    hasLastRawPlacementCandidate_ = true;
    lastRawPlacementCandidatePos_ = candidatePos;
    lastRawPlacementCandidateValid_ = candidateValid;

    constexpr float kSnapReleaseDistance = 1.5f;
    bool resolvedValid = candidateValid;
    std::string resolvedReason = candidateReason;

    if (candidateValid) {
        lastValidPlacementPos_ = candidatePos;
        hasValidPlacementAnchor_ = true;
        bungeeInvalidActive_ = false;
        bungeeAnchorPos_ = candidatePos;
        bungeeResolvedPos_ = candidatePos;
        result.worldPos = candidatePos;
    } else if (hasValidPlacementAnchor_) {
        if (!bungeeInvalidActive_) {
            bungeeInvalidActive_ = true;
            bungeeAnchorPos_ = lastValidPlacementPos_;
            bungeeResolvedPos_ = lastValidPlacementPos_;
        }

        if (glm::distance(candidatePos, bungeeAnchorPos_) >= kSnapReleaseDistance) {
            hasValidPlacementAnchor_ = false;
            bungeeInvalidActive_ = false;
            result.worldPos = candidatePos;
        } else {
            const glm::vec3 toCursor = candidatePos - bungeeResolvedPos_;
            glm::vec3 dirXZ(toCursor.x, 0.0f, toCursor.z);
            const float dirLen = glm::length(dirXZ);
            if (dirLen > 1e-4f) {
                dirXZ /= dirLen;
            } else {
                dirXZ = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            const glm::vec3 tangent(-dirXZ.z, 0.0f, dirXZ.x);

            bool foundSlide = false;
            glm::vec3 bestSlide = lastValidPlacementPos_;
            float bestDistSq = std::numeric_limits<float>::max();
            constexpr float kSlideStep = 0.20f;
            constexpr int kSlideSteps = 6;
            for (int step = 1; step <= kSlideSteps; ++step) {
                const float d = kSlideStep * static_cast<float>(step);
                const glm::vec3 offsets[2] = {tangent * d, tangent * -d};
                for (const glm::vec3& off : offsets) {
                    const glm::vec3 probe = candidatePos + off;
                    const std::string probeReason = validatePlacement
                        ? validatePlacement(probe, kPreviewFootprintSampleCount, lastTerrainSample_)
                        : std::string{};
                    if (!probeReason.empty()) {
                        continue;
                    }

                    const float distSq = glm::dot(probe - candidatePos, probe - candidatePos);
                    if (distSq < bestDistSq) {
                        bestDistSq = distSq;
                        bestSlide = probe;
                        foundSlide = true;
                    }
                }
                if (foundSlide) {
                    break;
                }
            }

            if (foundSlide) {
                const float prevDistSq =
                    glm::dot(bungeeResolvedPos_ - candidatePos, bungeeResolvedPos_ - candidatePos);
                const float newDistSq = glm::dot(bestSlide - candidatePos, bestSlide - candidatePos);
                constexpr float kSlideSwitchHysteresis = 0.08f;
                constexpr float kSlideSwitchHysteresisSq = kSlideSwitchHysteresis * kSlideSwitchHysteresis;
                const glm::vec3 chosenSlide =
                    ((newDistSq + kSlideSwitchHysteresisSq) < prevDistSq) ? bestSlide : bungeeResolvedPos_;

                result.worldPos = chosenSlide;
                lastValidPlacementPos_ = chosenSlide;
                bungeeResolvedPos_ = chosenSlide;
                hasValidPlacementAnchor_ = true;
                resolvedValid = true;
                resolvedReason.clear();
            } else {
                glm::vec3 lowInvalid = candidatePos;
                glm::vec3 highValid = bungeeResolvedPos_;
                for (int i = 0; i < 7; ++i) {
                    const glm::vec3 mid = (lowInvalid + highValid) * 0.5f;
                    const std::string midReason = validatePlacement
                        ? validatePlacement(mid, kPreviewFootprintSampleCount, lastTerrainSample_)
                        : std::string{};
                    if (midReason.empty()) {
                        highValid = mid;
                    } else {
                        lowInvalid = mid;
                    }
                }

                const float prevDistSq =
                    glm::dot(bungeeResolvedPos_ - candidatePos, bungeeResolvedPos_ - candidatePos);
                const float newDistSq = glm::dot(highValid - candidatePos, highValid - candidatePos);
                constexpr float kFallbackSwitchHysteresis = 0.05f;
                constexpr float kFallbackSwitchHysteresisSq =
                    kFallbackSwitchHysteresis * kFallbackSwitchHysteresis;
                const glm::vec3 chosenSlide =
                    ((newDistSq + kFallbackSwitchHysteresisSq) < prevDistSq) ? highValid : bungeeResolvedPos_;

                result.worldPos = chosenSlide;
                lastValidPlacementPos_ = chosenSlide;
                bungeeResolvedPos_ = chosenSlide;
                hasValidPlacementAnchor_ = true;
                resolvedValid = true;
                resolvedReason.clear();
            }
        }
    } else {
        hasValidPlacementAnchor_ = false;
        bungeeInvalidActive_ = false;
        result.worldPos = candidatePos;
    }

    result.canPlace = resolvedValid;
    result.reason = resolvedReason;
    cacheValidationResult(selected->id, result.worldPos, resolvedValid, resolvedReason);
    return result;
}

bool TowerPlacementPreviewResolver::canPlaceAt(const TowerArchetype* selected, const glm::vec3& worldPos,
                                               const ValidateFn& validatePlacement, std::string& outReason) {
    if (!selected) {
        outReason = "no tower selected";
        return false;
    }

    constexpr float kValidationReuseDistance = 0.06f;
    constexpr float kValidationReuseDistanceSq = kValidationReuseDistance * kValidationReuseDistance;
    if (hasLastPlacementValidation_ && lastPlacementValidationTowerId_ == selected->id &&
        glm::dot(worldPos - lastPlacementValidationPos_, worldPos - lastPlacementValidationPos_) <=
            kValidationReuseDistanceSq) {
        outReason = lastPlacementValidationCachedReason_;
        return lastPlacementCanPlace_;
    }

    constexpr int kPreviewFootprintSampleCount = 4;
    outReason = validatePlacement ? validatePlacement(worldPos, kPreviewFootprintSampleCount, lastTerrainSample_)
                                  : std::string{};
    const bool canPlace = outReason.empty();
    cacheValidationResult(selected->id, worldPos, canPlace, outReason);
    return canPlace;
}
