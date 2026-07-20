#include "scenes/PlayLevelTowerPlacementController.hpp"

#include <imgui.h>

void PlayLevelTowerPlacementController::reset() {
    selectedLoadoutIndex_ = -1;
    state_ = {};
}

int PlayLevelTowerPlacementController::selectedLoadoutIndex() const {
    return selectedLoadoutIndex_;
}

void PlayLevelTowerPlacementController::setSelectedLoadoutIndex(int index) {
    selectedLoadoutIndex_ = index;
}

bool PlayLevelTowerPlacementController::hasActiveSelection() const {
    return selectedLoadoutIndex_ >= 0;
}

void PlayLevelTowerPlacementController::cancelPlacement() {
    selectedLoadoutIndex_ = -1;
    state_.hasHit = false;
    state_.canPlace = false;
}

void PlayLevelTowerPlacementController::updateSelectionHotkeys(std::size_t loadoutSize) {
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
        selectedLoadoutIndex_ = (loadoutSize >= 1) ? 0 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_2, false)) {
        selectedLoadoutIndex_ = (loadoutSize >= 2) ? 1 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
        selectedLoadoutIndex_ = (loadoutSize >= 3) ? 2 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_4, false)) {
        selectedLoadoutIndex_ = (loadoutSize >= 4) ? 3 : -1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_5, false)) {
        selectedLoadoutIndex_ = (loadoutSize >= 5) ? 4 : -1;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancelPlacement();
    }
}

void PlayLevelTowerPlacementController::updatePlacementFromInput(bool hasSelectedTower,
                                                                 const RaycastGroundFn& raycastGround,
                                                                 const CanPlaceFn& canPlace,
                                                                 const ConfirmPlacementFn& confirmPlacement) {
    if (!hasSelectedTower) {
        state_.hasHit = false;
        state_.canPlace = false;
        return;
    }

    glm::vec3 hitPos{0.0f};
    state_.hasHit = raycastGround ? raycastGround(hitPos) : false;
    if (!state_.hasHit) {
        state_.canPlace = false;
        return;
    }

    state_.worldPos = hitPos;
    state_.worldPos.y = 0.0f;
    state_.canPlace = canPlace ? canPlace(state_.worldPos) : false;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }

    if (state_.canPlace && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && confirmPlacement) {
        confirmPlacement(state_.worldPos);
    }
}

const PlayLevelTowerPlacementController::PlacementState& PlayLevelTowerPlacementController::state() const {
    return state_;
}
