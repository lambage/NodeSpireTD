#include "scenes/OptionsScene.hpp"

#include "AppSettings.hpp"
#include "scenes/SceneSharedState.hpp"

#include <algorithm>
#include <imgui.h>

OptionsScene::~OptionsScene() = default;

SceneFrameResult OptionsScene::render(SceneSharedState& state) {
    SceneFrameResult result;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 optionsSize{760.0f, 460.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - optionsSize.x) * 0.5f, (displaySize.y - optionsSize.y) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(optionsSize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags optionsFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Options", nullptr, optionsFlags);

    if (state.headingFont != nullptr) {
        ImGui::PushFont(state.headingFont);
    }
    ImGui::TextUnformatted("Settings");
    if (state.headingFont != nullptr) {
        ImGui::PopFont();
    }

    ImGui::Separator();
    ImGui::Checkbox("Fullscreen", &state.settings.fullscreen);
    ImGui::Checkbox("V-Sync", &state.settings.vSyncEnabled);

    if (!state.displayModes.empty()) {
        const int clampedIndex =
            std::clamp(state.selectedDisplayModeIndex, 0, static_cast<int>(state.displayModes.size() - 1));
        const std::string preview = modeLabel(state.displayModes[clampedIndex]);
        if (ImGui::BeginCombo("Display Mode", preview.c_str())) {
            for (int i = 0; i < static_cast<int>(state.displayModes.size()); ++i) {
                const bool selected = (state.selectedDisplayModeIndex == i);
                const std::string label = modeLabel(state.displayModes[i]);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    state.selectedDisplayModeIndex = i;
                    state.settings.displayWidth = state.displayModes[i].width;
                    state.settings.displayHeight = state.displayModes[i].height;
                    state.settings.refreshRate = state.displayModes[i].refreshRate;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SliderInt("Graphics Quality", &state.settings.graphicsQuality, 0, 3);
    ImGui::SliderFloat("Master Volume", &state.settings.masterVolume, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Music Volume", &state.settings.musicVolume, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("SFX Volume", &state.settings.sfxVolume, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Mute when unfocused", &state.settings.muteWhenUnfocused);

    if (state.displayConfirmationActive) {
        ImGui::OpenPopup("Confirm Display Changes");
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 170.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Confirm Display Changes", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Keep these display settings? They will be reverted "
                           "automatically if not confirmed.");
        ImGui::Spacing();
        ImGui::Text("Reverting in %.1f seconds", state.displayConfirmationSecondsRemaining);
        ImGui::Separator();

        if (ImGui::Button("Accept Changes", ImVec2(170.0f, 0.0f))) {
            result.requestAcceptDisplayChanges = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(120.0f, 0.0f))) {
            result.requestRevertDisplayChanges = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::Button("Apply", ImVec2(140.0f, 40.0f))) {
        result.requestApplySettings = true;
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