#include "ui/UIManager.h"

#include <imgui.h>
#include <imnodes.h>

#include "Application.h"

namespace NST {

UIManager::UIManager(Application& app) : m_app(app) {}

// ---------------------------------------------------------------
void UIManager::render(int width, int height) {
    switch (m_state) {
    case MenuState::MainMenu: renderMainMenu(width, height); break;
    case MenuState::Gameplay: renderGameplay(width, height); break;
    case MenuState::Settings: renderSettings(width, height); break;
    case MenuState::Exit:     break; // handled by Application
    }
}

// ---------------------------------------------------------------
// renderMainMenu
//
// Dark-slate, left-aligned asymmetric panel occupying roughly the
// left third of the screen.  Clean, responsive layout.
// ---------------------------------------------------------------
void UIManager::renderMainMenu(int width, int height) {
    const float panelW = static_cast<float>(width)  * 0.32f;
    const float panelH = static_cast<float>(height) * 0.72f;
    const float panelX = static_cast<float>(width)  * 0.06f;
    const float panelY = (static_cast<float>(height) - panelH) * 0.5f;

    ImGui::SetNextWindowPos({panelX, panelY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelW, panelH}, ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0f, 24.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {0.0f,  16.0f});

    if (ImGui::Begin("##MainMenu", nullptr, flags)) {
        // --- Title ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.72f, 1.00f, 1.00f));
        ImGui::SetWindowFontScale(1.9f);
        ImGui::TextUnformatted("NODESPIRE TD");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.65f, 0.70f, 1.00f));
        ImGui::TextUnformatted("Tower Defense · Talent Mastery");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();

        // --- Buttons ---
        const float btnW = panelW - 48.0f;
        const float btnH = 44.0f;

        if (ImGui::Button("Play", {btnW, btnH}))
            m_state = MenuState::Gameplay;

        if (ImGui::Button("Settings", {btnW, btnH}))
            m_state = MenuState::Settings;

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.45f, 0.18f, 0.18f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.60f, 0.24f, 0.24f, 1.00f));
        if (ImGui::Button("Exit", {btnW, btnH}))
            m_state = MenuState::Exit;
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------
// renderGameplay
//
// Displays an ImNodes-backed talent tree editor and a live stat
// inspector for the selected tower.
// ---------------------------------------------------------------
void UIManager::renderGameplay(int width, int height) {
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    // --- Talent Tree panel (right side) ---
    ImGui::SetNextWindowPos({fw * 0.55f, 20.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw * 0.43f, fh - 40.0f}, ImGuiCond_Always);

    ImGuiWindowFlags panelFlags =
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Talent Tree", nullptr, panelFlags)) {
        ImNodes::BeginNodeEditor();

        // Example node: Rapid Fire
        ImNodes::BeginNode(1);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Rapid Fire");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginOutputAttribute(10);
        ImGui::Text("ATK SPD +0.1");
        ImNodes::EndOutputAttribute();
        ImNodes::EndNode();

        // Example node: Eagle Eye
        ImNodes::BeginNode(2);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Eagle Eye");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(20);
        ImGui::Text("Range +1.0");
        ImNodes::EndInputAttribute();
        ImNodes::EndNode();

        // Example edge connecting the two nodes
        ImNodes::Link(100, 10, 20);

        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
    }
    ImGui::End();

    // --- Tower stats inspector (left side) ---
    ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw * 0.30f, 260.0f}, ImGuiCond_Always);

    if (ImGui::Begin("Tower Stats", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextDisabled("Selected: Archer Tower #1");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("ATK Speed : %.2f / s", 1.0f);
        ImGui::Text("Damage    : %.1f",     10.0f);
        ImGui::Text("Range     : %.1f",      5.0f);
        ImGui::Text("AoE       : %.1f",      0.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Respec", {120.0f, 32.0f})) { /* TODO */ }
        ImGui::SameLine();
        if (ImGui::Button("Back to Menu", {140.0f, 32.0f}))
            m_state = MenuState::MainMenu;
    }
    ImGui::End();
}

// ---------------------------------------------------------------
// renderSettings
// ---------------------------------------------------------------
void UIManager::renderSettings(int width, int height) {
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    ImGui::SetNextWindowPos({fw * 0.20f, fh * 0.15f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw * 0.60f, fh * 0.70f}, ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Settings", nullptr, flags)) {
        ImGui::SeparatorText("Display");

        static bool vsync = true;
        ImGui::Checkbox("VSync", &vsync);

        ImGui::Spacing();
        ImGui::SeparatorText("Audio");

        static float masterVolume = 0.8f;
        ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Back", {100.0f, 32.0f}))
            m_state = MenuState::MainMenu;
    }
    ImGui::End();
}

} // namespace NST
