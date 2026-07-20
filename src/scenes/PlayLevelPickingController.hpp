#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <string>
#include <vulkan/vulkan.h>

class WorldRenderer;

class PlayLevelPickingController {
  public:
    struct ModelSelection {
      bool valid = false;
      std::string group;
      std::string label;
      int meshIndex = -1;
      int nodeIndex = -1;
      int skinIndex = -1;
      int instanceIndex = -1;
      float distance = 0.0f;
      glm::vec3 hitPosition{0.0f};
      glm::vec3 hitNormal{0.0f, 1.0f, 0.0f};
    };

    struct OverlayStats {
      int sphereTotal = 0;
      int sphereDrawn = 0;
      int rejectBehindCamera = 0;
      int rejectClipW = 0;
      int rejectNdcZ = 0;
      int rejectRadius = 0;
      int hoveredSphereFound = 0;
      int hoveredRejectReason = 0;
      float hoveredDepth = 0.0f;
      float hoveredRadiusPixels = 0.0f;
      float displayWidth = 0.0f;
      float displayHeight = 0.0f;
      float renderWidth = 0.0f;
      float renderHeight = 0.0f;
    };

    void reset();

    void setPickingEnabled(bool enabled);
    bool pickingEnabled() const;

    void setPickSpheresVisible(bool visible);
    bool pickSpheresVisible() const;

    int hoveredInstanceIndex() const;
    int selectedInstanceIndex() const;

    const ModelSelection& hoverSelection() const;
    const ModelSelection& selectedSelection() const;
    const OverlayStats& overlayStats() const;
    const std::string& status() const;

    void setSelectedSelection(const ModelSelection& selection, const std::string& status);
    void setSelectedInstanceIndex(int instanceIndex);
    void setStatus(const char* status);
    void clearSelection(const char* reason);

    bool updateHoverFromMouse(const WorldRenderer* worldRenderer,
                  const glm::mat4& view,
                  const glm::vec3& rayOrigin,
                  VkExtent2D lastRenderExtent);
    void updateSelectionFromMouse(const WorldRenderer* worldRenderer,
                                  const glm::mat4& view,
                                  const glm::vec3& rayOrigin,
                                  bool selectionBlockedByPlacement);

    bool pickAtCursor(const WorldRenderer* worldRenderer,
                      const glm::mat4& view,
                      const glm::vec3& rayOrigin,
                      ModelSelection& outSelection) const;

    void drawPickSpheresOverlay(const WorldRenderer* worldRenderer, const glm::mat4& view, VkExtent2D lastRenderExtent) const;

  private:
    bool pickModelAtScreen(const WorldRenderer* worldRenderer,
                           const glm::mat4& view,
                           const glm::vec3& rayOrigin,
                           float screenX,
                           float screenY,
                           ModelSelection& outSelection) const;

    bool pickModelAtCursor(const WorldRenderer* worldRenderer,
                           const glm::mat4& view,
                           const glm::vec3& rayOrigin,
                           ModelSelection& outSelection) const;

    bool pickingEnabled_ = true;
    bool pickSpheresVisible_ = false;
    ModelSelection selectedSelection_{};
    ModelSelection hoverSelection_{};
    int hoveredInstanceIndex_ = -1;
    int selectedInstanceIndex_ = -1;
    std::string status_ = "click in world to inspect";
    mutable OverlayStats overlayStats_{};
};
