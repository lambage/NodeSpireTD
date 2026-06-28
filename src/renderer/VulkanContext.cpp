#include "renderer/VulkanContext.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace NST {

// ---------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

// ---------------------------------------------------------------
bool VulkanContext::init(GLFWwindow* window) {
    m_window = window;

    // --- 1. Bootstrap: instance ---
    vkb::InstanceBuilder builder;
    auto instResult = builder.set_app_name("NodeSpireTD")
                          .request_validation_layers(true)
                          .use_default_debug_messenger()
                          .require_api_version(1, 3, 0)
                          .build();

    if (!instResult) {
        std::fprintf(stderr, "[VulkanContext] Instance creation failed: %s\n", instResult.error().message().c_str());
        return false;
    }

    m_vkbInstance = instResult.value();
    m_instance = m_vkbInstance.instance;
    m_debugMessenger = m_vkbInstance.debug_messenger;

    // Must call after instance is created so volk loads all symbols.
    volkLoadInstance(m_instance);

    // --- 2. Surface ---
    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanContext] Failed to create window surface.\n");
        return false;
    }

    // --- 3. Physical device ---
    vkb::PhysicalDeviceSelector selector{m_vkbInstance};
    auto physResult = selector.set_minimum_version(1, 3).set_surface(m_surface).select();

    if (!physResult) {
        std::fprintf(stderr, "[VulkanContext] Physical device selection failed: %s\n",
                     physResult.error().message().c_str());
        return false;
    }
    m_physDevice = physResult.value().physical_device;

    // --- 4. Logical device ---
    vkb::DeviceBuilder devBuilder{physResult.value()};
    auto devResult = devBuilder.build();
    if (!devResult) {
        std::fprintf(stderr, "[VulkanContext] Device creation failed: %s\n", devResult.error().message().c_str());
        return false;
    }
    m_vkbDevice = devResult.value();
    m_device = m_vkbDevice.device;

    // Must call after device is created so volk loads device-level symbols.
    volkLoadDevice(m_device);

    auto queueResult = m_vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!queueResult) {
        std::fprintf(stderr, "[VulkanContext] Failed to retrieve graphics queue.\n");
        return false;
    }
    m_graphicsQueue = queueResult.value();
    m_graphicsFamily = m_vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // --- 5. VMA allocator ---
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.physicalDevice = m_physDevice;
    allocInfo.device = m_device;
    allocInfo.instance = m_instance;
    allocInfo.pVulkanFunctions = &vulkanFunctions;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    if (vmaCreateAllocator(&allocInfo, &m_allocator) != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanContext] VMA allocator creation failed.\n");
        return false;
    }

    // --- 6. Swapchain ---
    if (!recreateSwapchain()) {
        return false;
    }

    // --- 7. Render pass ---
    if (!createRenderPass()) {
        return false;
    }

    // --- 8. Framebuffers ---
    if (!createFramebuffers()) {
        return false;
    }

    // --- 9. Sync objects ---
    if (!createSyncObjects()) {
        return false;
    }

    // --- 10. ImGui descriptor pool ---
    if (!createDescriptorPool()) {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------
bool VulkanContext::recreateSwapchain() {
    if (m_device) {
        vkDeviceWaitIdle(m_device);
    }

    cleanupSwapchain();

    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);

    vkb::SwapchainBuilder swapBuilder{m_vkbDevice};
    auto swapResult = swapBuilder.use_default_format_selection()
                          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                          .set_desired_extent(static_cast<uint32_t>(w), static_cast<uint32_t>(h))
                          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                          .build();

    if (!swapResult) {
        std::fprintf(stderr, "[VulkanContext] Swapchain creation failed: %s\n", swapResult.error().message().c_str());
        return false;
    }

    m_vkbSwapchain = swapResult.value();
    m_swapchain = m_vkbSwapchain.swapchain;
    m_swapFormat = m_vkbSwapchain.image_format;
    m_swapExtent = m_vkbSwapchain.extent;
    m_swapImages = m_vkbSwapchain.get_images().value();
    m_swapImageViews = m_vkbSwapchain.get_image_views().value();

    if (m_renderPass) {
        createFramebuffers();
    }
    return true;
}

// ---------------------------------------------------------------
bool VulkanContext::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    return vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass) == VK_SUCCESS;
}

// ---------------------------------------------------------------
bool VulkanContext::createFramebuffers() {
    m_framebuffers.resize(m_swapImageViews.size());

    for (std::size_t i = 0; i < m_swapImageViews.size(); ++i) {
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &m_swapImageViews[i];
        fbInfo.width = m_swapExtent.width;
        fbInfo.height = m_swapExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------
bool VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandPoolCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = m_graphicsFamily;

    for (auto& frame : m_frames) {
        if (vkCreateCommandPool(m_device, &cpInfo, nullptr, &frame.commandPool) != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbInfo.commandPool = frame.commandPool;
        cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_device, &cbInfo, &frame.commandBuffer) != VK_SUCCESS) {
            return false;
        }

        if (vkCreateSemaphore(m_device, &semInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semInfo, nullptr, &frame.renderFinished) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------
bool VulkanContext::createDescriptorPool() {
    // Generous pool for ImGui – one pool covering all descriptor types.
    std::array<VkDescriptorPoolSize, 11> poolSizes{{
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
    }};

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    return vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_imguiPool) == VK_SUCCESS;
}

// ---------------------------------------------------------------
BeginFrameResult VulkanContext::beginFrame() {
    auto& frame = m_frames[m_currentFrame];

    vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result =
        vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return {};
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return {};
    }

    vkResetFences(m_device, 1, &frame.inFlight);
    vkResetCommandBuffer(frame.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

    return {frame.commandBuffer, m_framebuffers[imageIndex], imageIndex, true};
}

// ---------------------------------------------------------------
void VulkanContext::endFrame(VkCommandBuffer cmd, uint32_t imageIndex) {
    vkEndCommandBuffer(cmd);

    auto& frame = m_frames[m_currentFrame];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frame.renderFinished;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inFlight);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }

    m_currentFrame = (m_currentFrame + 1) % k_maxFramesInFlight;
}

// ---------------------------------------------------------------
uint32_t VulkanContext::swapchainImageCount() const noexcept {
    return static_cast<uint32_t>(m_swapImages.size());
}

// ---------------------------------------------------------------
void VulkanContext::cleanupSwapchain() {
    for (auto fb : m_framebuffers) {
        if (fb) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
        }
    }
    m_framebuffers.clear();

    for (auto iv : m_swapImageViews) {
        if (iv) {
            vkDestroyImageView(m_device, iv, nullptr);
        }
    }
    m_swapImageViews.clear();
    m_swapImages.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------
void VulkanContext::destroy() {
    if (!m_device) {
        return;
    }

    vkDeviceWaitIdle(m_device);

    for (auto& frame : m_frames) {
        if (frame.commandPool) {
            vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
        }
        if (frame.imageAvailable) {
            vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
        }
        if (frame.renderFinished) {
            vkDestroySemaphore(m_device, frame.renderFinished, nullptr);
        }
        if (frame.inFlight) {
            vkDestroyFence(m_device, frame.inFlight, nullptr);
        }
    }

    if (m_imguiPool) {
        vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
    }

    cleanupSwapchain();

    if (m_renderPass) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    }

    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
    }

    vkb::destroy_device(m_vkbDevice);

    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }

    vkb::destroy_instance(m_vkbInstance);
}

} // namespace NST
