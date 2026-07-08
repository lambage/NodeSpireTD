#include "AppController.hpp"

#include "ImGuiLayer.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"

#include <SFML/Window.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

sf::VideoMode toVideoMode(const AppSettings& settings) {
    return sf::VideoMode({static_cast<unsigned int>(settings.displayWidth), static_cast<unsigned int>(settings.displayHeight)});
}

sf::State toWindowState(const AppSettings& settings) {
    return settings.fullscreen ? sf::State::Fullscreen : sf::State::Windowed;
}

bool hasDisplayChanges(const AppSettings& left, const AppSettings& right) {
    return left.fullscreen != right.fullscreen ||
           left.displayWidth != right.displayWidth ||
           left.displayHeight != right.displayHeight ||
           left.refreshRate != right.refreshRate;
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
    if (settings.fullscreen && !requestedMode.isValid()) {
        const sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
        settings.displayWidth = static_cast<int>(desktopMode.size.x);
        settings.displayHeight = static_cast<int>(desktopMode.size.y);
    }

    return settings;
}

void applySystemDisplayMode(const AppSettings& settings) {
#ifdef _WIN32
    if (settings.fullscreen) {
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

} // namespace

int AppController::run() {
    try {
        SettingsManager settingsManager;
        AppSettings activeSettings = sanitizeSettings(settingsManager.loadOrCreateDefaults());

        sf::Window window;

        std::unique_ptr<VulkanContext> vulkanContext;
        std::unique_ptr<ImGuiLayer> imguiLayer;

        auto rebuildRuntime = [&](const AppSettings& settings, bool openOptions, bool showDisplayConfirmation, float confirmationSeconds) {
            if (vulkanContext) {
                vulkanContext->waitIdle();
            }

            imguiLayer.reset();
            vulkanContext.reset();

            applySystemDisplayMode(settings);
            window.create(toVideoMode(settings), "NodeSpireTD", sf::Style::Default, toWindowState(settings));
            window.setVerticalSyncEnabled(settings.vSyncEnabled);

            vulkanContext = std::make_unique<VulkanContext>(window);
            imguiLayer = std::make_unique<ImGuiLayer>();
            imguiLayer->initializeVulkanBackend(*vulkanContext);
            imguiLayer->setSettings(settings);
            imguiLayer->setLoadingComplete();
            imguiLayer->setDisplaySize(window.getSize().x, window.getSize().y);
            imguiLayer->setDisplayConfirmationState(showDisplayConfirmation, confirmationSeconds);
            if (openOptions) {
                imguiLayer->openOptionsScene();
            }
        };

        rebuildRuntime(activeSettings, false, false, 0.0f);

        sf::Clock deltaClock;
        bool windowResized = false;
        uint32_t resizedWidth = window.getSize().x;
        uint32_t resizedHeight = window.getSize().y;

        size_t currentFrame = 0;

        struct PendingDisplayConfirmation {
            bool active = false;
            AppSettings previousSettings{};
            AppSettings candidateSettings{};
            float secondsRemaining = 0.0f;
        };

        PendingDisplayConfirmation pendingDisplayConfirmation;

        while (window.isOpen()) {
            const float elapsedSeconds = deltaClock.restart().asSeconds();
            imguiLayer->setDeltaTime(elapsedSeconds);

            if (pendingDisplayConfirmation.active) {
                pendingDisplayConfirmation.secondsRemaining -= elapsedSeconds;
                if (pendingDisplayConfirmation.secondsRemaining <= 0.0f) {
                    pendingDisplayConfirmation.secondsRemaining = 0.0f;
                }
            }

            imguiLayer->setDisplayConfirmationState(
                pendingDisplayConfirmation.active,
                pendingDisplayConfirmation.secondsRemaining);

            while (const std::optional<sf::Event> event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                    continue;
                }

                ImGuiLayer::EventResult eventResult = imguiLayer->processEvent(*event);
                if (eventResult.resized) {
                    windowResized = true;
                    resizedWidth = eventResult.width;
                    resizedHeight = eventResult.height;
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
            const ImGuiLayer::RenderResult uiResult = imguiLayer->renderSceneUi();
            const AppSettings requestedSettings = sanitizeSettings(imguiLayer->settings());

            if (uiResult.requestQuit) {
                window.close();
            }

            VkCommandBuffer commandBuffer = vulkanContext->beginFrameRecording(currentFrame, imageIndex);
            imguiLayer->renderDrawData(commandBuffer);
            vulkanContext->endFrameRecordingAndSubmit(currentFrame, imageIndex, commandBuffer);

            if (vulkanContext->present(imageIndex)) {
                windowResized = true;
            }

            currentFrame = (currentFrame + 1) % VulkanContext::kMaxFramesInFlight;

            if (uiResult.requestRevertDisplayChanges ||
                (pendingDisplayConfirmation.active && pendingDisplayConfirmation.secondsRemaining <= 0.0f)) {
                activeSettings = pendingDisplayConfirmation.previousSettings;
                rebuildRuntime(activeSettings, true, false, 0.0f);
                settingsManager.save(activeSettings);

                pendingDisplayConfirmation = {};
                currentFrame = 0;
                windowResized = false;
                resizedWidth = window.getSize().x;
                resizedHeight = window.getSize().y;
                continue;
            }

            if (uiResult.requestAcceptDisplayChanges && pendingDisplayConfirmation.active) {
                activeSettings = pendingDisplayConfirmation.candidateSettings;
                settingsManager.save(activeSettings);
                pendingDisplayConfirmation = {};
                imguiLayer->setDisplayConfirmationState(false, 0.0f);
            }

            if (uiResult.requestApplySettings) {
                const bool displayChanges = hasDisplayChanges(activeSettings, requestedSettings);
                if (displayChanges) {
                    PendingDisplayConfirmation nextConfirmation;
                    nextConfirmation.active = true;
                    nextConfirmation.previousSettings = activeSettings;
                    nextConfirmation.candidateSettings = requestedSettings;
                    nextConfirmation.secondsRemaining = 10.0f;

                    activeSettings = requestedSettings;
                    rebuildRuntime(activeSettings, true, true, nextConfirmation.secondsRemaining);
                    pendingDisplayConfirmation = nextConfirmation;

                    currentFrame = 0;
                    windowResized = false;
                    resizedWidth = window.getSize().x;
                    resizedHeight = window.getSize().y;
                    continue;
                }

                activeSettings = requestedSettings;
                window.setVerticalSyncEnabled(activeSettings.vSyncEnabled);
                imguiLayer->setSettings(activeSettings);
                settingsManager.save(activeSettings);
            }
        }

        // Ensure all queued GPU work is done before ImGui backend destroys Vulkan objects.
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
