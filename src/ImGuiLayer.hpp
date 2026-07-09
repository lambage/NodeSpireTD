#pragma once

#include "AppSettings.hpp"
#include "utility/VulkanTexture.hpp"

#include <SFML/Window.hpp>
#include <imgui.h>
#include <volk.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanContext;

class ImGuiLayer {
public:
    struct DisplayModeOption {
        int width = 1280;
        int height = 720;
        int refreshRate = 60;
    };

    ImGuiLayer();
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void initializeVulkanBackend(const VulkanContext& context);

    struct EventResult {
        bool resized = false;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    EventResult processEvent(const sf::Event& event);

    void setDeltaTime(float deltaSeconds);
    void setDisplaySize(uint32_t width, uint32_t height);
    void setSettings(const AppSettings& settings);
    const AppSettings& settings() const;
    void setLoadingComplete();
    void openOptionsScene();
    void setDisplayConfirmationState(bool active, float secondsRemaining);

    struct RenderResult {
        bool requestQuit = false;
        bool requestApplySettings = false;
        bool requestAcceptDisplayChanges = false;
        bool requestRevertDisplayChanges = false;
    };

    void beginFrame();
    RenderResult renderSceneUi();
    void renderDrawData(VkCommandBuffer commandBuffer);

private:
    struct SceneNode {
        std::string id;
        std::function<void()> onEnter;
        std::function<void()> onExit;
        std::function<RenderResult()> onRender;
    };

    struct PendingSceneTransition {
        bool active = false;
        std::string targetSceneId;
        std::string loadingMessage;
        float elapsedSeconds = 0.0f;
        float minDurationSeconds = 0.6f;
    };

    static ImGuiKey translateSfmlKeyToImGui(sf::Keyboard::Key key);

    void applyModernStyle();
    void loadUiFonts();

    void refreshDisplayModeOptions();
    int findDisplayModeIndexForSettings(const AppSettings& settings) const;
    std::string modeLabel(const DisplayModeOption& mode) const;
    void buildSceneGraph();
    void queueSceneTransition(const std::string& targetSceneId, const std::string& loadingMessage, float minDurationSeconds = 0.6f);
    void updateSceneTransition();
    void renderLoadingScene();
    RenderResult renderCurrentScene();
    void renderSplashScene();
    RenderResult renderMainMenuScene();
    RenderResult renderOptionsScene();
    RenderResult renderLevelSelectionScene();
    void renderSimulationScene();

    std::unordered_map<std::string, SceneNode> sceneGraph_;
    std::string currentSceneId_;
    PendingSceneTransition pendingTransition_;

    AppSettings settings_;
    std::optional<VulkanTexture> splashTexture_;
    ImFont* headingFont_ = nullptr;

    bool loadingComplete_ = false;
    float splashElapsedSeconds_ = 0.0f;
    float splashMinimumSeconds_ = 1.5f;
    float simulationRemainingSeconds_ = 5.0f;
    std::vector<std::string> availableLevels_ = {
        "Forest Outskirts",
        "Iron Ridge",
        "Delta Relay"
    };
    int selectedLevelIndex_ = 0;
    std::string activeLevelName_ = "Forest Outskirts";

    std::vector<DisplayModeOption> displayModes_;
    int selectedDisplayModeIndex_ = 0;

    bool displayConfirmationActive_ = false;
    float displayConfirmationSecondsRemaining_ = 0.0f;
};
