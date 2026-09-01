#include "scenes/PlayLevelPickingController.hpp"

#include "utility/WorldRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <limits>

namespace {

constexpr float kPickStaticRadiusScale = 1.0f;
constexpr float kPickStaticRadiusPadding = 0.05f;
constexpr float kPickStaticMinRadius = 0.10f;
// Enemies are small, mobile, and easy to lose track of, so they get a generous,
// distance-forgiving pick radius.
constexpr float kPickInstancedRadiusScale = 1.45f;
constexpr float kPickInstancedRadiusPadding = 0.35f;
constexpr float kPickInstancedMinRadius = 0.65f;
// Towers are stationary and packed more tightly on the grid, so their pick radius is much
// tighter than an enemy's -- otherwise neighboring towers' hover/click zones overlap and
// hovering feels far too generous/imprecise.
constexpr float kPickTowerRadiusScale = 0.55f;
constexpr float kPickTowerRadiusPadding = 0.05f;
constexpr float kPickTowerMinRadius = 0.30f;
constexpr float kPickingFovRadians = glm::radians(60.0f);

enum class ProjectionRejectReason {
    None = 0,
    BehindCamera = 1,
    ClipW = 2,
    NdcZ = 3,
};

bool isSelectableSelection(const PlayLevelPickingController::ModelSelection& selection) {
    return selection.entityKind != WorldEntityKind::None;
}

bool projectWorldToScreen(const glm::vec3& worldPos,
                          const glm::mat4& view,
                          const glm::mat4& proj,
                          const ImVec2& displaySize,
                          const ImVec2& renderSize,
                          ImVec2& outScreen,
                          float& outDepthAbs,
                          ProjectionRejectReason* outRejectReason = nullptr) {
    if (outRejectReason) {
        *outRejectReason = ProjectionRejectReason::None;
    }

    const glm::vec4 viewPos = view * glm::vec4(worldPos, 1.0f);
    if (viewPos.z >= -1e-4f) {
        if (outRejectReason) {
            *outRejectReason = ProjectionRejectReason::BehindCamera;
        }
        return false;
    }
    outDepthAbs = -viewPos.z;

    const glm::vec4 clip = proj * viewPos;
    if (clip.w <= 1e-6f) {
        if (outRejectReason) {
            *outRejectReason = ProjectionRejectReason::ClipW;
        }
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        if (outRejectReason) {
            *outRejectReason = ProjectionRejectReason::NdcZ;
        }
        return false;
    }

    const float renderX = (ndc.x * 0.5f + 0.5f) * renderSize.x;
    const float renderY = (ndc.y * 0.5f + 0.5f) * renderSize.y;

    const float sx = (renderSize.x > 1e-5f) ? (displaySize.x / renderSize.x) : 1.0f;
    const float sy = (renderSize.y > 1e-5f) ? (displaySize.y / renderSize.y) : 1.0f;
    outScreen.x = renderX * sx;
    outScreen.y = renderY * sy;
    return true;
}

WorldPickOptions makeDefaultPickOptions() {
    WorldPickOptions pickOptions{};
    pickOptions.staticRadiusScale = kPickStaticRadiusScale;
    pickOptions.staticRadiusPadding = kPickStaticRadiusPadding;
    pickOptions.staticMinRadius = kPickStaticMinRadius;
    pickOptions.instancedRadiusScale = kPickInstancedRadiusScale;
    pickOptions.instancedRadiusPadding = kPickInstancedRadiusPadding;
    pickOptions.instancedMinRadius = kPickInstancedMinRadius;
    pickOptions.towerRadiusScale = kPickTowerRadiusScale;
    pickOptions.towerRadiusPadding = kPickTowerRadiusPadding;
    pickOptions.towerMinRadius = kPickTowerMinRadius;
    return pickOptions;
}

} // namespace

void PlayLevelPickingController::reset() {
    pickingEnabled_ = true;
    pickSpheresVisible_ = false;
    selectedSelection_ = {};
    hoverSelection_ = {};
    hoveredEntityKind_ = WorldEntityKind::None;
    hoveredInstanceIndex_ = -1;
    selectedEntityKind_ = WorldEntityKind::None;
    selectedInstanceIndex_ = -1;
    status_ = "click in world to inspect";
    overlayStats_ = {};
}

