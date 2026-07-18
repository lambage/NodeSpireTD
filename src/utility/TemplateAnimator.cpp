#include "utility/TemplateAnimator.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <functional>

namespace {

glm::mat4 composeTRS(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale) {
    return glm::translate(glm::mat4{1.0f}, translation) *
           glm::mat4_cast(rotation) *
           glm::scale(glm::mat4{1.0f}, scale);
}

std::size_t findAnimationSegment(const std::vector<float>& times, float t) {
    if (times.size() <= 1) {
        return 0;
    }

    if (t <= times.front()) {
        return 0;
    }
    if (t >= times.back()) {
        return times.size() - 2;
    }

    const auto it = std::upper_bound(times.begin(), times.end(), t);
    const std::size_t index = static_cast<std::size_t>(std::distance(times.begin(), it));
    return (index == 0) ? 0 : index - 1;
}

float normalizedSegmentT(const std::vector<float>& times, std::size_t segIndex, float t) {
    const float t0 = times[segIndex];
    const float t1 = times[segIndex + 1];
    if (t1 <= t0) {
        return 0.0f;
    }
    return glm::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f);
}

std::string normalizeName(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

void TemplateAnimator::reset() {
    nodeParents_.clear();
    baseTranslations_.clear();
    baseRotations_.clear();
    baseScales_.clear();
    nodeWorldTransforms_.clear();
    sceneRootNodes_.clear();
    skins_.clear();
    animationClips_.clear();

    activeAnimationClipIndex_ = -1;
    playAllAnimationClips_ = false;
    animationEnabled_ = false;
    animationName_.clear();
    animationTimeSeconds_ = 0.0f;
    debugInfo_ = {};
}

std::vector<std::string> TemplateAnimator::animationClipNames() const {
    std::vector<std::string> names;
    names.reserve(animationClips_.size());
    for (const AnimationClip& clip : animationClips_) {
        names.push_back(clip.name);
    }
    return names;
}

bool TemplateAnimator::setActiveAnimationClipByIndex(int clipIndex) {
    if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= animationClips_.size()) {
        return false;
    }

    activeAnimationClipIndex_ = clipIndex;
    animationEnabled_ = true;
    playAllAnimationClips_ = false;
    animationName_ = animationClips_[clipIndex].name;
    animationTimeSeconds_ = 0.0f;
    return true;
}

bool TemplateAnimator::setActiveAnimationClipByName(const std::string& clipName) {
    if (clipName.empty()) {
        return false;
    }

    const std::string needle = normalizeName(clipName);
    for (std::size_t i = 0; i < animationClips_.size(); ++i) {
        if (normalizeName(animationClips_[i].name) == needle) {
            return setActiveAnimationClipByIndex(static_cast<int>(i));
        }
    }
    return false;
}

