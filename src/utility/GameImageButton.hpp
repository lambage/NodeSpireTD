#pragma once

#include "AudioEngine.hpp"
#include "VulkanTexture.hpp"

#include <functional>
#include <memory>
#include <string>

class VulkanTexture;
class VulkanContext;

// ImGui image button rendered inline in the current layout, with optional
// hover/click sfx wired through the shared AudioEngine.
class GameImageButton {
  public:
    GameImageButton(const std::string& id, VulkanContext& context, AudioEngine& audioEngine,
                    const std::string& imagePath, const std::string& imageHoverPath, float width, float height,
                    std::function<void()> onClickCallback, std::string hoverSoundPath = std::string(),
                    std::string clickSoundPath = std::string());
    ~GameImageButton();

    // Renders the button and returns true if it was clicked this frame.
    bool render();

    void setSize(float width, float height);

  private:
    std::string id_;
    float width_;
    float height_;
    std::function<void()> onClickCallback_;
    std::string hoverSoundPath_;
    std::string clickSoundPath_;

    AudioEngine& audioEngine_;
    std::unique_ptr<VulkanTexture> texture_;
    std::unique_ptr<VulkanTexture> textureHover_;

    bool isHovered_ = false;
};