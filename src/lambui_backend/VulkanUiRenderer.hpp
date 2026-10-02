#pragma once

// LambUI::IRenderer implementation reconciled with the game-owned
// VulkanContext (device/allocator/descriptor pool/command pool are all
// borrowed, not owned here -- unlike LambUI's self-contained
// examples/vulkan/src/VulkanExampleRenderer.cpp, which this is adapted
// from). Records draw calls directly into the command buffer/dynamic-
// rendering pass that VulkanContext::beginFrameRecording already opened
// for the frame; never opens its own render pass or swapchain.
//
// Vertex layout, the push-constant Settings struct, and the SDF
// text-coverage shading in assets/shaders/ui.vert/.frag are ported
// verbatim from LambUI's reference Vulkan renderer so this stays faithful
// to the library's own (fact-checked) conventions.

#include <LambUI/IRenderer.h>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class VulkanContext;

namespace LambUI {
class FontAtlas;
}

namespace lambui_backend {

class VulkanUiRenderer final : public LambUI::IRenderer {
  public:
    explicit VulkanUiRenderer(VulkanContext& context);
    ~VulkanUiRenderer() override;

    VulkanUiRenderer(const VulkanUiRenderer&) = delete;
    VulkanUiRenderer& operator=(const VulkanUiRenderer&) = delete;

    // Uploads an RGBA8 texture, returning an opaque handle usable as
    // LambUI::UIRenderCommand::textureHandle / UITextureWidget::SetTexture.
    void* UploadTexture(int width, int height, const uint8_t* rgbaPixels);

    // Uploads a font atlas's SDF bitmap and registers it under fontHandle.
    // fontHandle must match the handle registered with the paired
    // LambUI::FontAtlasTextMeasurer (see LambUiFontLoader) so text layout
    // and glyph rendering agree on metrics.
    void LoadFont(const LambUI::FontAtlas& atlas, void* fontHandle);

    // Must be called once per frame after VulkanContext::beginFrameRecording
    // (so a dynamic-rendering pass targeting the swapchain is already
    // active) and before LambUI::UIManager::Render() triggers
    // SubmitRenderCommands().
    void BeginFrame(VkCommandBuffer commandBuffer, VkExtent2D framebufferExtent);

    void SubmitRenderCommands(const std::vector<LambUI::UIRenderCommand>& commands) override;

  private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        uint32_t color = 0xFFFFFFFFu;
    };

    struct Settings {
        float viewportWidth = 1.0f;
        float viewportHeight = 1.0f;
        float distanceScale = 0.0f;
        float mode = 0.0f;
        float edge = 0.5f;
    };

    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    struct Font {
        const LambUI::FontAtlas* atlas = nullptr;
        size_t textureIndex = 0;
    };

    void createSamplerAndDescriptorLayout();
    void createPipeline();
    void ensureVertexBufferCapacity(VkDeviceSize bytes);
    size_t uploadTexture(int width, int height, const void* pixels, VkFormat format);
    void destroyTextures();

    VulkanContext& context_;

    VkCommandBuffer activeCommandBuffer_ = VK_NULL_HANDLE;
    VkExtent2D framebufferExtent_{};

    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation_ = nullptr;
    VkDeviceSize vertexBufferCapacity_ = 0;

    std::vector<Texture> textures_;
    std::unordered_map<void*, Font> fonts_;
};

} // namespace lambui_backend
