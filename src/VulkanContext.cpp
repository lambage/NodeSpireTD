#include "VulkanContext.hpp"

#include <SFML/Window/Vulkan.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

#define VOLK_IMPLEMENTATION
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

SwapchainData VulkanContext::createEngineSwapchain(vkb::Device& vkbDevice, uint32_t width, uint32_t height, VkSwapchainKHR oldSwapchain) {
    vkb::SwapchainBuilder swapchainBuilder{vkbDevice};
    if (oldSwapchain != VK_NULL_HANDLE) {
        swapchainBuilder.set_old_swapchain(oldSwapchain);
    }

    auto swapchainResult = swapchainBuilder
        .set_desired_extent(width, height)
        .set_desired_min_image_count(2)
        .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .build();

    if (!swapchainResult) {
        throw std::runtime_error("Failed to build Vulkan swapchain: " + swapchainResult.error().message());
    }

    vkb::Swapchain vkbSwapchain = swapchainResult.value();
    auto imagesResult = vkbSwapchain.get_images();
    auto viewsResult = vkbSwapchain.get_image_views();
    if (!imagesResult || !viewsResult) {
        throw std::runtime_error("Failed to query swapchain images or image views.");
    }

    const uint32_t imageCount = static_cast<uint32_t>(imagesResult.value().size());
    if (imageCount < 2) {
        throw std::runtime_error("Swapchain returned fewer than 2 images, which is unsupported by ImGui Vulkan backend.");
    }

    VkSwapchainKHR swapchainHandle = vkbSwapchain.swapchain;
    auto images = imagesResult.value();
    auto imageViews = viewsResult.value();

    return {
        std::move(vkbSwapchain),
        swapchainHandle,
        std::move(images),
        std::move(imageViews),
        imageCount
    };
}

VulkanContext::VulkanContext(sf::Window& window) : window_(window) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Volk.");
    }

    if (!sf::Vulkan::isAvailable()) {
        throw std::runtime_error("Vulkan is not available on this system.");
    }

    currentWidth_ = window_.getSize().x;
    currentHeight_ = window_.getSize().y;

    initializeInstanceAndDevice();
    initializeAllocator();
    initializeDescriptorPool();
    initializeSwapchainAndCommands();
    initializeSyncObjects();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    destroySwapchainDependentResources();

    for (VkFence fence : inFlightFences_) {
        vkDestroyFence(device_, fence, nullptr);
    }
    for (VkSemaphore semaphore : imageAvailableSemaphores_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }

    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }

    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkb::destroy_instance(vkbInstance_);
    }
}

void VulkanContext::initializeInstanceAndDevice() {
    vkb::InstanceBuilder instanceBuilder;
    auto instanceResult = instanceBuilder
        .set_app_name("NodeSpireTD")
        .request_validation_layers(true)
        .require_api_version(1, 3, 0)
        .build();

    if (!instanceResult) {
        throw std::runtime_error("Failed to create Vulkan instance: " + instanceResult.error().message());
    }

    vkbInstance_ = instanceResult.value();
    instance_ = vkbInstance_.instance;
    volkLoadInstance(instance_);

    if (!window_.createVulkanSurface(instance_, surface_)) {
        throw std::runtime_error("Failed to create Vulkan surface from SFML window.");
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkbInstance_};
    auto physicalDeviceResult = selector
        .set_surface(surface_)
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .select();

    if (!physicalDeviceResult) {
        throw std::runtime_error("Failed to select physical device: " + physicalDeviceResult.error().message());
    }

    vkb::DeviceBuilder deviceBuilder{physicalDeviceResult.value()};
    auto deviceResult = deviceBuilder.build();
    if (!deviceResult) {
        throw std::runtime_error("Failed to create logical device: " + deviceResult.error().message());
    }

    vkbDevice_ = deviceResult.value();
    device_ = vkbDevice_.device;
    physicalDevice_ = vkbDevice_.physical_device;

    volkLoadDevice(device_);

    auto queueResult = vkbDevice_.get_queue(vkb::QueueType::graphics);
    if (!queueResult) {
        throw std::runtime_error("Failed to get graphics queue.");
    }
    graphicsQueue_ = queueResult.value();

    auto queueFamilyResult = vkbDevice_.get_queue_index(vkb::QueueType::graphics);
    if (!queueFamilyResult) {
        throw std::runtime_error("Failed to get graphics queue family index.");
    }
    graphicsQueueFamily_ = queueFamilyResult.value();
}

