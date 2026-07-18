#include "AppController.hpp"

#include "ImGuiLayer.hpp"
#include "PlatformRuntime.hpp"
#include "Scenes.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"
#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <SFML/Window.hpp>
#include <algorithm>
#include <imgui.h>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

bool useExclusiveFullscreen(const AppSettings& settings) {
    return settings.fullscreen && settings.exclusiveFullscreen;
}

sf::VideoMode toVideoMode(const AppSettings& settings) {
    if (settings.fullscreen && !settings.exclusiveFullscreen) {
        const sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
        return sf::VideoMode(desktopMode.size);
    }

    return sf::VideoMode(
        {static_cast<unsigned int>(settings.displayWidth), static_cast<unsigned int>(settings.displayHeight)});
}

sf::State toWindowState(const AppSettings& settings) {
    return useExclusiveFullscreen(settings) ? sf::State::Fullscreen : sf::State::Windowed;
}

unsigned int toWindowStyle(const AppSettings& settings) {
    return (settings.fullscreen && !settings.exclusiveFullscreen) ? sf::Style::None : sf::Style::Default;
}

bool hasDisplayChanges(const AppSettings& left, const AppSettings& right) {
    return left.fullscreen != right.fullscreen || left.exclusiveFullscreen != right.exclusiveFullscreen ||
           left.displayWidth != right.displayWidth ||
           left.displayHeight != right.displayHeight || left.refreshRate != right.refreshRate;
}

AppSettings sanitizeSettings(AppSettings settings) {
    settings.displayWidth = std::max(640, settings.displayWidth);
    settings.displayHeight = std::max(480, settings.displayHeight);
    settings.refreshRate = std::max(30, settings.refreshRate);
    settings.graphicsQuality = std::clamp(settings.graphicsQuality, 0, 3);
    settings.masterVolume = std::clamp(settings.masterVolume, 0.0f, 1.0f);
    settings.musicVolume = std::clamp(settings.musicVolume, 0.0f, 1.0f);
    settings.sfxVolume = std::clamp(settings.sfxVolume, 0.0f, 1.0f);

    const sf::VideoMode requestedMode = toVideoMode(settings);
    if (useExclusiveFullscreen(settings) && !requestedMode.isValid()) {
        const sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
        settings.displayWidth = static_cast<int>(desktopMode.size.x);
        settings.displayHeight = static_cast<int>(desktopMode.size.y);
    }

    return settings;
}

void applySystemDisplayMode(const AppSettings& settings) {
#ifdef _WIN32
    if (useExclusiveFullscreen(settings)) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(DEVMODEW);
        mode.dmPelsWidth = static_cast<DWORD>(settings.displayWidth);
        mode.dmPelsHeight = static_cast<DWORD>(settings.displayHeight);
        mode.dmDisplayFrequency = static_cast<DWORD>(settings.refreshRate);
        mode.dmBitsPerPel = 32;
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;

        const LONG changeResult = ChangeDisplaySettingsW(&mode, CDS_FULLSCREEN);
        if (changeResult != DISP_CHANGE_SUCCESSFUL) {
            spdlog::warn("ChangeDisplaySettingsW failed with code {}", static_cast<int>(changeResult));
        }
    } else {
        ChangeDisplaySettingsW(nullptr, 0);
    }
#else
    (void)settings;
#endif
}