void PlayLevelPickingController::setPickingEnabled(bool enabled) {
    pickingEnabled_ = enabled;
}

bool PlayLevelPickingController::pickingEnabled() const {
    return pickingEnabled_;
}

void PlayLevelPickingController::setPickSpheresVisible(bool visible) {
    pickSpheresVisible_ = visible;
}

bool PlayLevelPickingController::pickSpheresVisible() const {
    return pickSpheresVisible_;
}

int PlayLevelPickingController::hoveredInstanceIndex() const {
    return hoveredInstanceIndex_;
}

int PlayLevelPickingController::selectedInstanceIndex() const {
    return selectedInstanceIndex_;
}

WorldEntityKind PlayLevelPickingController::hoveredEntityKind() const {
    return hoveredEntityKind_;
}

WorldEntityKind PlayLevelPickingController::selectedEntityKind() const {
    return selectedEntityKind_;
}

const PlayLevelPickingController::ModelSelection& PlayLevelPickingController::hoverSelection() const {
    return hoverSelection_;
}

const PlayLevelPickingController::ModelSelection& PlayLevelPickingController::selectedSelection() const {
    return selectedSelection_;
}

const PlayLevelPickingController::OverlayStats& PlayLevelPickingController::overlayStats() const {
    return overlayStats_;
}

const std::string& PlayLevelPickingController::status() const {
    return status_;
}

void PlayLevelPickingController::setSelectedSelection(const ModelSelection& selection, const std::string& status) {
    selectedSelection_ = selection;
    selectedEntityKind_ = selection.entityKind;
    selectedInstanceIndex_ = selection.instanceIndex;
    status_ = status;
}

void PlayLevelPickingController::setSelectedInstanceIndex(int instanceIndex) {
    selectedInstanceIndex_ = instanceIndex;
    if (selectedSelection_.valid && selectedSelection_.entityKind != WorldEntityKind::None) {
        selectedSelection_.instanceIndex = instanceIndex;
    }
}

void PlayLevelPickingController::setStatus(const char* status) {
    if (status) {
        status_ = status;
    }
}

void PlayLevelPickingController::clearSelection(const char* reason) {
    selectedSelection_ = {};
    selectedEntityKind_ = WorldEntityKind::None;
    selectedInstanceIndex_ = -1;
    if (reason) {
        status_ = reason;
    }
}

bool PlayLevelPickingController::pickModelAtScreen(const WorldRenderer* worldRenderer,
                                                   const glm::mat4& view,
                                                   const glm::vec3& rayOrigin,
                                                   float screenX,
                                                   float screenY,
                                                   ModelSelection& outSelection) const {
    if (!worldRenderer || !worldRenderer->isLoaded()) {
        return false;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return false;
    }

    const float ndcX = (2.0f * screenX) / displaySize.x - 1.0f;
    const float ndcY = (2.0f * screenY) / displaySize.y - 1.0f;

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(kPickingFovRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;

    const glm::mat4 invVP = glm::inverse(proj * view);

    const glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec4 nearWorld = invVP * nearClip;
    glm::vec4 farWorld = invVP * farClip;
    if (std::abs(nearWorld.w) < 1e-6f || std::abs(farWorld.w) < 1e-6f) {
        return false;
    }
    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;

    glm::vec3 rayDir = glm::vec3(farWorld - nearWorld);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return false;
    }
    rayDir = glm::normalize(rayDir);

    WorldPickHit hit{};
    if (!worldRenderer->pickModel(rayOrigin, rayDir, hit, makeDefaultPickOptions())) {
        return false;
    }

    outSelection.valid = true;
    outSelection.group = hit.group;
    outSelection.label = hit.label;
    outSelection.meshIndex = hit.meshIndex;
    outSelection.nodeIndex = hit.nodeIndex;
    outSelection.skinIndex = hit.skinIndex;
    outSelection.entityKind = hit.entityKind;
    outSelection.instanceIndex = hit.instanceIndex;
    outSelection.distance = hit.distance;
    outSelection.hitPosition = hit.worldPosition;
    outSelection.hitNormal = hit.worldNormal;
    return true;
}

