#pragma once

#include "lambui/SceneTypes.hpp"

#include <cstdint>
#include <volk.h>

class AudioEngine;

namespace LambUI {
class UIManager;
}

namespace NodeSpireUi {

// A screen driven by LambUI widgets/Lua scripts. Implementations build their
// widget tree in onEnter() and tear it down in onExit(); LambUI itself
// handles rendering the active widget tree each frame via
// LambUI::UIManager::Render(), so scenes don't need a render() method for UI.
class IScene {
  public:
    virtual ~IScene() = default;

    virtual void onEnter(LambUI::UIManager& ui, AudioEngine& audio, float viewportWidth, float viewportHeight) = 0;
    virtual void onExit(LambUI::UIManager& ui) = 0;
    virtual void onLayoutChanged(float /*width*/, float /*height*/) {}

    // Called once per frame before ui.Update(). Return a SceneId to request a
    // transition.
    virtual SceneTransition update(float dt) = 0;

    // Records scene-owned 3D rendering before LambUI in the application's active command buffer.
    virtual void renderWorld(VkCommandBuffer /*commandBuffer*/, VkExtent2D /*extent*/) {}

    // Records scene-owned overlays after LambUI while the application's render pass is still active.
    virtual void renderOverlay(VkCommandBuffer /*commandBuffer*/, VkExtent2D /*extent*/) {}

    // Called for every key press while this scene is active, using
    // LambUI::ScanCode values. Return a SceneId to request a transition;
    // return std::nullopt to leave the key unhandled.
    virtual SceneTransition onKeyDown(uint32_t /*scanCode*/) {
        return std::nullopt;
    }

    // Called after LambUI leaves a key unhandled. Return true when the active scene consumes it.
    virtual bool handleShortcut(uint32_t /*scanCode*/) { return false; }
};

} // namespace NodeSpireUi
