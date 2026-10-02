#include "lambui_backend/VulkanUiRenderer.hpp"

#include "VulkanContext.hpp"

#include <LambUI/UIFontAtlas.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace lambui_backend {

namespace {

constexpr VkDeviceSize kMinVertexBufferCapacity = 64 * 1024;

VkShaderModule loadSpirv(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open SPIR-V: " + path);
    }
    const auto size = static_cast<size_t>(file.tellg());
    if (size == 0 || size % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V file size: " + path);
    }
    file.seekg(0);
    std::vector<uint32_t> code(size / 4);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = size;
    ci.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + path);
    }
    return module;
}

// Packs a decoded codepoint's UTF-8 continuation bytes the same way
// LambUI's own example renderer does (examples/vulkan/src/VulkanExampleRenderer.cpp).
size_t decodeUtf8(const std::string& text, size_t offset, char32_t& outCodepoint) {
    const auto first = static_cast<unsigned char>(text[offset]);
    size_t consumed = 1;
    char32_t codepoint = first;
    if (first >= 0xC0) {
        int extra = first < 0xE0 ? 1 : (first < 0xF0 ? 2 : 3);
        codepoint = first & ((1 << (6 - extra)) - 1);
        while (extra-- > 0 && offset + consumed < text.size() &&
               (static_cast<unsigned char>(text[offset + consumed]) & 0xC0) == 0x80) {
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[offset + consumed]) & 0x3F);
            ++consumed;
        }
    }
    outCodepoint = codepoint;
    return consumed;
}

} // namespace

VulkanUiRenderer::VulkanUiRenderer(VulkanContext& context) : context_(context) {
    createSamplerAndDescriptorLayout();
    createPipeline();

    // Slot 0 is always a 1x1 opaque white pixel: DrawQuad commands with a
    // null textureHandle (plain colored fills) sample this.
    const uint8_t white[4] = {255, 255, 255, 255};
    uploadTexture(1, 1, white, VK_FORMAT_R8G8B8A8_UNORM);
}

VulkanUiRenderer::~VulkanUiRenderer() {
    context_.waitIdle();
    const VkDevice device = context_.device();

    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context_.allocator(), vertexBuffer_, vertexBufferAllocation_);
    }
    destroyTextures();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
    }
}

void VulkanUiRenderer::destroyTextures() {
    const VkDevice device = context_.device();
    for (auto& texture : textures_) {
        if (texture.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, texture.view, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE) {
            vmaDestroyImage(context_.allocator(), texture.image, texture.allocation);
        }
    }
    textures_.clear();
}

void VulkanUiRenderer::createSamplerAndDescriptorLayout() {
    VkSamplerCreateInfo samplerCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(context_.device(), &samplerCI, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("VulkanUiRenderer: failed to create sampler.");
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCI.bindingCount = 1;
    layoutCI.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(context_.device(), &layoutCI, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("VulkanUiRenderer: failed to create descriptor set layout.");
    }
}

void VulkanUiRenderer::createPipeline() {
    VkShaderModule vert = loadSpirv(context_.device(), "assets/shaders/ui.vert.spv");
    VkShaderModule frag = loadSpirv(context_.device(), "assets/shaders/ui.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribs[3]{};
    attribs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)};
    attribs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)};
    attribs[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color)};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Settings);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(context_.device(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(context_.device(), vert, nullptr);
        vkDestroyShaderModule(context_.device(), frag, nullptr);
        throw std::runtime_error("VulkanUiRenderer: failed to create pipeline layout.");
    }

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // UI draws after the 3D world in the same dynamic-rendering pass and
    // must never be depth-tested/occluded by it.
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.blendEnable = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttach.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blendAttach;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    const VkFormat colorFmt = context_.swapchainColorFormat();
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFmt;
    // Must match the depth attachment format bound in the shared dynamic-rendering
    // pass (WorldRenderer's depth buffer) even though depth test/write are off here.
    renderingInfo.depthAttachmentFormat = context_.depthFormat();

    VkGraphicsPipelineCreateInfo pipelineCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineCI.pNext = &renderingInfo;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = stages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pColorBlendState = &blending;
    pipelineCI.pDynamicState = &dynState;
    pipelineCI.layout = pipelineLayout_;

    const VkResult result = vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline_);

    vkDestroyShaderModule(context_.device(), vert, nullptr);
    vkDestroyShaderModule(context_.device(), frag, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanUiRenderer: failed to create graphics pipeline.");
    }
}

