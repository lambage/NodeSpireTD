#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <cstddef>
#include <functional>

class PlayLevelTowerPlacementController {
  public:
    struct PlacementState {
      bool hasHit = false;
      bool canPlace = false;
      glm::vec3 worldPos{0.0f};
    };

    using RaycastGroundFn = std::function<bool(glm::vec3&)>;
    using CanPlaceFn = std::function<bool(const glm::vec3&)>;
    using ConfirmPlacementFn = std::function<void(const glm::vec3&)>;

    void reset();

    int selectedLoadoutIndex() const;
    void setSelectedLoadoutIndex(int index);
    bool hasActiveSelection() const;

    void cancelPlacement();

    void updateSelectionHotkeys(std::size_t loadoutSize);
    void updatePlacementFromInput(bool hasSelectedTower,
                                  const RaycastGroundFn& raycastGround,
                                  const CanPlaceFn& canPlace,
                                  const ConfirmPlacementFn& confirmPlacement);

    const PlacementState& state() const;

  private:
    int selectedLoadoutIndex_ = -1;
    PlacementState state_{};
};
