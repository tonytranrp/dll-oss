#pragma once

#include <algorithm>
#include <cmath>

#include "Utils/Utils.hpp"

namespace ForcedRenderRotationState {
inline uint64_t lastUpdateMs = 0;
inline uint64_t lastStepMs = 0;
inline bool hasForcedRotation = false;
inline constexpr uint64_t timeoutMs = 260;
inline constexpr float lerpSpeed = 12.0f;
inline constexpr float bodyTurnThreshold = 40.0f;

inline Vec2<float> currentRot{0.0f, 0.0f};
inline float currentHeadYaw = 0.0f;
inline float currentBodyYaw = 0.0f;

inline Vec2<float> targetRot{0.0f, 0.0f};
inline float targetHeadYaw = 0.0f;
inline float targetBodyYaw = 0.0f;

inline float wrapAngleTo180(float angle) {
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

inline void updateBodyTarget() {
    const float headToBodyDiff = wrapAngleTo180(targetHeadYaw - currentBodyYaw);
    if (std::fabs(headToBodyDiff) > bodyTurnThreshold) {
        targetBodyYaw = currentBodyYaw + headToBodyDiff;
    }
}

inline void set(const Vec2<float>& rot, float headYaw) {
    const uint64_t now = Utils::getCurrentMs();
    if (!hasForcedRotation) {
        currentRot = rot;
        currentHeadYaw = headYaw;
        currentBodyYaw = headYaw;

        targetRot = rot;
        targetHeadYaw = headYaw;
        targetBodyYaw = headYaw;
    }

    targetRot.x = std::clamp(rot.x, -90.0f, 90.0f);
    targetRot.y = currentRot.y + wrapAngleTo180(rot.y - currentRot.y);
    targetHeadYaw = currentHeadYaw + wrapAngleTo180(headYaw - currentHeadYaw);
    updateBodyTarget();

    lastUpdateMs = now;
    if (lastStepMs == 0) {
        lastStepMs = now;
    }
    hasForcedRotation = true;
}

inline bool step() {
    if (!hasForcedRotation) return false;

    const uint64_t now = Utils::getCurrentMs();
    if (now - lastUpdateMs > timeoutMs) {
        hasForcedRotation = false;
        return false;
    }

    const float dt = std::clamp(static_cast<float>(now - lastStepMs) / 1000.0f, 0.0f, 0.050f);
    lastStepMs = now;
    const float factor = std::clamp(dt * lerpSpeed, 0.0f, 1.0f);

    updateBodyTarget();

    currentRot.x += (targetRot.x - currentRot.x) * factor;
    currentRot.y += wrapAngleTo180(targetRot.y - currentRot.y) * factor;
    currentHeadYaw += wrapAngleTo180(targetHeadYaw - currentHeadYaw) * factor;
    currentBodyYaw += wrapAngleTo180(targetBodyYaw - currentBodyYaw) * factor;

    currentRot.x = std::clamp(currentRot.x, -90.0f, 90.0f);
    currentRot.y = wrapAngleTo180(currentRot.y);
    currentHeadYaw = wrapAngleTo180(currentHeadYaw);
    currentBodyYaw = wrapAngleTo180(currentBodyYaw);

    return true;
}

inline bool tryGet(Vec2<float>& outRot, float& outHeadYaw, float& outBodyYaw) {
    if (!step()) return false;

    outRot = currentRot;
    outHeadYaw = currentHeadYaw;
    outBodyYaw = currentBodyYaw;
    return true;
}

inline bool tryGet(Vec2<float>& outRot, float& outHeadYaw) {
    float bodyYaw = 0.0f;
    if (!tryGet(outRot, outHeadYaw, bodyYaw)) return false;
    return true;
}

inline void clear() {
    hasForcedRotation = false;
    lastUpdateMs = 0;
    lastStepMs = 0;
}
} // namespace ForcedRenderRotationState