bool PlayLevelPickingController::pickModelAtCursor(const WorldRenderer* worldRenderer,
                                                   const glm::mat4& view,
                                                   const glm::vec3& rayOrigin,
                                                   ModelSelection& outSelection) const {
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return false;
    }
    return pickModelAtScreen(worldRenderer, view, rayOrigin, mousePos.x, mousePos.y, outSelection);
}

void PlayLevelPickingController::updateSelectionFromMouse(const WorldRenderer* worldRenderer,
                                                          const glm::mat4& view,
                                                          const glm::vec3& rayOrigin,
                                                          bool selectionBlockedByPlacement) {
    if (selectionBlockedByPlacement || !pickingEnabled_) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }

    if (hoverSelection_.valid && isSelectableSelection(hoverSelection_)) {
        selectedSelection_ = hoverSelection_;
        selectedEntityKind_ = hoverSelection_.entityKind;
        selectedInstanceIndex_ = hoverSelection_.instanceIndex;
        status_ = "picked " + hoverSelection_.group + " / " + hoverSelection_.label;
        return;
    }

    ModelSelection selection{};
    if (pickModelAtCursor(worldRenderer, view, rayOrigin, selection) && isSelectableSelection(selection)) {
        selectedSelection_ = selection;
        selectedEntityKind_ = selection.entityKind;
        selectedInstanceIndex_ = selection.instanceIndex;
        status_ = "picked " + selection.group + " / " + selection.label;
    } else if (selection.valid) {
        clearSelection("hit non-selectable model");
    } else {
        clearSelection("no model hit at cursor");
    }
}

bool PlayLevelPickingController::updateHoverFromMouse(const WorldRenderer* worldRenderer,
                                                      const glm::mat4& view,
                                                      const glm::vec3& rayOrigin,
                                                      VkExtent2D lastRenderExtent) {
    const int previousHoveredInstanceIndex = hoveredInstanceIndex_;
    const bool previousHoverValid = hoverSelection_.valid;

    hoveredEntityKind_ = WorldEntityKind::None;
    hoveredInstanceIndex_ = -1;
    hoverSelection_ = {};

    if (!pickingEnabled_) {
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    const ImVec2 mousePos = io.MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    ModelSelection hover{};
    if (pickModelAtCursor(worldRenderer, view, rayOrigin, hover) && isSelectableSelection(hover)) {
        hoverSelection_ = hover;
        hoveredEntityKind_ = hover.entityKind;
        hoveredInstanceIndex_ = hover.instanceIndex;
        return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
    }

    if (worldRenderer && worldRenderer->isLoaded()) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 renderSize(
            (lastRenderExtent.width > 0) ? static_cast<float>(lastRenderExtent.width) : displaySize.x,
            (lastRenderExtent.height > 0) ? static_cast<float>(lastRenderExtent.height) : displaySize.y);

        const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
        glm::mat4 proj = glm::perspective(kPickingFovRadians, aspect, 0.05f, 2000.0f);
        proj[1][1] *= -1.0f;

        const std::vector<WorldPickDebugSphere> spheres = worldRenderer->buildDynamicPickDebugSpheres(makeDefaultPickOptions());
        const float focalPixels = displaySize.y / (2.0f * std::tan(kPickingFovRadians * 0.5f));

        WorldEntityKind bestKind = WorldEntityKind::None;
        int bestInstance = -1;
        float bestNormalizedDist2 = std::numeric_limits<float>::max();
        glm::vec3 bestCenter{0.0f};
        std::string bestGroup;
        std::string bestLabel;

        for (const WorldPickDebugSphere& sphere : spheres) {
            ImVec2 screenPos{};
            float depthAbs = 0.0f;
            if (!projectWorldToScreen(sphere.center, view, proj, displaySize, renderSize, screenPos, depthAbs, nullptr)) {
                continue;
            }

            float radiusPixels = sphere.radius * focalPixels / std::max(0.1f, depthAbs);
            if (!std::isfinite(radiusPixels)) {
                continue;
            }
            // Floor the clickable target size in screen space so towers/enemies remain
            // selectable even when zoomed/flown far enough away that their true projected
            // size drops below a pixel. Without this, radiusPixels shrinks to 0 at a distance
            // and selection becomes impossible (not because of any world/collision bounds --
            // purely a screen-space projection artifact).
            constexpr float kMinPickRadiusPixels = 8.0f;
            radiusPixels = std::max(radiusPixels, kMinPickRadiusPixels);

            const float dx = mousePos.x - screenPos.x;
            const float dy = mousePos.y - screenPos.y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 > radiusPixels * radiusPixels) {
                continue;
            }

            const float normalizedDist2 = dist2 / std::max(1.0f, radiusPixels * radiusPixels);
            if (normalizedDist2 < bestNormalizedDist2) {
                bestNormalizedDist2 = normalizedDist2;
                bestKind = sphere.entityKind;
                bestInstance = sphere.instanceIndex;
                bestCenter = sphere.center;
                bestGroup = sphere.group;
                bestLabel = sphere.label;
            }
        }

        if (bestKind != WorldEntityKind::None) {
            hoverSelection_.valid = true;
            hoverSelection_.entityKind = bestKind;
            hoverSelection_.instanceIndex = bestInstance;
            hoverSelection_.group = bestGroup;
            hoverSelection_.label = bestLabel;
            hoverSelection_.hitPosition = bestCenter;
            hoveredEntityKind_ = bestKind;
            hoveredInstanceIndex_ = bestInstance;
        }
    }

    return (previousHoveredInstanceIndex != hoveredInstanceIndex_) || (previousHoverValid != hoverSelection_.valid);
}

