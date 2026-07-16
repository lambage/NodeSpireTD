#pragma once

#include <SFML/Window.hpp>
#include <VkBootstrap.h>
#include <cstdint>
#include <vector>
#include <vk_mem_alloc.h>
#include <volk.h>

struct SwapchainData {
    vkb::Swapchain vkbSwapchain;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    uint32_t swapchainImageCount = 0;
};

class VulkanContext {
  public:
    static constexpr size_t kMaxFramesInFlight = 2;

    explicit VulkanContext(sf::Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void waitForFrameFence(size_t frameIndex) const;
    bool recreateSwapchain(uint32_t width, uint32_t height);
    void waitIdle() const;

    enum class AcquireStatus {
        Ok,
        OutOfDate
    };

    AcquireStatus acquireNextImage(size_t frameIndex, uint32_t& imageIndex) const;
    VkCommandBuffer beginFrameRecording(size_t frameIndex, uint32_t imageIndex);
    void endFrameRecordingAndSubmit(size_t frameIndex, uint32_t imageIndex, VkCommandBuffer commandBuffer);
    bool present(uint32_t imageIndex) const;

    VkInstance instance() const {
        return instance_;
    }
    VkPhysicalDevice physicalDevice() const {
        return physicalDevice_;
    }
    VkDevice device() const {
        return device_;
    }
    VkQueue graphicsQueue() const {
        return graphicsQueue_;
    }
    uint32_t graphicsQueueFamily() const {
        return graphicsQueueFamily_;
    }
    VkDescriptorPool descriptorPool() const {
        return descriptorPool_;
    }
    VmaAllocator allocator() const {
        return allocator_;
    }
    VkCommandPool commandPool() const {
        return commandPool_;
    }
    uint32_t swapchainImageCount() const {
        return swapchainData_.swapchainImageCount;
    }
    VkExtent2D extent() const {
        return {currentWidth_, currentHeight_};
    }
    VkFormat swapchainColorFormat() const {
        return VK_FORMAT_B8G8R8A8_UNORM;
    }
    VkFormat depthFormat() const {
        return kDepthFormat;
    }
    VkImageView depthImageView() const {
        return depthImageView_;
    }

  private:
    static SwapchainData createEngineSwapchain(vkb::Device& vkbDevice, uint32_t width, uint32_t height,
                                               VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

    void initializeInstanceAndDevice();
    void initializeAllocator();
    void initializeDescriptorPool();
    void initializeSwapchainAndCommands();
    void initializeSyncObjects();
    void createDepthResources();
    void destroySwapchainDependentResources();

    sf::Window& window_;

    vkb::Instance vkbInstance_;
    VkInstance instance_ = VK_NULL_HANDLE;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    vkb::Device vkbDevice_;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    SwapchainData swapchainData_;
    uint32_t currentWidth_ = 1280;
    uint32_t currentHeight_ = 720;

    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    mutable std::vector<VkFence> imagesInFlight_;
};
