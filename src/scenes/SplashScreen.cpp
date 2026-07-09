#include "scenes/SplashScreen.hpp"

#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <imgui.h>

SplashScene::~SplashScene() = default;

void SplashScene::onEnter(SceneSharedState&) {
    elapsedSeconds_ = 0.0f;
}

SceneFrameResult SplashScene::render(SceneSharedState& state) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags splashWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                   ImGuiWindowFlags_NoSavedSettings |
                                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("SplashScreen", nullptr, splashWindowFlags);

    if (state.hasSplashTexture && state.splashTextureWidth > 0 && state.splashTextureHeight > 0) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float scale = std::min(available.x / static_cast<float>(state.splashTextureWidth),
                                     available.y / static_cast<float>(state.splashTextureHeight));
        const ImVec2 imageSize{static_cast<float>(state.splashTextureWidth) * scale,
                               static_cast<float>(state.splashTextureHeight) * scale};

        const float cursorX = std::max(0.0f, (available.x - imageSize.x) * 0.5f);
        const float cursorY = std::max(0.0f, (available.y - imageSize.y) * 0.45f);
        ImGui::SetCursorPos(ImVec2(cursorX, cursorY));
        ImGui::Image(state.splashTextureRef, imageSize);
    }

    // Draw title after the splash image so it is always visible in the
    // foreground.
    if (state.titleFont != nullptr) {
        ImGui::PushFont(state.titleFont);
    }
    const char* titleText = "NodeSpireTD";
    const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    const float titleX = std::max(0.0f, (ImGui::GetWindowWidth() - titleSize.x) * 0.5f);
    ImGui::SetCursorPos(ImVec2(titleX, 28.0f));
    ImGui::TextUnformatted(titleText);
    if (state.titleFont != nullptr) {
        ImGui::PopFont();
    }

    const char* statusText = "Initializing systems...";
    const ImVec2 statusSize = ImGui::CalcTextSize(statusText);
    const float statusX = std::max(0.0f, (ImGui::GetWindowWidth() - statusSize.x) * 0.5f);
    const float statusY = std::max(0.0f, ImGui::GetWindowHeight() - 46.0f);
    ImGui::SetCursorPos(ImVec2(statusX, statusY));
    ImGui::TextUnformatted(statusText);

    ImGui::End();

    elapsedSeconds_ += ImGui::GetIO().DeltaTime;

    SceneFrameResult result;
    if (state.loadingComplete && elapsedSeconds_ >= kMinimumSplashSeconds) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::MainMenu;
        result.transitionMessage = "Loading main menu...";
        result.transitionMinDurationSeconds = 0.3f;
    }

    return result;
}