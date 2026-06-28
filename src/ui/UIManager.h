#pragma once

#include <chrono>
#include <cstdint>

// volk must be included before Vulkan headers.
#include <volk.h>

// VMA needs the same compile-time defines used by VulkanContext.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace NST {

// ---------------------------------------------------------------
// MenuState – top-level UI state machine
// ---------------------------------------------------------------
enum class MenuState : uint8_t {
  Splash,
    MainMenu,
    Gameplay,
    Settings,
    Exit,
};

class Application; // forward declaration

// ---------------------------------------------------------------
// UIManager
//
// Immediate-mode state machine that drives all Dear ImGui panels.
// Each tick it inspects m_state and delegates to the appropriate
// render function.  State transitions set m_state directly.
// ---------------------------------------------------------------
class UIManager {
  public:
    explicit UIManager(Application& app);
    ~UIManager();

    /// Called once per frame after ImGuiLayer::beginFrame().
    void render(int width, int height);

    [[nodiscard]] MenuState state() const noexcept {
        return m_state;
    }

  private:
    void renderSplash(int width, int height);
    void renderMainMenu(int width, int height);
    void renderGameplay(int width, int height);
    void renderSettings(int width, int height);

    bool loadSplashTexture();
    void destroySplashTexture();

    Application& m_app;
    MenuState m_state{MenuState::Splash};

    std::chrono::steady_clock::time_point m_splashStart{};
    bool m_splashTextureLoaded{false};
    int m_splashWidth{0};
    int m_splashHeight{0};

    VkImage m_splashImage{VK_NULL_HANDLE};
    VmaAllocation m_splashAllocation{VK_NULL_HANDLE};
    VkImageView m_splashImageView{VK_NULL_HANDLE};
    VkSampler m_splashSampler{VK_NULL_HANDLE};
    VkDescriptorSet m_splashDescriptorSet{VK_NULL_HANDLE};
};

} // namespace NST
