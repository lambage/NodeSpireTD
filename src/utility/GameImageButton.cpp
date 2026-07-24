#include "GameImageButton.hpp"

#include "VulkanContext.hpp"
#include "utility/VulkanTexture.hpp"


GameImageButton::GameImageButton(const std::string& id, VulkanContext& context, AudioEngine& audioEngine,
                                 const std::string& imagePath, const std::string& imageHoverPath, float width,
                                 float height, std::function<void()> onClickCallback, std::string hoverSoundPath,
                                 std::string clickSoundPath)
    : id_(id), width_(width), height_(height), onClickCallback_(std::move(onClickCallback)),
      hoverSoundPath_(std::move(hoverSoundPath)), clickSoundPath_(std::move(clickSoundPath)),
      audioEngine_(audioEngine),
      texture_(std::make_unique<VulkanTexture>(context.device(), context.allocator(), context.commandPool(),
                                               context.graphicsQueue())),
      textureHover_(std::make_unique<VulkanTexture>(context.device(), context.allocator(), context.commandPool(),
                                                    context.graphicsQueue())) {
    if (!texture_->loadFromFile(imagePath)) {
        throw std::runtime_error("Failed to load texture from file: " + imagePath);
    }
    if (!textureHover_->loadFromFile(imageHoverPath)) {
        throw std::runtime_error("Failed to load hover texture from file: " + imageHoverPath);
    }

    if (!hoverSoundPath_.empty()) {
        audioEngine_.preload(hoverSoundPath_, AudioChannel::Sfx);
    }
    if (!clickSoundPath_.empty()) {
        audioEngine_.preload(clickSoundPath_, AudioChannel::Sfx);
    }
}

GameImageButton::~GameImageButton() = default;

void GameImageButton::setSize(float width, float height) {
    width_ = width;
    height_ = height;
}

bool GameImageButton::render() {
    const bool clicked = ImGui::ImageButton(id_.c_str(), isHovered_ ? textureHover_->textureRef() : texture_->textureRef(),
    ImVec2(width_, height_));
    // due to how ImGui works we're always behind a frame
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