void VulkanContext::initializeAllocator() {
    VmaVulkanFunctions vmaFunctions{};
    vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.pVulkanFunctions = &vmaFunctions;

    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator.");
    }
}

void VulkanContext::initializeDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool.");
    }
}

void VulkanContext::initializeSwapchainAndCommands() {
    swapchainData_ = createEngineSwapchain(vkbDevice_, currentWidth_, currentHeight_);

    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreateInfo.queueFamilyIndex = graphicsQueueFamily_;

    if (vkCreateCommandPool(device_, &poolCreateInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool.");
    }

    commandBuffers_.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers.");
    }
}

void VulkanContext::initializeSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create frame synchronization objects.");
        }
    }

    renderFinishedSemaphores_.resize(swapchainData_.swapchainImageCount);
    for (size_t i = 0; i < swapchainData_.swapchainImageCount; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render-finished semaphore.");
        }
    }

    imagesInFlight_.assign(swapchainData_.swapchainImageCount, VK_NULL_HANDLE);
}

void VulkanContext::destroySwapchainDependentResources() {
    for (VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    renderFinishedSemaphores_.clear();
    imagesInFlight_.clear();

    for (VkImageView view : swapchainData_.imageViews) {
        vkDestroyImageView(device_, view, nullptr);
    }

    if (swapchainData_.swapchain != VK_NULL_HANDLE) {
        vkb::destroy_swapchain(swapchainData_.vkbSwapchain);
    }
}

void VulkanContext::waitForFrameFence(size_t frameIndex) const {
    vkWaitForFences(device_, 1, &inFlightFences_[frameIndex], VK_TRUE, UINT64_MAX);
}

bool VulkanContext::recreateSwapchain(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }

    vkDeviceWaitIdle(device_);

    const std::vector<VkImageView> oldViews = swapchainData_.imageViews;
    const VkSwapchainKHR oldSwapchain = swapchainData_.swapchain;

    swapchainData_ = createEngineSwapchain(vkbDevice_, width, height, oldSwapchain);

    for (VkImageView view : oldViews) {
        vkDestroyImageView(device_, view, nullptr);
    }
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    }

    for (VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinishedSemaphores_.resize(swapchainData_.swapchainImageCount);
    for (size_t i = 0; i < swapchainData_.swapchainImageCount; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to recreate render-finished semaphores.");
        }
    }

    imagesInFlight_.assign(swapchainData_.swapchainImageCount, VK_NULL_HANDLE);

    currentWidth_ = width;
    currentHeight_ = height;

    return true;
}

void VulkanContext::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

VulkanContext::AcquireStatus VulkanContext::acquireNextImage(size_t frameIndex, uint32_t& imageIndex) const {
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device_,
        swapchainData_.swapchain,
        UINT64_MAX,
        imageAvailableSemaphores_[frameIndex],
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return AcquireStatus::OutOfDate;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed.");
    }

    return AcquireStatus::Ok;
}

VkCommandBuffer VulkanContext::beginFrameRecording(size_t frameIndex, uint32_t imageIndex) {
    if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
    }

    imagesInFlight_[imageIndex] = inFlightFences_[frameIndex];
    vkResetFences(device_, 1, &inFlightFences_[frameIndex]);

    VkCommandBuffer commandBuffer = commandBuffers_[frameIndex];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = swapchainData_.images[imageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

    VkClearValue clearColor = {{{0.1f, 0.12f, 0.18f, 1.0f}}};

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = swapchainData_.imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = clearColor;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, {currentWidth_, currentHeight_}};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    return commandBuffer;
}

void VulkanContext::endFrameRecordingAndSubmit(size_t frameIndex, uint32_t imageIndex, VkCommandBuffer commandBuffer) {
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.image = swapchainData_.images[imageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores_[frameIndex];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores_[imageIndex];

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[frameIndex]) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed.");
    }
}

bool VulkanContext::present(uint32_t imageIndex) const {
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainData_.swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        return true;
    }

    if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed.");
    }

    return false;
}
