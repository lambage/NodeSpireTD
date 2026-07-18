#pragma once

#include <string>

struct TemplateAnimationDebugInfo {
    bool enabled = false;
    std::string clipName;
    int selectedClipIndex = -1;
    int clipCount = 0;
    bool compositeMode = false;
    int compositeAppliedClips = 0;
    float timeSeconds = 0.0f;
    float durationSeconds = 0.0f;
    int trackCount = 0;
    int keyCount = 0;
    int keyIndex = 0;
    int nextKeyIndex = 0;
    float keyTimeSeconds = 0.0f;
    float nextKeyTimeSeconds = 0.0f;
    float segmentAlpha = 0.0f;
    bool stepInterpolation = false;
};

using EnemyAnimationDebugInfo = TemplateAnimationDebugInfo;
