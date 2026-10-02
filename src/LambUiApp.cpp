// LambUI-based app bootstrap using SDL3 and the game-owned VulkanContext.
// Replaces the old RmlUi/Backend bootstrap: LambUI has no equivalent
// "Backend" abstraction, so this file owns SDL window/event-pump setup
// directly and feeds LambUI::UIManager via lambui_backend (SDL input
// bridge + Vulkan IRenderer reconciled with VulkanContext).
#include "AudioEngine.hpp"
#include "LambUiFontLoader.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"
#include "lambui/AppControl.hpp"
#include "lambui/SceneManager.hpp"
#include "lambui/SceneTypes.hpp"
#include "lambui_backend/SdlUiInputBridge.hpp"
#include "lambui_backend/VulkanUiRenderer.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "multiplayer/PlayerProfileStore.hpp"

#include <LambUI/UIManager.h>
#include <LambUI/UILog.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <spdlog/spdlog.h>

namespace {

// RAII guard so every early-return path below still tears down SDL.
struct SdlGuard {
    SdlGuard() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
    }
    ~SdlGuard() { SDL_Quit(); }
    SdlGuard(const SdlGuard&) = delete;
    SdlGuard& operator=(const SdlGuard&) = delete;
};

SDL_Window* createWindow(const StartupWindowConfig& startupWindow) {
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    if (startupWindow.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    SDL_Window* window = SDL_CreateWindow("NodeSpireTD", startupWindow.width, startupWindow.height, flags);
    if (!window) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    SDL_StartTextInput(window);
    return window;
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
  try {
        LambUI::Log::SetCallback([](LambUI::LogLevel, const char* tag, const std::string& message) {
            std::fprintf(stderr, "LambUI [%s]: %s\n", tag, message.c_str());
                std::fflush(stderr);
        });
        LambUI::Log::SetMinLevel(LambUI::LogLevel::Debug);

    const AppSettings startupSettings = SettingsManager().loadOrCreateDefaults();
    const auto startupWindow = resolveStartupWindowConfig(startupSettings);

    SdlGuard sdlGuard;
    SDL_Window* window = createWindow(startupWindow);

    auto vulkanContext = std::make_unique<VulkanContext>(window);
    vulkanContext->setVSyncEnabled(startupSettings.vSyncEnabled);
    std::fprintf(stderr, "TRACE: VulkanContext ready\n"); std::fflush(stderr);

    LambUiFontLoader fontLoader;
    if (!fontLoader.loadAll("assets/fonts")) {
        spdlog::warn("{}", "LambUiApp: one or more fonts failed to load from assets/fonts");
    }
    std::fprintf(stderr, "TRACE: fonts loaded\n"); std::fflush(stderr);

    auto renderer = std::make_shared<lambui_backend::VulkanUiRenderer>(*vulkanContext);
    std::fprintf(stderr, "TRACE: UI renderer created\n"); std::fflush(stderr);
    fontLoader.forEachFont([&renderer](void* fontHandle, const LambUI::FontAtlas& atlas) {
        renderer->LoadFont(atlas, fontHandle);
    });
    std::fprintf(stderr, "TRACE: fonts uploaded to renderer\n"); std::fflush(stderr);

    LambUI::UIManager uiManager(renderer, fontLoader.textMeasurer());
    const VkExtent2D initialExtent = vulkanContext->extent();
    uiManager.SetDisplaySize(static_cast<float>(initialExtent.width), static_cast<float>(initialExtent.height));
    std::fprintf(stderr, "TRACE: UIManager ready\n"); std::fflush(stderr);

    {
        AudioEngine audioEngine(startupSettings.audioDevice);
        audioEngine.setEffectiveSettings(startupSettings);
        multiplayer::MultiplayerSession multiplayerSession;
        multiplayer::PlayerProfileStore playerProfileStore;
        std::fprintf(stderr, "TRACE: audio/multiplayer ready, entering Splash scene\n"); std::fflush(stderr);

        NodeSpireUi::SceneManager sceneManager(uiManager, NodeSpireUi::SceneId::Splash, audioEngine, *vulkanContext,
                                               multiplayerSession, playerProfileStore);
        std::fprintf(stderr, "TRACE: Splash scene entered, starting main loop\n"); std::fflush(stderr);

        auto lastFrameTime = std::chrono::steady_clock::now();
        bool running = true;
        size_t frameIndex = 0;
        while (running) {
            const auto onKeyDown = [&](SDL_Keycode keycode, uint32_t scanCode) {
                if (keycode == SDLK_F5) {
                    sceneManager.reloadActiveScene();
                    return;
                }
                if (scanCode != 0) {
                    sceneManager.handleKeyDown(scanCode);
                }
            };

            const auto pumpResult = lambui_backend::SdlUiInputBridge::Pump(uiManager, onKeyDown);
            if (pumpResult.quitRequested || NodeSpireUi::QuitRequested()) {
                running = false;
                continue;
            }
            if (pumpResult.windowResized && pumpResult.windowWidth > 0 && pumpResult.windowHeight > 0) {
                vulkanContext->recreateSwapchain(static_cast<uint32_t>(pumpResult.windowWidth),
                                                 static_cast<uint32_t>(pumpResult.windowHeight));
                uiManager.SetDisplaySize(static_cast<float>(pumpResult.windowWidth),
                                        static_cast<float>(pumpResult.windowHeight));
            }

            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            multiplayerSession.update();
            if (frameIndex == 0) { std::fprintf(stderr, "TRACE: frame0 before sceneManager.update\n"); std::fflush(stderr); }
            sceneManager.update(dt);
            if (frameIndex == 0) { std::fprintf(stderr, "TRACE: frame0 after sceneManager.update\n"); std::fflush(stderr); }
            audioEngine.update(dt);
            if (frameIndex == 0) { std::fprintf(stderr, "TRACE: frame0 before uiManager.Update\n"); std::fflush(stderr); }
            uiManager.Update(dt);
            if (frameIndex == 0) { std::fprintf(stderr, "TRACE: frame0 after uiManager.Update\n"); std::fflush(stderr); }

            vulkanContext->waitForFrameFence(frameIndex);
            uint32_t imageIndex = 0;
            if (vulkanContext->acquireNextImage(frameIndex, imageIndex) == VulkanContext::AcquireStatus::OutOfDate) {
                const VkExtent2D extent = vulkanContext->extent();
                vulkanContext->recreateSwapchain(extent.width, extent.height);
                continue;
            }

            const VkExtent2D extent = vulkanContext->extent();
            VkCommandBuffer commandBuffer = vulkanContext->beginFrameRecording(frameIndex, imageIndex);
            sceneManager.renderWorld(commandBuffer, extent);
            sceneManager.renderOverlay(commandBuffer, extent);
            renderer->BeginFrame(commandBuffer, extent);
            if (frameIndex == 0) { std::fprintf(stderr, "TRACE: frame0 before uiManager.Render\n"); std::fflush(stderr); }
            uiManager.Render();
            if (frameIndex == 0) { std::fprintf(stderr, "TRACE: frame0 after uiManager.Render\n"); std::fflush(stderr); }
            vulkanContext->endFrameRecordingAndSubmit(frameIndex, imageIndex, commandBuffer);
            if (vulkanContext->present(imageIndex)) {
                vulkanContext->recreateSwapchain(extent.width, extent.height);
            }
            frameIndex = (frameIndex + 1) % VulkanContext::kMaxFramesInFlight;
        }

        vulkanContext->waitIdle();
        sceneManager.shutdown();
    }

    renderer.reset();
    vulkanContext.reset();
    SDL_DestroyWindow(window);

    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FATAL: unhandled exception: %s\n", e.what());
    std::fflush(stderr);
    return 1;
  } catch (...) {
    std::fprintf(stderr, "FATAL: unhandled non-std exception\n");
    std::fflush(stderr);
    return 1;
  }
}
