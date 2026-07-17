#include "scenes/PlayLevelScene.hpp"

#include "VulkanContext.hpp"
#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"
#include "utility/WorldRenderer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <cmath>
#include <filesystem>

PlayLevelScene::PlayLevelScene(lua_State* L) : GameScene(L) {}
PlayLevelScene::~PlayLevelScene() = default;

// ─── camera helpers ───────────────────────────────────────────────────────────

// Forward direction from yaw + pitch (Y-up, right-handed)
static glm::vec3 cameraForward(float yaw, float pitch) {
    return glm::normalize(glm::vec3(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw)));
}

glm::mat4 PlayLevelScene::buildViewMatrix() const {
    const glm::vec3 fwd = cameraForward(camYaw_, camPitch_);
    return glm::lookAt(camPos_, camPos_ + fwd, glm::vec3(0.0f, 1.0f, 0.0f));
}

void PlayLevelScene::updateCamera(float dt) {
    const ImGuiIO& io = ImGui::GetIO();

    // ── Mouse look (hold RMB) ─────────────────────────────────────────────
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        camYaw_   -= io.MouseDelta.x * 0.003f;
        camPitch_ -= io.MouseDelta.y * 0.003f;
        camPitch_  = glm::clamp(camPitch_, -1.48f, 1.48f); // ±~85°
    }

    // ── WASD + Space/Shift movement ───────────────────────────────────────
    // Speed: hold Shift to sprint (×4)
    const float baseSpeed = 8.0f;
    const float speed = baseSpeed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f) * dt;

    const glm::vec3 fwd   = cameraForward(camYaw_, camPitch_);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));

    if (ImGui::IsKeyDown(ImGuiKey_W))     camPos_ += fwd   * speed;
    if (ImGui::IsKeyDown(ImGuiKey_S))     camPos_ -= fwd   * speed;
    if (ImGui::IsKeyDown(ImGuiKey_A))     camPos_ -= right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_D))     camPos_ += right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_Space)) camPos_.y += speed;
    if (ImGui::IsKeyDown(ImGuiKey_Q))     camPos_.y -= speed;
}

// ─── scene lifecycle ──────────────────────────────────────────────────────────

void PlayLevelScene::onEnter(SceneSharedState& state) {
    // Reset camera
    camPos_   = {0.0f, 5.0f, 20.0f};
    camYaw_   = 3.14159f;
    camPitch_ = -0.25f;

    worldRenderer_.reset();
    loadStatus_.clear();

    std::filesystem::path assetPath = state.activeLevelAssetPath;
    if (std::filesystem::is_directory(assetPath)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
            const auto ext = entry.path().extension().string();
            if (ext == ".glb" || ext == ".gltf") {
                assetPath = entry.path();
                break;
            }
        }
    }

    if (!state.vulkanContext) {
        loadStatus_ = "No Vulkan context available.";
        return;
    }

    worldRenderer_ = std::make_unique<WorldRenderer>(L_, *state.vulkanContext);
    worldRenderer_->beginLoad(assetPath); // non-blocking
}

void PlayLevelScene::onExit(SceneSharedState& state) {
    if (state.vulkanContext) {
        state.vulkanContext->waitIdle();
    }
    worldRenderer_.reset();
}

void PlayLevelScene::renderWorld(VkCommandBuffer cmd, VkExtent2D extent) {
    if (worldRenderer_ && worldRenderer_->isLoaded()) {
        worldRenderer_->render(cmd, extent, buildViewMatrix());
    }
}

// ─── per-frame ────────────────────────────────────────────────────────────────

SceneFrameResult PlayLevelScene::render(SceneSharedState& state, float dt) {
    SceneFrameResult result;

    // Tick GPU uploads (one step per frame while loading)
    if (worldRenderer_ && !worldRenderer_->isLoaded() && !worldRenderer_->loadFailed()) {
        worldRenderer_->tickLoad();
    }

    const bool isLoaded  = worldRenderer_ && worldRenderer_->isLoaded();
    const bool isFailed  = !worldRenderer_ || worldRenderer_->loadFailed();
    const bool isLoading = worldRenderer_ && !isLoaded && !isFailed;

    if (isLoaded) {
        updateCamera(dt);
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // ── Loading screen (centred, replaces world HUD) ──────────────────────
    if (isLoading) {
        const float      progress = worldRenderer_->loadProgress();
        const std::string act     = worldRenderer_->loadActivity();

        constexpr float panelW = 600.0f;
        constexpr float panelH = 160.0f;
        ImGui::SetNextWindowPos(ImVec2((displaySize.x - panelW) * 0.5f,
                                       (displaySize.y - panelH) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);

        constexpr ImGuiWindowFlags loadFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("WorldLoading", nullptr, loadFlags);

        ImGui::Text("Loading  %s", state.activeLevelName.c_str());
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted(act.c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.13f, 0.52f, 0.70f, 1.0f));
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 18.0f), "");
        ImGui::PopStyleColor();
        ImGui::Text("  %.0f%%", progress * 100.0f);

        ImGui::End();
        return result;
    }

    // ── Compact bottom-left HUD (world loaded or failed) ──────────────────
    constexpr float hudW = 460.0f;
    constexpr float hudH = 200.0f;
    ImGui::SetNextWindowPos(ImVec2(16.0f, displaySize.y - hudH - 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(hudW, hudH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.70f);

    constexpr ImGuiWindowFlags hudFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("WorldHUD", nullptr, hudFlags);

    ImGui::Text("%s", state.activeLevelName.c_str());
    ImGui::Separator();

    if (isLoaded) {
        ImGui::Text("Meshes %d  |  Verts %d  |  Tris %d",
                    worldRenderer_->meshCount(),
                    worldRenderer_->totalVertices(),
                    worldRenderer_->totalIndices() / 3);
        ImGui::Text("Pos (%.1f, %.1f, %.1f)", camPos_.x, camPos_.y, camPos_.z);
        ImGui::TextUnformatted("RMB+drag: look   WASD: fly   Space/Q: up/down   Shift: sprint");
    } else {
        ImGui::TextUnformatted("Status: LOAD FAILED");
        const std::string reason = worldRenderer_ ? worldRenderer_->statusMessage() : loadStatus_;
        ImGui::TextWrapped("%s", reason.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Exit to Mission Select", ImVec2(-1.0f, 30.0f))) {
        result.requestTransition = true;
        result.transitionTarget  = SceneId::LevelSelection;
        result.transitionMessage = "Returning to mission select...";
        result.transitionMinDurationSeconds = 0.0f;
    }

    ImGui::End();
    return result;
}
