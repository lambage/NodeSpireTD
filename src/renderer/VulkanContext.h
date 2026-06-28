#pragma once

// volk must be included before any Vulkan header.
#include <VkBootstrap.h>
#include <volk.h>


// VMA needs VK_NO_PROTOTYPES satisfied (provided by build definitions).
// These macros must match those used in vma_impl.cpp.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <GLFW/glfw3.h>
#include <array>
#include <cstdint>
#include <vector>
#include <vk_mem_alloc.h>


namespace NST {

// ---------------------------------------------------------------
// VulkanContext
//
// Owns all Vulkan objects required to drive the main render loop:
//   - Instance / surface / physical + logical device (via vk-bootstrap)
//   - Swapchain and its image views
//   - A single render pass used for both the game and ImGui overlays
//   - Framebuffers matching the swapchain images
//   - Command pool and per-frame command buffers
//   - Per-frame synchronisation primitives
//   - A VMA allocator (for GPU memory management)
//   - A dedicated ImGui descriptor pool
//
// Call sequence:
//   VulkanContext ctx;
//   ctx.init(window);
//   while (running) {
//       auto [cmd, fb] = ctx.beginFrame();
//       // record draw calls …
//       ctx.endFrame(cmd);
//   }
//   ctx.destroy();
// ---------------------------------------------------------------

static constexpr uint32_t k_maxFramesInFlight = 2;

struct FrameData {
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkSemaphore imageAvailable{VK_NULL_HANDLE};
    VkSemaphore renderFinished{VK_NULL_HANDLE};
    VkFence inFlight{VK_NULL_HANDLE};
};

struct BeginFrameResult {
    VkCommandBuffer cmd{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    uint32_t imageIndex{0};
    bool valid{false};
};

class VulkanContext {
  public:
    bool init(GLFWwindow* window);
    void destroy();

    // Waits for the frame fence, acquires the next swapchain image, and
    // begins a command buffer recording session.
    BeginFrameResult beginFrame();

    // Ends recording, submits to the graphics queue, and presents.
    void endFrame(VkCommandBuffer cmd, uint32_t imageIndex);

    // Recreate swapchain (called after window resize).
    bool recreateSwapchain();

    // ----- Accessors used by ImGuiLayer ----------------------------
    [[nodiscard]] VkInstance instance() const noexcept {
        return m_instance;
    }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept {
        return m_physDevice;
    }
    [[nodiscard]] VkDevice device() const noexcept {
        return m_device;
    }
    [[nodiscard]] VkQueue graphicsQueue() const noexcept {
        return m_graphicsQueue;
    }
    [[nodiscard]] uint32_t graphicsFamily() const noexcept {
        return m_graphicsFamily;
    }
    [[nodiscard]] VkRenderPass renderPass() const noexcept {
        return m_renderPass;
    }
    [[nodiscard]] VkDescriptorPool imguiPool() const noexcept {
        return m_imguiPool;
    }
    [[nodiscard]] uint32_t swapchainImageCount() const noexcept;
    [[nodiscard]] VkExtent2D swapchainExtent() const noexcept {
        return m_swapExtent;
    }
    [[nodiscard]] VmaAllocator allocator() const noexcept {
        return m_allocator;
    }

    [[nodiscard]] GLFWwindow* window() const noexcept {
        return m_window;
    }

  private:
    bool createRenderPass();
    bool createFramebuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    void cleanupSwapchain();

    GLFWwindow* m_window{nullptr};

    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkPhysicalDevice m_physDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    uint32_t m_graphicsFamily{0};

    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_swapFormat{};
    VkExtent2D m_swapExtent{};
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
    std::vector<VkFramebuffer> m_framebuffers;

    VkRenderPass m_renderPass{VK_NULL_HANDLE};
    VkDescriptorPool m_imguiPool{VK_NULL_HANDLE};

    VmaAllocator m_allocator{VK_NULL_HANDLE};

    std::array<FrameData, k_maxFramesInFlight> m_frames{};
    uint32_t m_currentFrame{0};

    vkb::Instance m_vkbInstance;
    vkb::Device m_vkbDevice;
    vkb::Swapchain m_vkbSwapchain;
};

} // namespace NST
