#include "scenes/MainMenuScene.hpp"

#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <imgui.h>

MainMenuScene::~MainMenuScene() = default;

SceneFrameResult MainMenuScene::render(SceneSharedState& state) {
    SceneFrameResult result;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 menuSize{420.0f, 320.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - menuSize.x) * 0.5f, (displaySize.y - menuSize.y) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(menuSize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags menuFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    ImGui::Begin("MainMenu", nullptr, menuFlags);

    if (state.headingFont != nullptr) {
        ImGui::PushFont(state.headingFont);
    }
    ImGui::TextUnformatted("NodeSpireTD");
    if (state.headingFont != nullptr) {
        ImGui::PopFont();
    }

    ImGui::TextUnformatted("Deploy. Defend. Adapt.");
    ImGui::Separator();

    if (ImGui::Button("Play", ImVec2(-1.0f, 42.0f))) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::LevelSelection;
        result.transitionMessage = "Loading level selection...";
        result.transitionMinDurationSeconds = 0.0f;
    }

    if (ImGui::Button("Options", ImVec2(-1.0f, 42.0f))) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::Options;
        result.transitionMessage = "Loading options...";
        result.transitionMinDurationSeconds = 0.0f;
    }

    if (ImGui::Button("Quit", ImVec2(-1.0f, 42.0f))) {
        result.requestQuit = true;
    }

    ImGui::End();
    return result;
}