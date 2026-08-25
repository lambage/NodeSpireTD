#include "scenes/PlayLevelFrameCoordinator.hpp"

bool PlayLevelFrameCoordinator::run(Context& context, const Callbacks& callbacks, float dt) const {
    if (context.worldRenderer && !context.worldRenderer->isLoaded() && !context.worldRenderer->loadFailed()) {
        context.worldRenderer->tickLoad();
    }

    const bool isLoaded = context.worldRenderer && context.worldRenderer->isLoaded();
    if (!isLoaded) {
        return false;
    }

    if (context.placementRegions.empty()) {
        context.placementRegions = context.worldRenderer->placementRegions();
    }

    callbacks.updateRouteFromWorld();
    callbacks.syncTowerInstanceTransforms();
    callbacks.syncPlacedTowerModels();
    callbacks.updateCamera(dt);
    callbacks.updateTowerPlacementFromInput();
    callbacks.syncTowerInstanceTransforms();
    callbacks.syncPlacedTowerModels();

    const glm::mat4 viewMatrix = callbacks.buildViewMatrix();
    const bool hoverChanged = context.pickingController.updateHoverFromMouse(
        context.worldRenderer, viewMatrix, context.cameraController.position(), context.renderExtent);
    if (hoverChanged) {
        callbacks.syncTowerInstanceTransforms();
        callbacks.syncPlacedTowerModels();
    }

    context.pickingController.updateSelectionFromMouse(context.worldRenderer, viewMatrix,
                                                       context.cameraController.position(),
                                                       context.selectionBlocked);
    const int selectedInstanceIndex = context.pickingController.selectedInstanceIndex();
    const bool selectedIsEnemy = context.pickingController.selectedEntityKind() == WorldEntityKind::Enemy;
    context.selectedEnemyRuntimeId =
        (selectedIsEnemy && selectedInstanceIndex >= 0 &&
         selectedInstanceIndex < static_cast<int>(context.activeEnemies.size()))
            ? context.activeEnemies[static_cast<std::size_t>(selectedInstanceIndex)].runtimeId
            : 0;
    context.worldRenderer->setHighlightedInstances(
        context.pickingController.hoveredEntityKind(), context.pickingController.hoveredInstanceIndex(),
        context.pickingController.selectedEntityKind(), context.pickingController.selectedInstanceIndex());
    return true;
}
