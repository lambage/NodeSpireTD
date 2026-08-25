#pragma once

#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/PlayLevelCameraController.hpp"
#include "scenes/PlayLevelPickingController.hpp"
#include "utility/WorldAssetLoader.hpp"
#include "utility/WorldRenderer.hpp"

#include <cstdint>
#include <functional>
#include <vector>

class PlayLevelFrameCoordinator {
  public:
    struct Context {
        WorldRenderer* worldRenderer = nullptr;
        std::vector<TowerPlacementRegion>& placementRegions;
        PlayLevelPickingController& pickingController;
        PlayLevelCameraController& cameraController;
        const std::vector<playlevel::ActiveEnemy>& activeEnemies;
        std::uint64_t& selectedEnemyRuntimeId;
        bool selectionBlocked = false;
        VkExtent2D renderExtent{};
    };

    struct Callbacks {
        std::function<void()> updateRouteFromWorld;
        std::function<void()> syncTowerInstanceTransforms;
        std::function<void()> syncPlacedTowerModels;
        std::function<void(float)> updateCamera;
        std::function<void()> updateTowerPlacementFromInput;
        std::function<glm::mat4()> buildViewMatrix;
    };

    bool run(Context& context, const Callbacks& callbacks, float dt) const;
};
