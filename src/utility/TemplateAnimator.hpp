#pragma once

#include "utility/TemplateAnimationDebugInfo.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace fastgltf {
struct Asset;
}

class TemplateAnimator {
  public:
    TemplateAnimator() = default;

    void reset();
    void initializeFromAsset(const fastgltf::Asset& modelAsset);
    void update(float dtSeconds);

    bool hasAnimation() const { return animationEnabled_; }
    const std::string& animationName() const { return animationName_; }
    int animationClipCount() const { return static_cast<int>(animationClips_.size()); }
    int activeAnimationClipIndex() const { return activeAnimationClipIndex_; }
    std::vector<std::string> animationClipNames() const;

    bool setActiveAnimationClipByIndex(int clipIndex);
    bool setActiveAnimationClipByName(const std::string& clipName);

    void setCompositeMode(bool enabled) { playAllAnimationClips_ = enabled; }
    bool compositeMode() const { return playAllAnimationClips_; }

    const TemplateAnimationDebugInfo& debugInfo() const { return debugInfo_; }

    glm::mat4 resolveNodeTransform(int nodeIndex, const glm::mat4& fallback) const;
    const std::vector<glm::mat4>* skinJointMatricesForSkin(int skinIndex) const;

  private:
    enum class AnimationPath {
        Translation,
        Rotation,
        Scale
    };

    struct AnimationTrack {
        int nodeIndex = -1;
        AnimationPath path = AnimationPath::Translation;
        bool stepInterpolation = false;
        std::vector<float> times;
        std::vector<glm::vec3> vec3Values;
        std::vector<glm::quat> quatValues;
    };

    struct AnimationClip {
        std::string name;
        float durationSeconds = 0.0f;
        std::vector<AnimationTrack> tracks;
    };

    struct Skin {
        std::vector<int> jointNodes;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<glm::mat4> jointMatrices;
    };

    std::vector<int> nodeParents_;
    std::vector<glm::vec3> baseTranslations_;
    std::vector<glm::quat> baseRotations_;
    std::vector<glm::vec3> baseScales_;
    std::vector<glm::mat4> nodeWorldTransforms_;
    std::vector<std::size_t> sceneRootNodes_;
    std::vector<Skin> skins_;
    std::vector<AnimationClip> animationClips_;

    int activeAnimationClipIndex_ = -1;
    bool playAllAnimationClips_ = false;
    bool animationEnabled_ = false;
    std::string animationName_;
    float animationTimeSeconds_ = 0.0f;
    TemplateAnimationDebugInfo debugInfo_{};
};
