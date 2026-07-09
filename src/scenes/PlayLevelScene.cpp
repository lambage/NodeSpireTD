#include "scenes/PlayLevelScene.hpp"

#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <imgui.h>

PlayLevelScene::~PlayLevelScene() = default;

void PlayLevelScene::onEnter(SceneSharedState&) {
    remainingSeconds_ = kSimulationDurationSeconds;
}

SceneFrameResult PlayLevelScene::render(SceneSharedState& state) {
    remainingSeconds_ -= ImGui::GetIO().DeltaTime;

    SceneFrameResult result;
    if (remainingSeconds_ <= 0.0f) {
        remainingSeconds_ = kSimulationDurationSeconds;
        result.requestTransition = true;
        result.transitionTarget = SceneId::MainMenu;
        result.transitionMessage = "Returning to main menu...";
        result.transitionMinDurationSeconds = 0.4f;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 simulationSize{560.0f, 280.0f};
    ImGui::SetNextWindowPos(
        ImVec2((displaySize.x - simulationSize.x) * 0.5f, (displaySize.y - simulationSize.y) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(simulationSize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags simulationFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Simulation", nullptr, simulationFlags);
    ImGui::Text("Running level: %s", state.activeLevelName.c_str());
    ImGui::Separator();
    ImGui::Text("Returning to menu in %.1f seconds", std::max(0.0f, remainingSeconds_));
    ImGui::ProgressBar(1.0f - (std::max(0.0f, remainingSeconds_) / kSimulationDurationSeconds), ImVec2(-1.0f, 0.0f));
    ImGui::End();

    return result;
}
