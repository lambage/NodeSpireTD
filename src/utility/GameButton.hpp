#pragma once

#include "AudioEngine.hpp"

#include <functional>
#include <string>

// ImGui text button rendered inline in the current layout, with optional
// hover/click sfx wired through the shared AudioEngine.
class GameButton {
  public:
    GameButton(const std::string& id, AudioEngine& audioEngine, const std::string& label, float width, float height,
               std::function<void()> onClickCallback = nullptr, std::string hoverSoundPath = std::string(),
               std::string clickSoundPath = std::string());
    ~GameButton();

    // Renders the button and returns true if it was clicked this frame.
    bool render();

    void setLabel(const std::string& label);
    void setSize(float width, float height);

  private:
    std::string id_;
    std::string label_;
    float width_;
    float height_;
    std::function<void()> onClickCallback_;
    std::string hoverSoundPath_;
    std::string clickSoundPath_;

    AudioEngine& audioEngine_;
    bool isHovered_ = false;
};