std::vector<DisplayModeOption> refreshDisplayModeOptions() {
    std::vector<DisplayModeOption> displayModes;

#ifdef _WIN32
    std::unordered_set<unsigned long long> dedup;
    DEVMODEW mode{};
    mode.dmSize = sizeof(DEVMODEW);
    for (DWORD modeIndex = 0; EnumDisplaySettingsW(nullptr, modeIndex, &mode) != 0; ++modeIndex) {
        if (mode.dmPelsWidth == 0 || mode.dmPelsHeight == 0 || mode.dmDisplayFrequency == 0) {
            continue;
        }

        const unsigned long long key = (static_cast<unsigned long long>(mode.dmPelsWidth) << 40) |
                                       (static_cast<unsigned long long>(mode.dmPelsHeight) << 20) |
                                       static_cast<unsigned long long>(mode.dmDisplayFrequency);

        if (dedup.insert(key).second) {
            displayModes.push_back({static_cast<int>(mode.dmPelsWidth), static_cast<int>(mode.dmPelsHeight),
                                    static_cast<int>(mode.dmDisplayFrequency)});
        }
    }
#endif

    if (displayModes.empty()) {
        const auto& fullscreenModes = sf::VideoMode::getFullscreenModes();
        for (const auto& mode : fullscreenModes) {
            displayModes.push_back({static_cast<int>(mode.size.x), static_cast<int>(mode.size.y), 60});
        }
    }

    if (displayModes.empty()) {
        displayModes.push_back({1280, 720, 60});
    }

    std::sort(displayModes.begin(), displayModes.end(),
              [](const DisplayModeOption& left, const DisplayModeOption& right) {
                  if (left.width != right.width) {
                      return left.width > right.width;
                  }
                  if (left.height != right.height) {
                      return left.height > right.height;
                  }
                  return left.refreshRate > right.refreshRate;
              });

    return displayModes;
}

int findDisplayModeIndexForSettings(const std::vector<DisplayModeOption>& displayModes, const AppSettings& settings) {
    for (size_t i = 0; i < displayModes.size(); ++i) {
        const DisplayModeOption& mode = displayModes[i];
        if (mode.width == settings.displayWidth && mode.height == settings.displayHeight &&
            mode.refreshRate == settings.refreshRate) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void renderSceneLoadingOverlay(const std::string& loadingMessage, float progress) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags loadingWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                    ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("SceneLoading", nullptr, loadingWindowFlags);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 110.0f);
    ImGui::TextUnformatted(loadingMessage.c_str());

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 80.0f);
    ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));

    ImGui::End();
}

} // namespace