size_t VulkanUiRenderer::uploadTexture(int width, int height, const void* pixels, VkFormat format) {
    if (width <= 0 || height <= 0 || pixels == nullptr) {
        throw std::invalid_argument("VulkanUiRenderer: invalid texture dimensions/pixels.");
    }
    const size_t bytesPerPixel = (format == VK_FORMAT_R8_UNORM) ? 1 : 4;
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * bytesPerPixel;

    VkBufferCreateInfo stagingCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingCI.size = byteSize;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI{};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = nullptr;
    VmaAllocationInfo stagingInfo{};
    if (vmaCreateBuffer(context_.allocator(), &stagingCI, &stagingAllocCI, &staging, &stagingAlloc, &stagingInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("VulkanUiRenderer: failed to create staging buffer.");
    }
    std::memcpy(stagingInfo.pMappedData, pixels, static_cast<size_t>(byteSize));

    Texture texture{};
    VkImageCreateInfo imageCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = format;
    imageCI.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocCI{};
    imageAllocCI.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(context_.allocator(), &imageCI, &imageAllocCI, &texture.image, &texture.allocation, nullptr) !=
        VK_SUCCESS) {
        vmaDestroyBuffer(context_.allocator(), staging, stagingAlloc);
        throw std::runtime_error("VulkanUiRenderer: failed to create texture image.");
    }

    VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbAI.commandPool = context_.commandPool();
    cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(context_.device(), &cbAI, &cb);

    VkCommandBufferBeginInfo cbBI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbBI);

    auto transition = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcAccessMask = src;
        barrier.dstAccessMask = dst;
        barrier.image = texture.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };

    transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = imageCI.extent;
    vkCmdCopyBufferToImage(cb, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cb);
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(context_.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.graphicsQueue());
    vkFreeCommandBuffers(context_.device(), context_.commandPool(), 1, &cb);
    vmaDestroyBuffer(context_.allocator(), staging, stagingAlloc);

    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image = texture.image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(context_.device(), &viewCI, nullptr, &texture.view) != VK_SUCCESS) {
        vmaDestroyImage(context_.allocator(), texture.image, texture.allocation);
        throw std::runtime_error("VulkanUiRenderer: failed to create texture image view.");
    }

    VkDescriptorSetAllocateInfo descAI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descAI.descriptorPool = context_.descriptorPool();
    descAI.descriptorSetCount = 1;
    descAI.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(context_.device(), &descAI, &texture.descriptorSet) != VK_SUCCESS) {
        vkDestroyImageView(context_.device(), texture.view, nullptr);
        vmaDestroyImage(context_.allocator(), texture.image, texture.allocation);
        throw std::runtime_error("VulkanUiRenderer: failed to allocate texture descriptor set.");
    }

    VkDescriptorImageInfo imageInfo{sampler_, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture.descriptorSet;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);

    textures_.push_back(texture);
    return textures_.size() - 1;
}

void* VulkanUiRenderer::UploadTexture(int width, int height, const uint8_t* rgbaPixels) {
    const size_t index = uploadTexture(width, height, rgbaPixels, VK_FORMAT_R8G8B8A8_UNORM);
    return reinterpret_cast<void*>(index);
}

void VulkanUiRenderer::LoadFont(const LambUI::FontAtlas& atlas, void* fontHandle) {
    if (fontHandle == nullptr) {
        throw std::invalid_argument("VulkanUiRenderer::LoadFont: fontHandle must not be null.");
    }
    if (fonts_.count(fontHandle) != 0) {
        throw std::invalid_argument("VulkanUiRenderer::LoadFont: fontHandle already registered.");
    }
    const size_t textureIndex =
        uploadTexture(atlas.GetAtlasWidth(), atlas.GetAtlasHeight(), atlas.GetAtlasPixels().data(), VK_FORMAT_R8_UNORM);
    fonts_.emplace(fontHandle, Font{&atlas, textureIndex});
    spdlog::debug("VulkanUiRenderer: registered font atlas (handle={})", fontHandle);
}