void TemplateAnimator::initializeFromAsset(const fastgltf::Asset& modelAsset) {
    reset();

    nodeParents_.assign(modelAsset.nodes.size(), -1);
    baseTranslations_.assign(modelAsset.nodes.size(), glm::vec3{0.0f});
    baseRotations_.assign(modelAsset.nodes.size(), glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    baseScales_.assign(modelAsset.nodes.size(), glm::vec3{1.0f});
    nodeWorldTransforms_.assign(modelAsset.nodes.size(), glm::mat4{1.0f});

    std::vector<bool> childMask(modelAsset.nodes.size(), false);
    for (std::size_t i = 0; i < modelAsset.nodes.size(); ++i) {
        const fastgltf::Node& node = modelAsset.nodes[i];
        for (std::size_t child : node.children) {
            if (child < nodeParents_.size()) {
                nodeParents_[child] = static_cast<int>(i);
                childMask[child] = true;
            }
        }

        std::visit(fastgltf::visitor{
            [&](const fastgltf::TRS& trs) {
                baseTranslations_[i] = glm::vec3(trs.translation.x(), trs.translation.y(), trs.translation.z());
                baseRotations_[i] = glm::quat(trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z());
                baseScales_[i] = glm::vec3(trs.scale.x(), trs.scale.y(), trs.scale.z());
            },
            [&](const fastgltf::math::fmat4x4& mat) {
                glm::mat4 m{1.0f};
                std::memcpy(&m, mat.data(), sizeof(glm::mat4));
                baseTranslations_[i] = glm::vec3(m[3]);
                baseRotations_[i] = glm::quat_cast(m);
                baseScales_[i] = glm::vec3(
                    glm::length(glm::vec3(m[0])),
                    glm::length(glm::vec3(m[1])),
                    glm::length(glm::vec3(m[2])));
            }
        }, node.transform);
    }

    if (!modelAsset.scenes.empty()) {
        const std::size_t sceneIdx = modelAsset.defaultScene.has_value() ? *modelAsset.defaultScene : 0;
        for (std::size_t rootIdx : modelAsset.scenes[sceneIdx].nodeIndices) {
            sceneRootNodes_.push_back(rootIdx);
        }
    } else {
        for (std::size_t i = 0; i < childMask.size(); ++i) {
            if (!childMask[i]) {
                sceneRootNodes_.push_back(i);
            }
        }
    }

    skins_.clear();
    skins_.reserve(modelAsset.skins.size());
    for (const fastgltf::Skin& skin : modelAsset.skins) {
        Skin parsedSkin;
        parsedSkin.jointNodes.reserve(skin.joints.size());
        parsedSkin.inverseBindMatrices.assign(skin.joints.size(), glm::mat4{1.0f});
        parsedSkin.jointMatrices.assign(skin.joints.size(), glm::mat4{1.0f});

        for (std::size_t i = 0; i < skin.joints.size(); ++i) {
            parsedSkin.jointNodes.push_back(static_cast<int>(skin.joints[i]));
        }

        if (skin.inverseBindMatrices.has_value()) {
            const std::size_t accessorIndex = *skin.inverseBindMatrices;
            if (accessorIndex < modelAsset.accessors.size()) {
                const fastgltf::Accessor& ibmAccessor = modelAsset.accessors[accessorIndex];
                std::vector<glm::mat4> ibmValues(ibmAccessor.count, glm::mat4{1.0f});
                if (!ibmValues.empty()) {
                    fastgltf::copyFromAccessor<glm::mat4>(modelAsset, ibmAccessor, ibmValues.data());
                    const std::size_t copyCount = std::min(parsedSkin.inverseBindMatrices.size(), ibmValues.size());
                    for (std::size_t i = 0; i < copyCount; ++i) {
                        parsedSkin.inverseBindMatrices[i] = ibmValues[i];
                    }
                }
            }
        }

        skins_.push_back(std::move(parsedSkin));
    }

    for (const fastgltf::Animation& animation : modelAsset.animations) {
        AnimationClip clip;
        clip.name = animation.name.empty() ? "walk" : std::string(animation.name);

        for (const fastgltf::AnimationChannel& channel : animation.channels) {
            if (!channel.nodeIndex.has_value()) {
                continue;
            }
            if (channel.samplerIndex >= animation.samplers.size()) {
                continue;
            }

            const fastgltf::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
            if (sampler.inputAccessor >= modelAsset.accessors.size() ||
                sampler.outputAccessor >= modelAsset.accessors.size()) {
                continue;
            }

            const fastgltf::Accessor& inputAccessor = modelAsset.accessors[sampler.inputAccessor];
            const fastgltf::Accessor& outputAccessor = modelAsset.accessors[sampler.outputAccessor];

            AnimationTrack track;
            track.nodeIndex = static_cast<int>(*channel.nodeIndex);
            track.stepInterpolation = sampler.interpolation == fastgltf::AnimationInterpolation::Step;
            track.times.resize(inputAccessor.count);
            if (!track.times.empty()) {
                fastgltf::copyFromAccessor<float>(modelAsset, inputAccessor, track.times.data());
                clip.durationSeconds = std::max(clip.durationSeconds, track.times.back());
            }

            if (channel.path == fastgltf::AnimationPath::Translation ||
                channel.path == fastgltf::AnimationPath::Scale) {
                track.path = (channel.path == fastgltf::AnimationPath::Translation)
                                 ? AnimationPath::Translation
                                 : AnimationPath::Scale;
                std::vector<glm::vec3> packed(outputAccessor.count);
                if (!packed.empty()) {
                    fastgltf::copyFromAccessor<glm::vec3>(modelAsset, outputAccessor, packed.data());
                    if (sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline) {
                        track.vec3Values.reserve(track.times.size());
                        for (std::size_t i = 0; i < track.times.size(); ++i) {
                            const std::size_t valueIndex = i * 3 + 1;
                            if (valueIndex < packed.size()) {
                                track.vec3Values.push_back(packed[valueIndex]);
                            }
                        }
                    } else {
                        track.vec3Values = std::move(packed);
                    }
                }
            } else if (channel.path == fastgltf::AnimationPath::Rotation) {
                track.path = AnimationPath::Rotation;
                std::vector<glm::vec4> packed(outputAccessor.count);
                if (!packed.empty()) {
                    fastgltf::copyFromAccessor<glm::vec4>(modelAsset, outputAccessor, packed.data());
                    if (sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline) {
                        track.quatValues.reserve(track.times.size());
                        for (std::size_t i = 0; i < track.times.size(); ++i) {
                            const std::size_t valueIndex = i * 3 + 1;
                            if (valueIndex < packed.size()) {
                                const glm::vec4& q = packed[valueIndex];
                                track.quatValues.emplace_back(q.w, q.x, q.y, q.z);
                            }
                        }
                    } else {
                        track.quatValues.reserve(packed.size());
                        for (const glm::vec4& q : packed) {
                            track.quatValues.emplace_back(q.w, q.x, q.y, q.z);
                        }
                    }
                }
            } else {
                continue;
            }

            if ((track.path == AnimationPath::Rotation && track.quatValues.size() < track.times.size()) ||
                ((track.path == AnimationPath::Translation || track.path == AnimationPath::Scale) &&
                 track.vec3Values.size() < track.times.size())) {
                continue;
            }

            clip.tracks.push_back(std::move(track));
        }

        if (!clip.tracks.empty() && clip.durationSeconds > 0.0f) {
            animationClips_.push_back(std::move(clip));
        }
    }

    if (animationClips_.empty()) {
        return;
    }

    auto clipKeyScore = [](const AnimationClip& clip) {
        std::size_t totalKeys = 0;
        for (const AnimationTrack& track : clip.tracks) {
            totalKeys += track.times.size();
        }
        return totalKeys;
    };

    int bestIndex = 0;
    std::size_t bestScore = 0;
    int bestPriority = -1;

    for (std::size_t i = 0; i < animationClips_.size(); ++i) {
        const AnimationClip& candidate = animationClips_[i];
        const std::string name = normalizeName(candidate.name);

        int priority = 0;
        if (name == "walking") {
            priority = 3;
        } else if (name.find("walk") != std::string::npos) {
            priority = 2;
        } else if (name.find("idle") != std::string::npos) {
            priority = 1;
        }

        const std::size_t score = clipKeyScore(candidate);
        if (priority > bestPriority || (priority == bestPriority && score > bestScore)) {
            bestPriority = priority;
            bestScore = score;
            bestIndex = static_cast<int>(i);
        }
    }

    activeAnimationClipIndex_ = bestIndex;
    animationEnabled_ = true;
    animationName_ = animationClips_[activeAnimationClipIndex_].name;

    int actionLikeCount = 0;
    for (const AnimationClip& c : animationClips_) {
        const std::string n = normalizeName(c.name);
        if (n.find("action") != std::string::npos) {
            ++actionLikeCount;
        }
    }
    playAllAnimationClips_ = (animationClips_.size() > 1) &&
                             (actionLikeCount >= static_cast<int>(animationClips_.size() / 2));

    spdlog::info("[TemplateAnimator] Selected: '{}' (clip {}/{}, {} tracks, {:.2f}s).",
                 animationName_, activeAnimationClipIndex_ + 1,
                 animationClips_.size(),
                 animationClips_[activeAnimationClipIndex_].tracks.size(),
                 animationClips_[activeAnimationClipIndex_].durationSeconds);
    spdlog::info("[TemplateAnimator] Composite mode: {}.", playAllAnimationClips_ ? "ON" : "OFF");
}

void TemplateAnimator::update(float dtSeconds) {
    debugInfo_.enabled = false;
    debugInfo_.clipName.clear();
    debugInfo_.selectedClipIndex = activeAnimationClipIndex_;
    debugInfo_.clipCount = static_cast<int>(animationClips_.size());
    debugInfo_.compositeMode = playAllAnimationClips_;
    debugInfo_.compositeAppliedClips = 0;
    debugInfo_.timeSeconds = 0.0f;
    debugInfo_.durationSeconds = 0.0f;
    debugInfo_.trackCount = 0;
    debugInfo_.keyCount = 0;
    debugInfo_.keyIndex = 0;
    debugInfo_.nextKeyIndex = 0;
    debugInfo_.keyTimeSeconds = 0.0f;
    debugInfo_.nextKeyTimeSeconds = 0.0f;
    debugInfo_.segmentAlpha = 0.0f;
    debugInfo_.stepInterpolation = false;

    if (nodeWorldTransforms_.empty()) {
        return;
    }

    std::vector<glm::vec3> localT = baseTranslations_;
    std::vector<glm::quat> localR = baseRotations_;
    std::vector<glm::vec3> localS = baseScales_;

    if (animationEnabled_ && !animationClips_.empty()) {
        float timelineDuration = 0.0f;
        if (playAllAnimationClips_) {
            for (const AnimationClip& clip : animationClips_) {
                timelineDuration = std::max(timelineDuration, clip.durationSeconds);
            }
        } else if (activeAnimationClipIndex_ >= 0 &&
                   static_cast<std::size_t>(activeAnimationClipIndex_) < animationClips_.size()) {
            timelineDuration = animationClips_[activeAnimationClipIndex_].durationSeconds;
        }

        if (timelineDuration > 1e-5f) {
            animationTimeSeconds_ = std::fmod(animationTimeSeconds_ + std::max(0.0f, dtSeconds), timelineDuration);
        } else {
            animationTimeSeconds_ = 0.0f;
        }

        debugInfo_.enabled = true;
        debugInfo_.durationSeconds = timelineDuration;
        debugInfo_.timeSeconds = animationTimeSeconds_;
        debugInfo_.compositeMode = playAllAnimationClips_;

        auto applyClip = [&](const AnimationClip& clip, float localTime) {
            for (const AnimationTrack& track : clip.tracks) {
                if (track.nodeIndex < 0 || static_cast<std::size_t>(track.nodeIndex) >= localT.size()) {
                    continue;
                }
                if (track.times.empty()) {
                    continue;
                }

                if (track.path == AnimationPath::Rotation) {
                    if (track.quatValues.empty()) {
                        continue;
                    }
                    if (track.times.size() == 1 || track.quatValues.size() == 1) {
                        localR[track.nodeIndex] = glm::normalize(track.quatValues.front());
                        continue;
                    }

                    const std::size_t seg = findAnimationSegment(track.times, localTime);
                    const std::size_t next = std::min(seg + 1, track.quatValues.size() - 1);
                    if (track.stepInterpolation) {
                        localR[track.nodeIndex] = glm::normalize(track.quatValues[seg]);
                    } else {
                        const float alpha = normalizedSegmentT(track.times, seg, localTime);
                        localR[track.nodeIndex] = glm::normalize(glm::slerp(track.quatValues[seg], track.quatValues[next], alpha));
                    }
                    continue;
                }

                if (track.vec3Values.empty()) {
                    continue;
                }
                if (track.times.size() == 1 || track.vec3Values.size() == 1) {
                    if (track.path == AnimationPath::Translation) {
                        localT[track.nodeIndex] = track.vec3Values.front();
                    } else if (track.path == AnimationPath::Scale) {
                        localS[track.nodeIndex] = track.vec3Values.front();
                    }
                    continue;
                }

                const std::size_t seg = findAnimationSegment(track.times, localTime);
                const std::size_t next = std::min(seg + 1, track.vec3Values.size() - 1);
                glm::vec3 sampled = track.vec3Values[seg];
                if (!track.stepInterpolation) {
                    const float alpha = normalizedSegmentT(track.times, seg, localTime);
                    sampled = glm::mix(track.vec3Values[seg], track.vec3Values[next], alpha);
                }

                if (track.path == AnimationPath::Translation) {
                    localT[track.nodeIndex] = sampled;
                } else if (track.path == AnimationPath::Scale) {
                    localS[track.nodeIndex] = sampled;
                }
            }
        };

        const AnimationClip* debugClip = nullptr;
        float debugClipTime = animationTimeSeconds_;

        if (playAllAnimationClips_) {
            debugInfo_.clipName = "[COMPOSITE]";
            debugInfo_.selectedClipIndex = -1;
            debugInfo_.trackCount = 0;

            for (const AnimationClip& clip : animationClips_) {
                const float localTime = (clip.durationSeconds > 1e-5f)
                    ? std::fmod(animationTimeSeconds_, clip.durationSeconds)
                    : 0.0f;
                applyClip(clip, localTime);
                debugInfo_.trackCount += static_cast<int>(clip.tracks.size());
                debugInfo_.compositeAppliedClips += 1;

                if (!debugClip || clip.tracks.size() > debugClip->tracks.size()) {
                    debugClip = &clip;
                    debugClipTime = localTime;
                }
            }
        } else if (activeAnimationClipIndex_ >= 0 &&
                   static_cast<std::size_t>(activeAnimationClipIndex_) < animationClips_.size()) {
            const AnimationClip& clip = animationClips_[activeAnimationClipIndex_];
            debugInfo_.clipName = clip.name;
            debugInfo_.selectedClipIndex = activeAnimationClipIndex_;
            debugInfo_.trackCount = static_cast<int>(clip.tracks.size());
            debugInfo_.compositeAppliedClips = 1;

            const float localTime = (clip.durationSeconds > 1e-5f)
                ? std::fmod(animationTimeSeconds_, clip.durationSeconds)
                : 0.0f;
            applyClip(clip, localTime);
            debugClip = &clip;
            debugClipTime = localTime;
        }

        if (debugClip) {
            const AnimationTrack* debugTrack = nullptr;
            for (const AnimationTrack& track : debugClip->tracks) {
                if (track.times.size() > 1) {
                    debugTrack = &track;
                    break;
                }
            }
            if (!debugTrack && !debugClip->tracks.empty()) {
                debugTrack = &debugClip->tracks.front();
            }

            if (debugTrack && !debugTrack->times.empty()) {
                debugInfo_.keyCount = static_cast<int>(debugTrack->times.size());
                debugInfo_.stepInterpolation = debugTrack->stepInterpolation;

                if (debugTrack->times.size() == 1) {
                    debugInfo_.keyIndex = 0;
                    debugInfo_.nextKeyIndex = 0;
                    debugInfo_.keyTimeSeconds = debugTrack->times.front();
                    debugInfo_.nextKeyTimeSeconds = debugTrack->times.front();
                    debugInfo_.segmentAlpha = 0.0f;
                } else {
                    const std::size_t seg = findAnimationSegment(debugTrack->times, debugClipTime);
                    const std::size_t next = std::min(seg + 1, debugTrack->times.size() - 1);
                    debugInfo_.keyIndex = static_cast<int>(seg);
                    debugInfo_.nextKeyIndex = static_cast<int>(next);
                    debugInfo_.keyTimeSeconds = debugTrack->times[seg];
                    debugInfo_.nextKeyTimeSeconds = debugTrack->times[next];
                    debugInfo_.segmentAlpha = normalizedSegmentT(debugTrack->times, seg, debugClipTime);
                }
            }
        }
    }

    std::vector<glm::mat4> localMats(localT.size(), glm::mat4{1.0f});
    for (std::size_t i = 0; i < localT.size(); ++i) {
        localMats[i] = composeTRS(localT[i], localR[i], localS[i]);
    }

    std::function<void(std::size_t, const glm::mat4&)> propagate =
        [&](std::size_t nodeIndex, const glm::mat4& parentWorld) {
            if (nodeIndex >= nodeWorldTransforms_.size()) {
                return;
            }

            const glm::mat4 world = parentWorld * localMats[nodeIndex];
            nodeWorldTransforms_[nodeIndex] = world;

            for (std::size_t child = 0; child < nodeParents_.size(); ++child) {
                if (nodeParents_[child] == static_cast<int>(nodeIndex)) {
                    propagate(child, world);
                }
            }
        };

    if (!sceneRootNodes_.empty()) {
        for (std::size_t root : sceneRootNodes_) {
            propagate(root, glm::mat4{1.0f});
        }
    } else {
        for (std::size_t i = 0; i < nodeParents_.size(); ++i) {
            if (nodeParents_[i] < 0) {
                propagate(i, glm::mat4{1.0f});
            }
        }
    }

    for (Skin& skin : skins_) {
        const std::size_t jointCount = std::min(skin.jointNodes.size(), skin.inverseBindMatrices.size());
        if (skin.jointMatrices.size() != skin.jointNodes.size()) {
            skin.jointMatrices.assign(skin.jointNodes.size(), glm::mat4{1.0f});
        }

        for (std::size_t i = 0; i < jointCount; ++i) {
            const int jointNodeIndex = skin.jointNodes[i];
            if (jointNodeIndex < 0 || static_cast<std::size_t>(jointNodeIndex) >= nodeWorldTransforms_.size()) {
                skin.jointMatrices[i] = glm::mat4{1.0f};
                continue;
            }

            skin.jointMatrices[i] = nodeWorldTransforms_[jointNodeIndex] * skin.inverseBindMatrices[i];
        }
    }
}

glm::mat4 TemplateAnimator::resolveNodeTransform(int nodeIndex, const glm::mat4& fallback) const {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodeWorldTransforms_.size()) {
        return fallback;
    }
    return nodeWorldTransforms_[nodeIndex];
}

const std::vector<glm::mat4>* TemplateAnimator::skinJointMatricesForSkin(int skinIndex) const {
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= skins_.size()) {
        return nullptr;
    }
    return &skins_[skinIndex].jointMatrices;
}
