#include "scenes/LevelSelectionScene.hpp"

#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <algorithm>
#include <imgui.h>

LevelSelectionScene::~LevelSelectionScene() = default;

void LevelSelectionScene::onEnter(SceneSharedState&) {
    selectedLevelIndex_ = std::clamp(selectedLevelIndex_, 0, static_cast<int>(availableLevels_.size() - 1));
}

SceneFrameResult LevelSelectionScene::render(SceneSharedState& state) {
    SceneFrameResult result;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 windowSize{820.0f, 480.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags levelSelectFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("LevelSelection", nullptr, levelSelectFlags);

    if (state.headingFont != nullptr) {
        ImGui::PushFont(state.headingFont);
    }
    ImGui::TextUnformatted("Mission Control");
    if (state.headingFont != nullptr) {
        ImGui::PopFont();
    }

    ImGui::TextUnformatted("Select a mission profile");
    ImGui::Separator();

    constexpr float missionListWidth = 260.0f;
    ImGui::BeginChild("MissionList", ImVec2(missionListWidth, -56.0f), true, ImGuiWindowFlags_NoScrollbar);
    for (int i = 0; i < static_cast<int>(availableLevels_.size()); ++i) {
        const bool selected = (selectedLevelIndex_ == i);
        if (ImGui::Selectable(availableLevels_[i].c_str(), selected)) {
            selectedLevelIndex_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("MissionDetails", ImVec2(0.0f, -56.0f), true, ImGuiWindowFlags_NoScrollbar);
    state.activeLevelName = availableLevels_[selectedLevelIndex_];
    ImGui::Text("Selected mission: %s", state.activeLevelName.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Scan complete. Terrain analytics, choke points, and enemy wave "
                       "patterns are ready for deployment simulation.");
    ImGui::EndChild();

    if (ImGui::Button("Load Level", ImVec2(170.0f, 40.0f))) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::PlayLevel;
        result.transitionMessage = "Loading level: " + state.activeLevelName + "...";
        result.transitionMinDurationSeconds = 1.2f;
    }

    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(140.0f, 40.0f))) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::MainMenu;
        result.transitionMessage = "Returning to main menu...";
        result.transitionMinDurationSeconds = 0.2f;
    }

    ImGui::End();
    return result;
}