void PlayLevelPickingController::drawPickSpheresOverlay(const WorldRenderer* worldRenderer,
                                                        const glm::mat4& view,
                                                        VkExtent2D lastRenderExtent) const {
    overlayStats_ = {};

    if (!pickSpheresVisible_ || !worldRenderer || !worldRenderer->isLoaded()) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return;
    }

    const ImVec2 renderSize(
        (lastRenderExtent.width > 0) ? static_cast<float>(lastRenderExtent.width) : displaySize.x,
        (lastRenderExtent.height > 0) ? static_cast<float>(lastRenderExtent.height) : displaySize.y);
    overlayStats_.displayWidth = displaySize.x;
    overlayStats_.displayHeight = displaySize.y;
    overlayStats_.renderWidth = renderSize.x;
    overlayStats_.renderHeight = renderSize.y;

    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;
    glm::mat4 proj = glm::perspective(kPickingFovRadians, aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f;

    const std::vector<WorldPickDebugSphere> spheres = worldRenderer->buildDynamicPickDebugSpheres(makeDefaultPickOptions());
    overlayStats_.sphereTotal = static_cast<int>(spheres.size());
    if (spheres.empty()) {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const float focalPixels = displaySize.y / (2.0f * std::tan(kPickingFovRadians * 0.5f));

    for (const WorldPickDebugSphere& sphere : spheres) {
        ImVec2 screenPos{};
        float depthAbs = 0.0f;
        ProjectionRejectReason rejectReason = ProjectionRejectReason::None;
        if (!projectWorldToScreen(sphere.center, view, proj, displaySize, renderSize, screenPos, depthAbs, &rejectReason)) {
            if (rejectReason == ProjectionRejectReason::BehindCamera) {
                ++overlayStats_.rejectBehindCamera;
            } else if (rejectReason == ProjectionRejectReason::ClipW) {
                ++overlayStats_.rejectClipW;
            } else if (rejectReason == ProjectionRejectReason::NdcZ) {
                ++overlayStats_.rejectNdcZ;
            }
            continue;
        }

        const float radiusPixels = sphere.radius * focalPixels / std::max(0.1f, depthAbs);
        if (!std::isfinite(radiusPixels) || radiusPixels < 1.0f) {
            ++overlayStats_.rejectRadius;
            continue;
        }

        const bool hovered = (sphere.entityKind == hoveredEntityKind_) && (sphere.instanceIndex == hoveredInstanceIndex_) &&
                            (hoveredEntityKind_ != WorldEntityKind::None);
        const ImU32 col = hovered ? IM_COL32(255, 220, 64, 235) : IM_COL32(64, 200, 255, 180);
        drawList->AddCircle(screenPos, radiusPixels, col, 42, hovered ? 2.5f : 1.5f);
        ++overlayStats_.sphereDrawn;
        if (hovered) {
            overlayStats_.hoveredSphereFound = 1;
            overlayStats_.hoveredDepth = depthAbs;
            overlayStats_.hoveredRadiusPixels = radiusPixels;
        }
    }
}
