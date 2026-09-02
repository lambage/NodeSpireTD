#include "AppController.hpp"

#include "AudioEngine.hpp"
#include "ImGuiLayer.hpp"
#include "PlatformRuntime.hpp"
#include "Scenes.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"
#include "scenes/IScene.hpp"
#include "scenes/SceneSharedState.hpp"

#include <SFML/Audio.hpp>
#include <SFML/Window.hpp>
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

bool supportsExclusiveFullscreen() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool useExclusiveFullscreen(const AppSettings& settings) {
    return supportsExclusiveFullscreen() && settings.fullscreen && settings.exclusiveFullscreen;
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
    if (!supportsExclusiveFullscreen()) {
        settings.exclusiveFullscreen = false;
    }
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

AppSettings mergeAudioSettingsForPersistence(const AppSettings& persistedBase, const AppSettings& working) {
    AppSettings merged = persistedBase;
    const AppSettings sanitizedWorking = sanitizeSettings(working);
    merged.masterVolume = sanitizedWorking.masterVolume;
    merged.musicVolume = sanitizedWorking.musicVolume;
    merged.sfxVolume = sanitizedWorking.sfxVolume;
    merged.muteWhenUnfocused = sanitizedWorking.muteWhenUnfocused;
    return merged;
}

AppSettings effectiveAudioSettings(const AppSettings& settings, bool windowFocused) {
    AppSettings effective = sanitizeSettings(settings);
    if (effective.muteWhenUnfocused && !windowFocused) {
        effective.masterVolume = 0.0f;
    }
    return effective;
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

class RuntimeHost {
  public:
    void rebuild(const AppSettings& settings) {
        if (vulkanContext_) {
            vulkanContext_->waitIdle();
        }

        imguiLayer_.reset();
        vulkanContext_.reset();

        applySystemDisplayMode(settings);
        window_.create(toVideoMode(settings), "NodeSpireTD", toWindowStyle(settings), toWindowState(settings));
        if (settings.fullscreen && !settings.exclusiveFullscreen) {
            window_.setPosition({0, 0});
        }
        window_.setVerticalSyncEnabled(settings.vSyncEnabled);

        vulkanContext_ = std::make_unique<VulkanContext>(window_);
        imguiLayer_ = std::make_unique<ImGuiLayer>();
        imguiLayer_->initializeVulkanBackend(*vulkanContext_);
        imguiLayer_->setDisplaySize(window_.getSize().x, window_.getSize().y);
    }

    sf::Window& window() { return window_; }
    VulkanContext& vulkanContext() { return *vulkanContext_; }
    const VulkanContext& vulkanContext() const { return *vulkanContext_; }
    VulkanContext* vulkanContextPtr() { return vulkanContext_.get(); }
    ImGuiLayer& imguiLayer() { return *imguiLayer_; }

    void waitIdle() {
        if (vulkanContext_) {
            vulkanContext_->waitIdle();
        }
    }

  private:
    sf::Window window_;
    std::unique_ptr<VulkanContext> vulkanContext_;
    std::unique_ptr<ImGuiLayer> imguiLayer_;
};

class DisplaySettingsCoordinator {
  public:
    explicit DisplaySettingsCoordinator(AppSettings initialSettings)
        : activeSettings_(sanitizeSettings(std::move(initialSettings))), workingSettings_(activeSettings_),
          displayModes_(refreshDisplayModeOptions()),
          selectedDisplayModeIndex_(findDisplayModeIndexForSettings(displayModes_, workingSettings_)) {}

    const AppSettings& activeSettings() const { return activeSettings_; }
    AppSettings& workingSettings() { return workingSettings_; }
    const std::vector<DisplayModeOption>& displayModes() const { return displayModes_; }
    int& selectedDisplayModeIndex() { return selectedDisplayModeIndex_; }
    bool confirmationActive() const { return pendingDisplayConfirmation_.active; }
    float confirmationSecondsRemaining() const { return pendingDisplayConfirmation_.secondsRemaining; }

    void tick(float dt) {
        if (pendingDisplayConfirmation_.active) {
            pendingDisplayConfirmation_.secondsRemaining =
                std::max(0.0f, pendingDisplayConfirmation_.secondsRemaining - dt);
        }
    }

    AppSettings requestedSettings() const {
        return sanitizeSettings(workingSettings_);
    }

    void saveMergedAudioSettings(SettingsManager& settingsManager) const {
        settingsManager.save(mergeAudioSettingsForPersistence(activeSettings_, workingSettings_));
    }

    bool handleRevertIfNeeded(const SceneRequestState& sceneRequests, RuntimeHost& runtimeHost,
                              const std::function<void()>& reloadActiveSceneResources,
                              SettingsManager& settingsManager, size_t& currentFrame, bool& windowResized,
                              uint32_t& resizedWidth, uint32_t& resizedHeight) {
        if (!sceneRequests.revertDisplayChangesRequested &&
            (!pendingDisplayConfirmation_.active || pendingDisplayConfirmation_.secondsRemaining > 0.0f)) {
            return false;
        }

        activeSettings_ = pendingDisplayConfirmation_.previousSettings;
        workingSettings_ = activeSettings_;
        selectedDisplayModeIndex_ = findDisplayModeIndexForSettings(displayModes_, workingSettings_);
        runtimeHost.rebuild(activeSettings_);
        reloadActiveSceneResources();
        settingsManager.save(activeSettings_);

        pendingDisplayConfirmation_ = {};
        resetFrameState(runtimeHost, currentFrame, windowResized, resizedWidth, resizedHeight);
        return true;
    }

    void handleAcceptIfRequested(const SceneRequestState& sceneRequests, SettingsManager& settingsManager) {
        if (!sceneRequests.acceptDisplayChangesRequested || !pendingDisplayConfirmation_.active) {
            return;
        }

        activeSettings_ = pendingDisplayConfirmation_.candidateSettings;
        workingSettings_ = activeSettings_;
        selectedDisplayModeIndex_ = findDisplayModeIndexForSettings(displayModes_, workingSettings_);
        settingsManager.save(activeSettings_);
        pendingDisplayConfirmation_ = {};
    }

    bool handleApplyIfRequested(const SceneRequestState& sceneRequests, RuntimeHost& runtimeHost,
                                const std::function<void()>& reloadActiveSceneResources,
                                SettingsManager& settingsManager, size_t& currentFrame, bool& windowResized,
                                uint32_t& resizedWidth, uint32_t& resizedHeight) {
        if (!sceneRequests.applySettingsRequested) {
            return false;
        }

        const AppSettings requested = requestedSettings();
        if (hasDisplayChanges(activeSettings_, requested)) {
            PendingDisplayConfirmation nextConfirmation;
            nextConfirmation.active = true;
            nextConfirmation.previousSettings = activeSettings_;
            nextConfirmation.candidateSettings = requested;
            nextConfirmation.secondsRemaining = 10.0f;

            activeSettings_ = requested;
            workingSettings_ = activeSettings_;
            selectedDisplayModeIndex_ = findDisplayModeIndexForSettings(displayModes_, workingSettings_);
            runtimeHost.rebuild(activeSettings_);
            reloadActiveSceneResources();
            pendingDisplayConfirmation_ = nextConfirmation;

            resetFrameState(runtimeHost, currentFrame, windowResized, resizedWidth, resizedHeight);
            return true;
        }

        activeSettings_ = requested;
        workingSettings_ = activeSettings_;
        selectedDisplayModeIndex_ = findDisplayModeIndexForSettings(displayModes_, workingSettings_);
        runtimeHost.window().setVerticalSyncEnabled(activeSettings_.vSyncEnabled);
        settingsManager.save(activeSettings_);
        return false;
    }

  private:
    static void resetFrameState(RuntimeHost& runtimeHost, size_t& currentFrame, bool& windowResized,
                                uint32_t& resizedWidth, uint32_t& resizedHeight) {
        currentFrame = 0;
        windowResized = false;
        resizedWidth = runtimeHost.window().getSize().x;
        resizedHeight = runtimeHost.window().getSize().y;
    }

    AppSettings activeSettings_;
    AppSettings workingSettings_;
    std::vector<DisplayModeOption> displayModes_;
    int selectedDisplayModeIndex_ = 0;
    PendingDisplayConfirmation pendingDisplayConfirmation_{};
};

class SceneDirector {
  public:
    SceneDirector() : sceneGraph_(createDefaultScenes()) {}

    void enterInitialScene(DisplaySettingsCoordinator& displaySettings, RuntimeHost& runtimeHost,
                           AudioEngine& audioEngine) {
        if (auto initialSceneIt = sceneGraph_.find(currentSceneId_); initialSceneIt != sceneGraph_.end()) {
            SceneSharedState initialState = makeSceneState(displaySettings, runtimeHost, audioEngine);
            initialSceneIt->second->onEnter(initialState);
        }
    }

    void handleEvent(const sf::Event& event, ImGuiLayer& imguiLayer) {
        auto sceneIt = sceneGraph_.find(currentSceneId_);
        if (sceneIt != sceneGraph_.end()) {
            sceneIt->second->handleEvent(event, imguiLayer);
        }
    }

    void render(DisplaySettingsCoordinator& displaySettings, RuntimeHost& runtimeHost, AudioEngine& audioEngine,
                float dt, SceneRequestState& outSceneRequests) {
        if (pendingSceneTransition_.active) {
            pendingSceneTransition_.elapsedSeconds += dt;

            const float progress =
                pendingSceneTransition_.minDurationSeconds > 0.0f
                    ? (pendingSceneTransition_.elapsedSeconds / pendingSceneTransition_.minDurationSeconds)
                    : 1.0f;
            renderSceneLoadingOverlay(pendingSceneTransition_.loadingMessage, progress);

            if (pendingSceneTransition_.elapsedSeconds >= pendingSceneTransition_.minDurationSeconds) {
                enterScene(pendingSceneTransition_.targetSceneId, displaySettings, runtimeHost, audioEngine);
                pendingSceneTransition_ = {};
            }
            outSceneRequests = {};
            return;
        }

        SceneSharedState sceneState = makeSceneState(displaySettings, runtimeHost, audioEngine);
        auto sceneIt = sceneGraph_.find(currentSceneId_);
        if (sceneIt == sceneGraph_.end()) {
            outSceneRequests = {};
            return;
        }

        sceneIt->second->render(sceneState, dt);
        outSceneRequests = sceneIt->second->consumeSceneRequests();
    }

    void renderWorld(VkCommandBuffer commandBuffer, const RuntimeHost& runtimeHost) {
        auto sceneIt = sceneGraph_.find(currentSceneId_);
        if (sceneIt != sceneGraph_.end()) {
            sceneIt->second->renderWorld(commandBuffer, runtimeHost.vulkanContext().extent());
        }
    }

    void queueTransition(const SceneTransitionRequest& request) {
        pendingSceneTransition_.active = true;
        pendingSceneTransition_.targetSceneId = request.target;
        pendingSceneTransition_.loadingMessage = request.message.empty() ? "Loading..." : request.message;
        pendingSceneTransition_.elapsedSeconds = 0.0f;
        pendingSceneTransition_.minDurationSeconds = std::max(0.0f, request.minDurationSeconds);

        if (pendingSceneTransition_.minDurationSeconds > 0.0f) {
            renderSceneLoadingOverlay(pendingSceneTransition_.loadingMessage, 0.0f);
        }
    }

    void reloadActiveSceneResources(DisplaySettingsCoordinator& displaySettings, RuntimeHost& runtimeHost,
                                    AudioEngine& audioEngine) {
        auto sceneIt = sceneGraph_.find(currentSceneId_);
        if (sceneIt == sceneGraph_.end()) {
            return;
        }

        SceneSharedState exitState = makeSceneState(displaySettings, runtimeHost, audioEngine);
        sceneIt->second->onExit(exitState);

        SceneSharedState enterState = makeSceneState(displaySettings, runtimeHost, audioEngine);
        sceneIt->second->onEnter(enterState);
    }

    void exitActiveScene(DisplaySettingsCoordinator& displaySettings, RuntimeHost& runtimeHost, AudioEngine& audioEngine) {
        auto sceneIt = sceneGraph_.find(currentSceneId_);
        if (sceneIt == sceneGraph_.end()) {
            return;
        }

        SceneSharedState state = makeSceneState(displaySettings, runtimeHost, audioEngine);
        sceneIt->second->onExit(state);
    }

  private:
    SceneSharedState makeSceneState(DisplaySettingsCoordinator& displaySettings, RuntimeHost& runtimeHost,
                                    AudioEngine& audioEngine) {
        return SceneSharedState{displaySettings.workingSettings(),
                                displaySettings.displayModes(),
                                displaySettings.selectedDisplayModeIndex(),
                                displaySettings.confirmationActive(),
                                displaySettings.confirmationSecondsRemaining(),
                                loadingComplete_,
                                activeLevelName_,
                                activeLevelAssetPath_,
                                activeLevelScriptPath_,
                                audioEngine.activeAssetKeys(),
                                hostMultiplayerMatch_,
                                multiplayerPort_,
                                joinRemoteHostAddress_,
                                runtimeHost.vulkanContextPtr(),
                                &audioEngine,
                                runtimeHost.imguiLayer().headingFont(),
                                runtimeHost.imguiLayer().titleFont()};
    }

    void enterScene(SceneId nextSceneId, DisplaySettingsCoordinator& displaySettings, RuntimeHost& runtimeHost,
                    AudioEngine& audioEngine) {
        SceneSharedState state = makeSceneState(displaySettings, runtimeHost, audioEngine);
        auto currentSceneIt = sceneGraph_.find(currentSceneId_);
        if (currentSceneIt != sceneGraph_.end()) {
            currentSceneIt->second->onExit(state);
        }

        currentSceneId_ = nextSceneId;

        auto nextSceneIt = sceneGraph_.find(currentSceneId_);
        if (nextSceneIt != sceneGraph_.end()) {
            nextSceneIt->second->onEnter(state);
        }
    }

    SceneGraph sceneGraph_;
    SceneId currentSceneId_ = SceneId::Splash;
    PendingSceneTransition pendingSceneTransition_{};
    std::string activeLevelName_ = "Forest Outskirts";
    std::string activeLevelAssetPath_ = "assets/terrain/Terrain003_4K.obj";
    std::string activeLevelScriptPath_;
    bool loadingComplete_ = true;
    bool hostMultiplayerMatch_ = false;
    unsigned short multiplayerPort_ = 47321;
    std::string joinRemoteHostAddress_;
};

} // namespace

int AppController::run() {
    try {
        PlatformRuntime platformRuntime{L_};
        SettingsManager settingsManager;
        DisplaySettingsCoordinator displaySettings(settingsManager.loadOrCreateDefaults());
        RuntimeHost runtimeHost;
        runtimeHost.rebuild(displaySettings.activeSettings());
        SceneDirector sceneDirector;
        AudioEngine audioEngine;
        sceneDirector.enterInitialScene(displaySettings, runtimeHost, audioEngine);

        sf::Clock deltaClock;
        bool windowResized = false;
        uint32_t resizedWidth = runtimeHost.window().getSize().x;
        uint32_t resizedHeight = runtimeHost.window().getSize().y;
        size_t currentFrame = 0;

        while (runtimeHost.window().isOpen()) {
            const float dt = deltaClock.restart().asSeconds();
            platformRuntime.tick();
            runtimeHost.imguiLayer().setDeltaTime(dt);
            displaySettings.tick(dt);

            while (const std::optional<sf::Event> event = runtimeHost.window().pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    displaySettings.saveMergedAudioSettings(settingsManager);
                    runtimeHost.window().close();
                    continue;
                }

                if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                    windowResized = true;
                    resizedWidth = resized->size.x;
                    resizedHeight = resized->size.y;
                    runtimeHost.imguiLayer().setDisplaySize(resizedWidth, resizedHeight);
                }

                sceneDirector.handleEvent(*event, runtimeHost.imguiLayer());
            }

            runtimeHost.vulkanContext().waitForFrameFence(currentFrame);

            if (windowResized) {
                if (!runtimeHost.vulkanContext().recreateSwapchain(resizedWidth, resizedHeight)) {
                    continue;
                }
                runtimeHost.imguiLayer().setDisplaySize(resizedWidth, resizedHeight);
                windowResized = false;
                continue;
            }

            uint32_t imageIndex = 0;
            if (runtimeHost.vulkanContext().acquireNextImage(currentFrame, imageIndex) ==
                VulkanContext::AcquireStatus::OutOfDate) {
                windowResized = true;
                continue;
            }

            runtimeHost.imguiLayer().beginFrame();

            {
                const bool windowFocused = runtimeHost.window().hasFocus();
                audioEngine.setEffectiveSettings(effectiveAudioSettings(displaySettings.workingSettings(), windowFocused));
            }

            SceneRequestState sceneRequests;
            sceneDirector.render(displaySettings, runtimeHost, audioEngine, dt, sceneRequests);

            for (const AudioReleaseRequest& request : sceneRequests.audioReleaseRequests) {
                audioEngine.release(request.path, request.channel);
            }

            for (const AudioPreloadRequest& request : sceneRequests.audioPreloadRequests) {
                audioEngine.preload(request.path, request.channel);
            }

            for (const AudioPlayRequest& request : sceneRequests.audioPlayRequests) {
                audioEngine.play(request.path, request.channel, request.loop, request.gain);
            }

            if (sceneRequests.sceneTransitionRequested) {
                sceneDirector.queueTransition(sceneRequests.sceneTransition);
            }

            audioEngine.update(dt);

            runtimeHost.imguiLayer().endFrame();

            VkCommandBuffer commandBuffer = runtimeHost.vulkanContext().beginFrameRecording(currentFrame, imageIndex);

            sceneDirector.renderWorld(commandBuffer, runtimeHost);

            runtimeHost.imguiLayer().renderDrawData(commandBuffer);
            runtimeHost.vulkanContext().endFrameRecordingAndSubmit(currentFrame, imageIndex, commandBuffer);

            if (runtimeHost.vulkanContext().present(imageIndex)) {
                windowResized = true;
            }

            if (sceneRequests.quitRequested) {
                sceneDirector.exitActiveScene(displaySettings, runtimeHost, audioEngine);
                displaySettings.saveMergedAudioSettings(settingsManager);
                runtimeHost.window().close();
            }

            if (!runtimeHost.window().isOpen()) {
                continue;
            }

            currentFrame = (currentFrame + 1) % VulkanContext::kMaxFramesInFlight;

            const auto reloadActiveSceneResources = [&]() {
                sceneDirector.reloadActiveSceneResources(displaySettings, runtimeHost, audioEngine);
            };
            if (displaySettings.handleRevertIfNeeded(sceneRequests, runtimeHost, reloadActiveSceneResources,
                                                     settingsManager, currentFrame, windowResized, resizedWidth,
                                                     resizedHeight)) {
                continue;
            }

            displaySettings.handleAcceptIfRequested(sceneRequests, settingsManager);
            if (displaySettings.handleApplyIfRequested(sceneRequests, runtimeHost, reloadActiveSceneResources,
                                                       settingsManager, currentFrame, windowResized, resizedWidth,
                                                       resizedHeight)) {
                continue;
            }
        }

        runtimeHost.waitIdle();
        displaySettings.saveMergedAudioSettings(settingsManager);

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
