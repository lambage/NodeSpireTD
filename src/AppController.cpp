#include "AppController.hpp"

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
#include <imgui.h>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr float kAudioStartGraceSeconds = 0.05f;

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

std::string makeAudioAssetKey(const std::string& path, AudioChannel channel) {
    return (channel == AudioChannel::Music ? "music:" : "sfx:") + path;
}

float computeMusicVolumePercent(const AppSettings& settings, float gain) {
    const float mixed = settings.masterVolume * settings.musicVolume * std::clamp(gain, 0.0f, 1.0f);
    return std::clamp(mixed, 0.0f, 1.0f) * 100.0f;
}

float computeSfxVolumePercent(const AppSettings& settings, float gain) {
    const float mixed = settings.masterVolume * settings.sfxVolume * std::clamp(gain, 0.0f, 1.0f);
    return std::clamp(mixed, 0.0f, 1.0f) * 100.0f;
}

#if SFML_VERSION_MAJOR >= 3
void setMusicLoopEnabled(sf::Music& music, bool enabled) {
    music.setLooping(enabled);
}
#else
void setMusicLoopEnabled(sf::Music& music, bool enabled) {
    music.setLoop(enabled);
}
#endif

bool isMusicStopped(const sf::Music& music) {
    return music.getStatus() == sf::SoundSource::Status::Stopped;
}

#if SFML_VERSION_MAJOR >= 3
void setSoundLoopEnabled(sf::Sound& sound, bool enabled) {
    sound.setLooping(enabled);
}
#else
void setSoundLoopEnabled(sf::Sound& sound, bool enabled) {
    sound.setLoop(enabled);
}
#endif

bool isSoundStopped(const sf::Sound& sound) {
    return sound.getStatus() == sf::SoundSource::Status::Stopped;
}

struct LuaMusicPlayback {
    std::string path;
    float gain = 1.0f;
    float ageSeconds = 0.0f;
    std::unique_ptr<sf::Music> music;
};