int AppController::run() {
    try {
        PlatformRuntime platformRuntime{L_};
        SettingsManager settingsManager;

        AppSettings activeSettings = sanitizeSettings(settingsManager.loadOrCreateDefaults());
        AppSettings workingSettings = activeSettings;

        std::vector<DisplayModeOption> displayModes = refreshDisplayModeOptions();
        int selectedDisplayModeIndex = findDisplayModeIndexForSettings(displayModes, workingSettings);

        sf::Window window;
        std::unique_ptr<VulkanContext> vulkanContext;
        std::unique_ptr<ImGuiLayer> imguiLayer;

        auto rebuildRuntime = [&](const AppSettings& settings) {
            if (vulkanContext) {
                vulkanContext->waitIdle();
            }

            imguiLayer.reset();
            vulkanContext.reset();

            applySystemDisplayMode(settings);
            window.create(toVideoMode(settings), "NodeSpireTD", toWindowStyle(settings), toWindowState(settings));
            if (settings.fullscreen && !settings.exclusiveFullscreen) {
                window.setPosition({0, 0});
            }
            window.setVerticalSyncEnabled(settings.vSyncEnabled);

            vulkanContext = std::make_unique<VulkanContext>(window);
            imguiLayer = std::make_unique<ImGuiLayer>();
            imguiLayer->initializeVulkanBackend(*vulkanContext);
            imguiLayer->setDisplaySize(window.getSize().x, window.getSize().y);
        };

        rebuildRuntime(activeSettings);

        SceneGraph sceneGraph = createDefaultScenes();
        SceneId currentSceneId = SceneId::Splash;

        struct PendingSceneTransition {
            bool active = false;
            SceneId targetSceneId = SceneId::MainMenu;
            std::string loadingMessage;
            float elapsedSeconds = 0.0f;
            float minDurationSeconds = 0.6f;
        };

        struct PendingDisplayConfirmation {
            bool active = false;
            AppSettings previousSettings{};
            AppSettings candidateSettings{};
            float secondsRemaining = 0.0f;
        };

        PendingSceneTransition pendingSceneTransition;
        PendingDisplayConfirmation pendingDisplayConfirmation;

        std::string activeLevelName = "Forest Outskirts";
        std::string activeLevelAssetPath = "assets/terrain/Terrain003_4K.obj";
        bool loadingComplete = true;

        auto makeSceneState = [&]() {
            return SceneSharedState{workingSettings,
                                    displayModes,
                                    selectedDisplayModeIndex,
                                    pendingDisplayConfirmation.active,
                                    pendingDisplayConfirmation.secondsRemaining,
                                    loadingComplete,
                                    activeLevelName,
                                    activeLevelAssetPath,
                                    vulkanContext.get(),
                                    imguiLayer->headingFont(),
                                    imguiLayer->titleFont()};
        };

        auto enterScene = [&](SceneId nextSceneId) {
            SceneSharedState state = makeSceneState();
            auto currentSceneIt = sceneGraph.find(currentSceneId);
            if (currentSceneIt != sceneGraph.end()) {
                currentSceneIt->second->onExit(state);
            }

            currentSceneId = nextSceneId;

            auto nextSceneIt = sceneGraph.find(currentSceneId);
            if (nextSceneIt != sceneGraph.end()) {
                nextSceneIt->second->onEnter(state);
            }
        };

        if (auto initialSceneIt = sceneGraph.find(currentSceneId); initialSceneIt != sceneGraph.end()) {
            SceneSharedState initialState = makeSceneState();
            initialSceneIt->second->onEnter(initialState);
        }

        sf::Clock deltaClock;
        bool windowResized = false;
        uint32_t resizedWidth = window.getSize().x;
        uint32_t resizedHeight = window.getSize().y;
        size_t currentFrame = 0;

        while (window.isOpen()) {
            const float dt = deltaClock.restart().asSeconds();
            platformRuntime.tick();
            imguiLayer->setDeltaTime(dt);

            if (pendingDisplayConfirmation.active) {
                pendingDisplayConfirmation.secondsRemaining -= dt;
                if (pendingDisplayConfirmation.secondsRemaining <= 0.0f) {
                    pendingDisplayConfirmation.secondsRemaining = 0.0f;
                }
            }

            while (const std::optional<sf::Event> event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                    continue;
                }

                if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                    windowResized = true;
                    resizedWidth = resized->size.x;
                    resizedHeight = resized->size.y;
                    imguiLayer->setDisplaySize(resizedWidth, resizedHeight);
                }

                auto sceneIt = sceneGraph.find(currentSceneId);
                if (sceneIt != sceneGraph.end()) {
                    sceneIt->second->handleEvent(*event, *imguiLayer);
                }
            }

            vulkanContext->waitForFrameFence(currentFrame);

            if (windowResized) {
                if (!vulkanContext->recreateSwapchain(resizedWidth, resizedHeight)) {
                    continue;
                }
                imguiLayer->setDisplaySize(resizedWidth, resizedHeight);
                windowResized = false;
                continue;
            }

            uint32_t imageIndex = 0;
            if (vulkanContext->acquireNextImage(currentFrame, imageIndex) == VulkanContext::AcquireStatus::OutOfDate) {
                windowResized = true;
                continue;
            }

            imguiLayer->beginFrame();

            SceneFrameResult frameResult;
            if (pendingSceneTransition.active) {
                pendingSceneTransition.elapsedSeconds += dt;

                const float progress =
                    pendingSceneTransition.minDurationSeconds > 0.0f
                        ? (pendingSceneTransition.elapsedSeconds / pendingSceneTransition.minDurationSeconds)
                        : 1.0f;
                renderSceneLoadingOverlay(pendingSceneTransition.loadingMessage, progress);

                if (pendingSceneTransition.elapsedSeconds >= pendingSceneTransition.minDurationSeconds) {
                    enterScene(pendingSceneTransition.targetSceneId);
                    pendingSceneTransition = {};
                }
            } else {
                SceneSharedState sceneState = makeSceneState();
                auto sceneIt = sceneGraph.find(currentSceneId);
                if (sceneIt != sceneGraph.end()) {
                    frameResult = sceneIt->second->render(sceneState, dt);
                }

                if (frameResult.requestTransition) {
                    // Defer scene teardown/enter to a later frame so any textures used by
                    // this frame's ImGui draw data remain valid through submission.
                    pendingSceneTransition.active = true;
                    pendingSceneTransition.targetSceneId = frameResult.transitionTarget;
                    pendingSceneTransition.loadingMessage =
                        frameResult.transitionMessage.empty() ? "Loading..." : frameResult.transitionMessage;
                    pendingSceneTransition.elapsedSeconds = 0.0f;
                    pendingSceneTransition.minDurationSeconds =
                        std::max(0.0f, frameResult.transitionMinDurationSeconds);

                    if (pendingSceneTransition.minDurationSeconds > 0.0f) {
                        renderSceneLoadingOverlay(pendingSceneTransition.loadingMessage, 0.0f);
                    }
                }
            }

            imguiLayer->endFrame();

            const AppSettings requestedSettings = sanitizeSettings(workingSettings);

            VkCommandBuffer commandBuffer = vulkanContext->beginFrameRecording(currentFrame, imageIndex);

            // 3-D world pass (before ImGui, so HUD overlays geometry)
            {
                auto sceneIt = sceneGraph.find(currentSceneId);
                if (sceneIt != sceneGraph.end()) {
                    sceneIt->second->renderWorld(commandBuffer, vulkanContext->extent());
                }
            }

            imguiLayer->renderDrawData(commandBuffer);
            vulkanContext->endFrameRecordingAndSubmit(currentFrame, imageIndex, commandBuffer);

            if (vulkanContext->present(imageIndex)) {
                windowResized = true;
            }

            if (frameResult.requestQuit) {
                auto sceneIt = sceneGraph.find(currentSceneId);
                if (sceneIt != sceneGraph.end()) {
                    SceneSharedState state = makeSceneState();
                    sceneIt->second->onExit(state);
                }
                window.close();
            }

            currentFrame = (currentFrame + 1) % VulkanContext::kMaxFramesInFlight;

            if (frameResult.requestRevertDisplayChanges ||
                (pendingDisplayConfirmation.active && pendingDisplayConfirmation.secondsRemaining <= 0.0f)) {
                activeSettings = pendingDisplayConfirmation.previousSettings;
                workingSettings = activeSettings;
                selectedDisplayModeIndex = findDisplayModeIndexForSettings(displayModes, workingSettings);
                rebuildRuntime(activeSettings);
                settingsManager.save(activeSettings);

                pendingDisplayConfirmation = {};
                currentFrame = 0;
                windowResized = false;
                resizedWidth = window.getSize().x;
                resizedHeight = window.getSize().y;
                continue;
            }

            if (frameResult.requestAcceptDisplayChanges && pendingDisplayConfirmation.active) {
                activeSettings = pendingDisplayConfirmation.candidateSettings;
                workingSettings = activeSettings;
                selectedDisplayModeIndex = findDisplayModeIndexForSettings(displayModes, workingSettings);
                settingsManager.save(activeSettings);
                pendingDisplayConfirmation = {};
            }

            if (frameResult.requestApplySettings) {
                const bool displayChanges = hasDisplayChanges(activeSettings, requestedSettings);
                if (displayChanges) {
                    PendingDisplayConfirmation nextConfirmation;
                    nextConfirmation.active = true;
                    nextConfirmation.previousSettings = activeSettings;
                    nextConfirmation.candidateSettings = requestedSettings;
                    nextConfirmation.secondsRemaining = 10.0f;

                    activeSettings = requestedSettings;
                    workingSettings = activeSettings;
                    selectedDisplayModeIndex = findDisplayModeIndexForSettings(displayModes, workingSettings);
                    rebuildRuntime(activeSettings);
                    pendingDisplayConfirmation = nextConfirmation;

                    currentFrame = 0;
                    windowResized = false;
                    resizedWidth = window.getSize().x;
                    resizedHeight = window.getSize().y;
                    continue;
                }

                activeSettings = requestedSettings;
                workingSettings = activeSettings;
                selectedDisplayModeIndex = findDisplayModeIndexForSettings(displayModes, workingSettings);
                window.setVerticalSyncEnabled(activeSettings.vSyncEnabled);
                settingsManager.save(activeSettings);
            }
        }

        if (vulkanContext) {
            vulkanContext->waitIdle();
        }

#ifdef _WIN32
        ChangeDisplaySettingsW(nullptr, 0);
#endif

        spdlog::info("Application Context Cleaned Up Flawlessly.");
        return 0;
    } catch (const std::exception& ex) {
        spdlog::error("Application startup/runtime failure: {}", ex.what());
        return -1;
    }
}