void VulkanUiRenderer::ensureVertexBufferCapacity(VkDeviceSize bytes) {
    if (bytes <= vertexBufferCapacity_) {
        return;
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context_.allocator(), vertexBuffer_, vertexBufferAllocation_);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    vertexBufferCapacity_ = std::max<VkDeviceSize>(bytes, kMinVertexBufferCapacity);

    VkBufferCreateInfo bufferCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCI.size = vertexBufferCapacity_;
    bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(context_.allocator(), &bufferCI, &allocCI, &vertexBuffer_, &vertexBufferAllocation_, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("VulkanUiRenderer: failed to create vertex buffer.");
    }
}

void VulkanUiRenderer::BeginFrame(VkCommandBuffer commandBuffer, VkExtent2D framebufferExtent) {
    activeCommandBuffer_ = commandBuffer;
    framebufferExtent_ = framebufferExtent;
}

void VulkanUiRenderer::SubmitRenderCommands(const std::vector<LambUI::UIRenderCommand>& commands) {
    using LambUI::RenderCommandType;
    using LambUI::UIRect;
    using LambUI::UIRenderCommand;

    if (activeCommandBuffer_ == VK_NULL_HANDLE) {
        return;
    }

    struct Draw {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        size_t textureIndex = 0;
        Settings settings;
        VkRect2D scissor{};
    };

    std::vector<Vertex> vertices;
    std::vector<Draw> draws;
    std::vector<UIRect> clipStack{
        {0.0f, 0.0f, static_cast<float>(framebufferExtent_.width), static_cast<float>(framebufferExtent_.height)}};

    auto currentScissor = [&]() {
        const UIRect& clip = clipStack.back();
        const int left = static_cast<int>(std::floor(clip.x));
        const int top = static_cast<int>(std::floor(clip.y));
        const int right = std::min(static_cast<int>(framebufferExtent_.width), static_cast<int>(std::ceil(clip.x + clip.width)));
        const int bottom =
            std::min(static_cast<int>(framebufferExtent_.height), static_cast<int>(std::ceil(clip.y + clip.height)));
        VkRect2D rect{};
        rect.offset = {std::max(0, left), std::max(0, top)};
        rect.extent = {clip.width > 0.0f ? static_cast<uint32_t>(std::max(0, right - rect.offset.x)) : 0u,
                       clip.height > 0.0f ? static_cast<uint32_t>(std::max(0, bottom - rect.offset.y)) : 0u};
        return rect;
    };

    auto appendQuad = [&](float x, float y, float width, float height, float u0, float v0, float u1, float v1,
                         uint32_t rgba) {
        // UIRenderCommand::color is packed RGBA (R in the high byte); our
        // vertex attribute is VK_FORMAT_R8G8B8A8_UNORM (R in the low byte).
        const uint32_t packed = ((rgba >> 24) & 0xFFu) | (((rgba >> 16) & 0xFFu) << 8) | (((rgba >> 8) & 0xFFu) << 16) |
                                ((rgba & 0xFFu) << 24);
        const Vertex quad[] = {
            {x, y, u0, v0, packed},         {x + width, y, u1, v0, packed},         {x + width, y + height, u1, v1, packed},
            {x, y, u0, v0, packed},         {x + width, y + height, u1, v1, packed}, {x, y + height, u0, v1, packed},
        };
        vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
    };

    for (const UIRenderCommand& item : commands) {
        if (item.type == RenderCommandType::PushScissor) {
            const UIRect& parent = clipStack.back();
            const float left = std::min(parent.x + parent.width, std::max(parent.x, item.x));
            const float top = std::min(parent.y + parent.height, std::max(parent.y, item.y));
            const float right = std::min(parent.x + parent.width, item.x + std::max(0.0f, item.width));
            const float bottom = std::min(parent.y + parent.height, item.y + std::max(0.0f, item.height));
            clipStack.push_back({left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)});
            continue;
        }
        if (item.type == RenderCommandType::PopScissor) {
            if (clipStack.size() > 1) {
                clipStack.pop_back();
            }
            continue;
        }
        if (item.type == RenderCommandType::CustomCallback) {
            if (item.customRenderFunc) {
                item.customRenderFunc({item.x, item.y, item.width, item.height, item.customRenderUserData});
            }
            continue;
        }

        Draw draw;
        draw.firstVertex = static_cast<uint32_t>(vertices.size());
        draw.settings.viewportWidth = static_cast<float>(framebufferExtent_.width);
        draw.settings.viewportHeight = static_cast<float>(framebufferExtent_.height);
        draw.scissor = currentScissor();

        if (item.type == RenderCommandType::DrawQuad) {
            draw.textureIndex = item.textureHandle != nullptr ? reinterpret_cast<uintptr_t>(item.textureHandle) : 0;
            if (draw.textureIndex >= textures_.size()) {
                draw.textureIndex = 0;
            }
            appendQuad(item.x, item.y, item.width, item.height, item.u0, item.v0, item.u1, item.v1, item.color);
        } else if (item.type == RenderCommandType::DrawString) {
            auto fontIt = fonts_.find(item.fontHandle);
            if (fontIt == fonts_.end()) {
                continue;
            }
            const LambUI::FontAtlas& atlas = *fontIt->second.atlas;
            draw.textureIndex = fontIt->second.textureIndex;
            draw.settings.mode = 1.0f;
            draw.settings.distanceScale = atlas.GetPixelDistanceScale() / 255.0f;
            draw.settings.edge = atlas.GetOnEdgeValue() / 255.0f;

            float penX = item.x;
            float penY = item.y + atlas.GetAscent();
            for (size_t offset = 0; offset < item.text.size();) {
                char32_t codepoint = 0;
                offset += decodeUtf8(item.text, offset, codepoint);
                if (codepoint == U'\n') {
                    penX = item.x;
                    penY += atlas.GetLineHeight();
                    continue;
                }
                if (codepoint == U'\r') {
                    continue;
                }
                const auto* glyph = atlas.FindGlyph(codepoint);
                if (!glyph) {
                    glyph = atlas.FindGlyph(U'?');
                }
                if (!glyph) {
                    continue;
                }
                if (glyph->width > 0.0f && glyph->height > 0.0f) {
                    appendQuad(penX + glyph->bearingX, penY + glyph->bearingY, glyph->width, glyph->height, glyph->u0,
                               glyph->v0, glyph->u1, glyph->v1, item.color);
                }
                penX += glyph->advance;
            }
        } else {
            continue;
        }

        draw.vertexCount = static_cast<uint32_t>(vertices.size()) - draw.firstVertex;
        draws.push_back(draw);
    }

    const VkDeviceSize vertexBytes = vertices.size() * sizeof(Vertex);
    if (vertexBytes > 0) {
        ensureVertexBufferCapacity(vertexBytes);
        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(context_.allocator(), vertexBufferAllocation_, &allocInfo);
        std::memcpy(allocInfo.pMappedData, vertices.data(), static_cast<size_t>(vertexBytes));
    }

    const VkViewport viewport{0.0f,
                              0.0f,
                              static_cast<float>(framebufferExtent_.width),
                              static_cast<float>(framebufferExtent_.height),
                              0.0f,
                              1.0f};
    vkCmdSetViewport(activeCommandBuffer_, 0, 1, &viewport);
    vkCmdBindPipeline(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDeviceSize vertexOffset = 0;
    if (!vertices.empty()) {
        vkCmdBindVertexBuffers(activeCommandBuffer_, 0, 1, &vertexBuffer_, &vertexOffset);
    }

    for (const Draw& draw : draws) {
        if (draw.vertexCount == 0 || draw.scissor.extent.width == 0 || draw.scissor.extent.height == 0) {
            continue;
        }
        vkCmdSetScissor(activeCommandBuffer_, 0, 1, &draw.scissor);
        vkCmdBindDescriptorSets(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                &textures_[draw.textureIndex].descriptorSet, 0, nullptr);
        vkCmdPushConstants(activeCommandBuffer_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Settings), &draw.settings);
        vkCmdDraw(activeCommandBuffer_, draw.vertexCount, 1, draw.firstVertex, 0);
    }
}

} // namespace lambui_backend
