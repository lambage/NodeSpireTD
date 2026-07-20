#include "scenes/PlayLevelCameraController.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace {

// Forward direction from yaw + pitch (Y-up, right-handed)
glm::vec3 cameraForward(float yaw, float pitch) {
    return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)));
}

} // namespace

void PlayLevelCameraController::reset() {
    camPos_ = {0.0f, 5.0f, 20.0f};
    camYaw_ = 3.14159f;
    camPitch_ = -0.25f;
}

void PlayLevelCameraController::update(float dt) {
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        camYaw_ -= io.MouseDelta.x * 0.003f;
        camPitch_ -= io.MouseDelta.y * 0.003f;
        camPitch_ = glm::clamp(camPitch_, -1.48f, 1.48f);
    }

    const float baseSpeed = 8.0f;
    const float speed = baseSpeed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f) * dt;

    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (ImGui::IsKeyDown(ImGuiKey_W)) {
        camPos_ += fwd * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
        camPos_ -= fwd * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
        camPos_ -= right * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
        camPos_ += right * speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_Space)) {
        camPos_.y += speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_Q)) {
        camPos_.y -= speed;
    }
}

glm::mat4 PlayLevelCameraController::buildViewMatrix() const {
    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    return glm::lookAt(camPos_, camPos_ + fwd, glm::vec3(0.0f, 1.0f, 0.0f));
}

bool PlayLevelCameraController::raycastGroundAtCursor(glm::vec3& outHit) const {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;
    if (displaySize.x <= 1.0f || displaySize.y <= 1.0f) {
        return false;
    }

    const ImVec2 mousePos = io.MousePos;
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return false;
    }

    const float ndcX = (2.0f * mousePos.x) / displaySize.x - 1.0f;
    const float ndcY = 1.0f - (2.0f * mousePos.y) / displaySize.y;
    const float aspect = displaySize.y > 0.0f ? (displaySize.x / displaySize.y) : 1.0f;

    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    constexpr float kFovYRadians = glm::radians(60.0f);
    const float tanHalfFovY = std::tan(kFovYRadians * 0.5f);

    glm::vec3 rayDir = fwd + right * (ndcX * aspect * tanHalfFovY) + up * (ndcY * tanHalfFovY);
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 <= 1e-8f) {
        return false;
    }
    rayDir = glm::normalize(rayDir);

    if (std::abs(rayDir.y) <= 1e-5f) {
        return false;
    }

    const float t = -camPos_.y / rayDir.y;
    if (t <= 0.0f) {
        return false;
    }

    outHit = camPos_ + rayDir * t;
    return std::isfinite(outHit.x) && std::isfinite(outHit.y) && std::isfinite(outHit.z);
}

const glm::vec3& PlayLevelCameraController::position() const {
    return camPos_;
}

float PlayLevelCameraController::yaw() const {
    return camYaw_;
}

float PlayLevelCameraController::pitch() const {
    return camPitch_;
}
