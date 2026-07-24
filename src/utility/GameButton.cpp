#include "GameButton.hpp"

#include <imgui.h>

GameButton::GameButton(const std::string& id, AudioEngine& audioEngine, const std::string& label, float width,
                       float height, std::function<void()> onClickCallback, std::string hoverSoundPath,
                       std::string clickSoundPath)
    : id_(id), label_(label), width_(width), height_(height), onClickCallback_(std::move(onClickCallback)),
      hoverSoundPath_(std::move(hoverSoundPath)), clickSoundPath_(std::move(clickSoundPath)),
      audioEngine_(audioEngine) {
    if (!hoverSoundPath_.empty()) {
        audioEngine_.preload(hoverSoundPath_, AudioChannel::Sfx);
    }
    if (!clickSoundPath_.empty()) {
        audioEngine_.preload(clickSoundPath_, AudioChannel::Sfx);
    }
}

GameButton::~GameButton() = default;

void GameButton::setLabel(const std::string& label) {
    label_ = label;
}

void GameButton::setSize(float width, float height) {
    width_ = width;
    height_ = height;
}

bool GameButton::render() {
    const std::string displayId = label_ + "##" + id_;
    const bool clicked = ImGui::Button(displayId.c_str(), ImVec2(width_, height_));
    const bool nowHovered = ImGui::IsItemHovered();

    if (nowHovered && !isHovered_ && !hoverSoundPath_.empty()) {
        audioEngine_.play(hoverSoundPath_, AudioChannel::Sfx);
    }

    if (clicked) {
        if (!clickSoundPath_.empty()) {
            audioEngine_.play(clickSoundPath_, AudioChannel::Sfx);
        }
        if (onClickCallback_) {
            onClickCallback_();
        }
    }

    isHovered_ = nowHovered;
    return clicked;
}