struct LuaSfxPlayback {
    std::string path;
    float gain = 1.0f;
    float ageSeconds = 0.0f;
    std::shared_ptr<sf::SoundBuffer> buffer;
    std::unique_ptr<sf::Sound> sound;
};

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
        std::vector<LuaMusicPlayback> activeLuaMusicPlaybacks;
        std::vector<LuaSfxPlayback> activeLuaSfxPlaybacks;
        std::unordered_set<std::string> activeAudioAssetKeys;
        std::unordered_map<std::string, std::shared_ptr<sf::SoundBuffer>> luaSfxBufferCache;

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
        std::string activeLevelScriptPath = "";
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
                                    activeLevelScriptPath,
                                    activeAudioAssetKeys,
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

            SceneRequestState sceneRequests;
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
                    sceneIt->second->render(sceneState, dt);
                    sceneRequests = sceneIt->second->consumeSceneRequests();
                }

                for (const AudioReleaseRequest& request : sceneRequests.audioReleaseRequests) {
                    if (request.path.empty()) {
                        continue;
                    }

                    if (request.channel == AudioChannel::Sfx) {
                        luaSfxBufferCache.erase(request.path);
                    }
                }

                for (const AudioPreloadRequest& request : sceneRequests.audioPreloadRequests) {
                    if (request.path.empty()) {
                        spdlog::warn("Rejected empty audio preload request path.");
                        continue;
                    }

                    if (request.channel == AudioChannel::Sfx) {
                        if (!luaSfxBufferCache.contains(request.path)) {
                            auto loadedBuffer = std::make_shared<sf::SoundBuffer>();
                            if (!loadedBuffer->loadFromFile(request.path)) {
                                spdlog::warn("Failed to preload sfx file from Lua request: {}", request.path);
                                continue;
                            }
                            luaSfxBufferCache.emplace(request.path, std::move(loadedBuffer));
                        }
                    }
                }

                for (const AudioPlayRequest& request : sceneRequests.audioPlayRequests) {
                    if (request.path.empty()) {
                        spdlog::warn("Rejected empty audio playback request path.");
                        continue;
                    }

                    if (request.channel == AudioChannel::Music) {
                        auto music = std::make_unique<sf::Music>();
                        if (!music->openFromFile(request.path)) {
                            spdlog::warn("Failed to open music file from Lua playback request: {}", request.path);
                            continue;
                        }

                        setMusicLoopEnabled(*music, request.loop);
                        music->setVolume(computeMusicVolumePercent(activeSettings, request.gain));

                        // Music is single-instance: replace currently playing track.
                        for (auto& existing : activeLuaMusicPlaybacks) {
                            if (existing.music) {
                                existing.music->stop();
                            }
                        }
                        activeLuaMusicPlaybacks.clear();

                        music->play();

                        LuaMusicPlayback playback;
                        playback.path = request.path;
                        playback.gain = request.gain;
                        playback.music = std::move(music);
                        activeLuaMusicPlaybacks.push_back(std::move(playback));
                        continue;
                    }

                    std::shared_ptr<sf::SoundBuffer> buffer;
                    if (auto it = luaSfxBufferCache.find(request.path); it != luaSfxBufferCache.end()) {
                        buffer = it->second;
                    } else {
                        auto loadedBuffer = std::make_shared<sf::SoundBuffer>();
                        if (!loadedBuffer->loadFromFile(request.path)) {
                            spdlog::warn("Failed to open sfx file from Lua playback request: {}", request.path);
                            continue;
                        }
                        luaSfxBufferCache.emplace(request.path, loadedBuffer);
                        buffer = std::move(loadedBuffer);
                    }

                    LuaSfxPlayback playback;
                    playback.path = request.path;
                    playback.gain = request.gain;
                    playback.buffer = buffer;
                    playback.sound = std::make_unique<sf::Sound>(*playback.buffer);
                    setSoundLoopEnabled(*playback.sound, request.loop);
                    playback.sound->setVolume(computeSfxVolumePercent(activeSettings, request.gain));
                    playback.sound->play();
                    activeLuaSfxPlaybacks.push_back(std::move(playback));
                }

                if (sceneRequests.sceneTransitionRequested) {
                    // Defer scene teardown/enter to a later frame so any textures used by
                    // this frame's ImGui draw data remain valid through submission.
                    pendingSceneTransition.active = true;
                    pendingSceneTransition.targetSceneId = sceneRequests.sceneTransition.target;
                    pendingSceneTransition.loadingMessage =
                        sceneRequests.sceneTransition.message.empty() ? "Loading..." : sceneRequests.sceneTransition.message;
                    pendingSceneTransition.elapsedSeconds = 0.0f;
                    pendingSceneTransition.minDurationSeconds =
                        std::max(0.0f, sceneRequests.sceneTransition.minDurationSeconds);

                    if (pendingSceneTransition.minDurationSeconds > 0.0f) {
                        renderSceneLoadingOverlay(pendingSceneTransition.loadingMessage, 0.0f);
                    }
                }
            }

            for (auto& playback : activeLuaMusicPlaybacks) {
                if (playback.music) {
                    playback.ageSeconds += dt;
                    playback.music->setVolume(computeMusicVolumePercent(activeSettings, playback.gain));
                }
            }

            activeLuaMusicPlaybacks.erase(
                std::remove_if(activeLuaMusicPlaybacks.begin(), activeLuaMusicPlaybacks.end(),
                               [](const LuaMusicPlayback& playback) {
                                   return !playback.music ||
                                          (playback.ageSeconds >= kAudioStartGraceSeconds &&
                                           isMusicStopped(*playback.music));
                               }),
                activeLuaMusicPlaybacks.end());

            for (auto& playback : activeLuaSfxPlaybacks) {
                if (playback.sound) {
                    playback.ageSeconds += dt;
                    playback.sound->setVolume(computeSfxVolumePercent(activeSettings, playback.gain));
                }
            }

            activeLuaSfxPlaybacks.erase(
                std::remove_if(activeLuaSfxPlaybacks.begin(), activeLuaSfxPlaybacks.end(),
                               [](const LuaSfxPlayback& playback) {
                                   return !playback.sound ||
                                          (playback.ageSeconds >= kAudioStartGraceSeconds &&
                                           isSoundStopped(*playback.sound));
                               }),
                activeLuaSfxPlaybacks.end());

            activeAudioAssetKeys.clear();
            for (const auto& playback : activeLuaMusicPlaybacks) {
                if (playback.music) {
                    activeAudioAssetKeys.insert(makeAudioAssetKey(playback.path, AudioChannel::Music));
                }
            }
            for (const auto& playback : activeLuaSfxPlaybacks) {
                if (playback.sound) {
                    activeAudioAssetKeys.insert(makeAudioAssetKey(playback.path, AudioChannel::Sfx));
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

            if (sceneRequests.quitRequested) {
                auto sceneIt = sceneGraph.find(currentSceneId);
                if (sceneIt != sceneGraph.end()) {
                    SceneSharedState state = makeSceneState();
                    sceneIt->second->onExit(state);
                }
                window.close();
            }

            currentFrame = (currentFrame + 1) % VulkanContext::kMaxFramesInFlight;

            if (sceneRequests.revertDisplayChangesRequested ||
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

            if (sceneRequests.acceptDisplayChangesRequested && pendingDisplayConfirmation.active) {
                activeSettings = pendingDisplayConfirmation.candidateSettings;
                workingSettings = activeSettings;
                selectedDisplayModeIndex = findDisplayModeIndexForSettings(displayModes, workingSettings);
                settingsManager.save(activeSettings);
                pendingDisplayConfirmation = {};
            }

            if (sceneRequests.applySettingsRequested) {
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
