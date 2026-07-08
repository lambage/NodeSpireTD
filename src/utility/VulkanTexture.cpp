#include "VulkanTexture.hpp"

#include <SFML/Graphics/Image.hpp>
#include <imgui_impl_vulkan.h>

#include <cstring>
#include <stdexcept>
#include <utility>

#include <vk_mem_alloc.h>

namespace {

VkCommandBuffer beginSingleUseCommandBuffer(VkDevice device, VkCommandPool commandPool) {
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer for texture upload.");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer for texture upload.");
    }

    return commandBuffer;
}

void endSingleUseCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool commandPool, VkCommandBuffer commandBuffer) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to end command buffer for texture upload.");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit command buffer for texture upload.");
    }

    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

} // namespace

VulkanTexture::VulkanTexture(VkDevice device, VmaAllocator allocator, VkCommandPool commandPool, VkQueue graphicsQueue)
    : device_(device),
      allocator_(allocator),
      commandPool_(commandPool),
      graphicsQueue_(graphicsQueue) {}

VulkanTexture::~VulkanTexture() {
    destroyInternal();
}

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept {
    *this = std::move(other);
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroyInternal();

    device_ = other.device_;
    allocator_ = other.allocator_;
    commandPool_ = other.commandPool_;
    graphicsQueue_ = other.graphicsQueue_;
    image_ = other.image_;
    allocation_ = other.allocation_;
    imageView_ = other.imageView_;
    descriptorSet_ = other.descriptorSet_;
    width_ = other.width_;
    height_ = other.height_;

    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = nullptr;
    other.commandPool_ = VK_NULL_HANDLE;
    other.graphicsQueue_ = VK_NULL_HANDLE;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.imageView_ = VK_NULL_HANDLE;
    other.descriptorSet_ = VK_NULL_HANDLE;
    other.width_ = 0;
    other.height_ = 0;

    return *this;
}

bool VulkanTexture::loadFromFile(const std::filesystem::path& texturePath) {
    if (device_ == VK_NULL_HANDLE || allocator_ == nullptr || commandPool_ == VK_NULL_HANDLE || graphicsQueue_ == VK_NULL_HANDLE) {
        return false;
    }

    destroyInternal();

    sf::Image image;
    if (!image.loadFromFile(texturePath)) {
        return false;
    }

    const sf::Vector2u imageSize = image.getSize();
    if (imageSize.x == 0 || imageSize.y == 0) {
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(imageSize.x) * static_cast<size_t>(imageSize.y) * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    VmaAllocationInfo stagingAllocationInfo{};

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = pixelBytes;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocCreateInfo{};
    stagingAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(allocator_, &stagingBufferInfo, &stagingAllocCreateInfo, &stagingBuffer, &stagingAllocation, &stagingAllocationInfo) != VK_SUCCESS) {
        return false;
    }

    std::memcpy(stagingAllocationInfo.pMappedData, image.getPixelsPtr(), pixelBytes);

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.extent = {imageSize.x, imageSize.y, 1};
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocCreateInfo{};
    imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(allocator_, &imageCreateInfo, &imageAllocCreateInfo, &image_, &allocation_, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    try {
        commandBuffer = beginSingleUseCommandBuffer(device_, commandPool_);

        VkImageMemoryBarrier transferBarrier{};
        transferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        transferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        transferBarrier.srcAccessMask = 0;
        transferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferBarrier.image = image_;
        transferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        transferBarrier.subresourceRange.baseMipLevel = 0;
        transferBarrier.subresourceRange.levelCount = 1;
        transferBarrier.subresourceRange.baseArrayLayer = 0;
        transferBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &transferBarrier);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = {imageSize.x, imageSize.y, 1};

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier shaderReadBarrier{};
        shaderReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        shaderReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        shaderReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shaderReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        shaderReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        shaderReadBarrier.image = image_;
        shaderReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        shaderReadBarrier.subresourceRange.baseMipLevel = 0;
        shaderReadBarrier.subresourceRange.levelCount = 1;
        shaderReadBarrier.subresourceRange.baseArrayLayer = 0;
        shaderReadBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &shaderReadBarrier);

        endSingleUseCommandBuffer(device_, graphicsQueue_, commandPool_, commandBuffer);
    } catch (...) {
        if (commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        }

        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        destroyInternal();
        return false;
    }

    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = image_;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &imageViewCreateInfo, nullptr, &imageView_) != VK_SUCCESS) {
        destroyInternal();
        return false;
    }

    descriptorSet_ = ImGui_ImplVulkan_AddTexture(imageView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (descriptorSet_ == VK_NULL_HANDLE) {
        destroyInternal();
        return false;
    }

    width_ = imageSize.x;
    height_ = imageSize.y;
    return true;
}

void VulkanTexture::reset() {
    destroyInternal();
}

bool VulkanTexture::isValid() const {
    return descriptorSet_ != VK_NULL_HANDLE && width_ > 0 && height_ > 0;
}

uint32_t VulkanTexture::width() const {
    return width_;
}

uint32_t VulkanTexture::height() const {
    return height_;
}

ImTextureRef VulkanTexture::textureRef() const {
    return ImTextureRef(descriptorSet_);
}

void VulkanTexture::destroyInternal() {
    if (descriptorSet_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(descriptorSet_);
        descriptorSet_ = VK_NULL_HANDLE;
    }

    if (imageView_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }

    if (image_ != VK_NULL_HANDLE && allocator_ != nullptr) {
        vmaDestroyImage(allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
}
