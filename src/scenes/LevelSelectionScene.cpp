#include "scenes/LevelSelectionScene.hpp"

#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <lua.hpp>

namespace {

constexpr const char* kLevelDefinitionFilename = "level.lua";

std::string prettifyDirName(const std::string& dirName) {
    std::string result = dirName;
    bool capitalizeNext = true;
    for (char& c : result) {
        if (c == '_' || c == '-') {
            c = ' ';
            capitalizeNext = true;
        } else if (capitalizeNext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalizeNext = false;
        }
    }
    return result;
}

bool loadLevelEntryFromScript(const std::filesystem::path& levelDir,
                              const std::filesystem::path& scriptPath,
                              std::string& outDisplayName,
                              std::filesystem::path& outMapAssetPath) {
    lua_State* L = luaL_newstate();
    if (!L) {
        return false;
    }

    const std::string scriptPathString = scriptPath.string();
    if (luaL_loadfile(L, scriptPathString.c_str()) != LUA_OK) {
        lua_close(L);
        return false;
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_close(L);
        return false;
    }

    if (!lua_istable(L, -1)) {
        lua_close(L);
        return false;
    }

    std::filesystem::path mapAssetPath;
    std::string displayName;

    lua_getfield(L, -1, "mapAssetPath");
    if (lua_isstring(L, -1)) {
        mapAssetPath = std::filesystem::path(lua_tostring(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "displayName");
    if (lua_isstring(L, -1)) {
        displayName = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_close(L);

    if (mapAssetPath.empty()) {
        return false;
    }

    if (!std::filesystem::is_regular_file(mapAssetPath)) {
        return false;
    }

    outDisplayName = displayName.empty() ? prettifyDirName(levelDir.filename().string()) : displayName;
    outMapAssetPath = mapAssetPath;
    return true;
}

} // namespace

LevelSelectionScene::~LevelSelectionScene() = default;

void LevelSelectionScene::onEnter(SceneSharedState&) {
    availableLevels_.clear();
    const std::filesystem::path levelsDir = "assets/levels";
    if (std::filesystem::is_directory(levelsDir)) {
        std::vector<std::filesystem::path> sortedDirs;
        for (const auto& entry : std::filesystem::directory_iterator(levelsDir)) {
            if (entry.is_directory()) {
                sortedDirs.push_back(entry.path());
            }
        }
        std::sort(sortedDirs.begin(), sortedDirs.end());
        for (const auto& dir : sortedDirs) {
            const std::filesystem::path scriptPath = dir / kLevelDefinitionFilename;
            if (!std::filesystem::is_regular_file(scriptPath)) {
                continue;
            }

            std::string displayName;
            std::filesystem::path mapAssetPath;
            if (loadLevelEntryFromScript(dir, scriptPath, displayName, mapAssetPath)) {
                availableLevels_.push_back({displayName, mapAssetPath, scriptPath});
            }
        }
    }
    if (!availableLevels_.empty()) {
        selectedLevelIndex_ = std::clamp(selectedLevelIndex_, 0, static_cast<int>(availableLevels_.size() - 1));
    } else {
        selectedLevelIndex_ = 0;
    }
}

SceneFrameResult LevelSelectionScene::render(SceneSharedState& state, float /*dt*/) {
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
        if (ImGui::Selectable(availableLevels_[i].name.c_str(), selected)) {
            selectedLevelIndex_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("MissionDetails", ImVec2(0.0f, -56.0f), true, ImGuiWindowFlags_NoScrollbar);
    const bool hasLevels = !availableLevels_.empty();
    if (hasLevels) {
        state.activeLevelName = availableLevels_[selectedLevelIndex_].name;
        state.activeLevelAssetPath = availableLevels_[selectedLevelIndex_].assetPath.string();
        state.activeLevelScriptPath = availableLevels_[selectedLevelIndex_].scriptPath.string();
        ImGui::Text("Selected mission: %s", state.activeLevelName.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Scan complete. Terrain analytics, choke points, and enemy wave "
                           "patterns are ready for deployment simulation.");
        ImGui::Spacing();
        ImGui::TextWrapped("Asset source: %s", state.activeLevelAssetPath.c_str());
    } else {
        ImGui::TextUnformatted("No valid levels found.");
        ImGui::Spacing();
        ImGui::TextWrapped("Each folder in assets/levels must provide a valid level.lua with mapAssetPath.");
    }
    ImGui::EndChild();

    if (!hasLevels) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Load Level", ImVec2(170.0f, 40.0f))) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::PlayLevel;
        result.transitionMessage = "Loading level: " + state.activeLevelName + "...";
        result.transitionMinDurationSeconds = 0.0f;
    }
    if (!hasLevels) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(140.0f, 40.0f))) {
        result.requestTransition = true;
        result.transitionTarget = SceneId::MainMenu;
        result.transitionMessage = "Returning to main menu...";
        result.transitionMinDurationSeconds = 0.0f;
    }

    ImGui::End();
    return result;
}
