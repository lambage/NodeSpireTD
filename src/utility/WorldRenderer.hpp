#pragma once

#include "VulkanContext.hpp"

#include <atomic>
#include <filesystem>
#include <glm/glm.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <volk.h>

struct WorldVertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{};
};

struct WorldTexture {
    VkImage       image      = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView   view       = VK_NULL_HANDLE;
    bool valid() const { return view != VK_NULL_HANDLE; }
};

struct WorldMesh {
    VkBuffer        vertexBuffer  = VK_NULL_HANDLE;
    VmaAllocation   vertexAlloc   = nullptr;
    VkBuffer        indexBuffer   = VK_NULL_HANDLE;
    VmaAllocation   indexAlloc    = nullptr;
    uint32_t        indexCount    = 0;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    glm::mat4       modelTransform{1.0f};
};

class WorldRenderer {
  public:
    explicit WorldRenderer(VulkanContext& ctx);
    ~WorldRenderer();

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Non-blocking. Starts background parse+decode; GPU uploads happen via tickLoad().
    void beginLoad(const std::filesystem::path& assetPath);

    // Call once per frame from the main thread until isLoaded() or loadFailed().
    // Performs one GPU upload step (one texture or all meshes).
    void tickLoad();

    bool isLoaded()      const { return loaded_; }
    bool loadFailed()    const { return loadFailed_; }
    float loadProgress() const { return progress_.load(std::memory_order_relaxed); }
    std::string loadActivity() const;
    const std::string& statusMessage() const { return status_; }

    int meshCount()     const { return static_cast<int>(meshes_.size()); }
    int totalVertices() const { return totalVertices_; }
    int totalIndices()  const { return totalIndices_; }

    void render(VkCommandBuffer cmd, VkExtent2D extent, const glm::mat4& view);
    void release();

  private:
    VulkanContext& ctx_;

    std::vector<WorldMesh> meshes_;
    bool        loaded_        = false;
    bool        loadFailed_    = false;
    std::string status_;
    int         totalVertices_ = 0;
    int         totalIndices_  = 0;

    // Pipeline
    VkDescriptorSetLayout textureDescLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_    = VK_NULL_HANDLE;
    VkPipeline            pipeline_          = VK_NULL_HANDLE;

    // Textures
    VkSampler        sampler_         = VK_NULL_HANDLE;
    VkDescriptorPool ownDescPool_     = VK_NULL_HANDLE;
    VkDescriptorSet  fallbackDescSet_ = VK_NULL_HANDLE;
    WorldTexture     fallbackTexture_;
    std::unordered_map<std::size_t, WorldTexture>    textureCache_;
    std::unordered_map<std::size_t, VkDescriptorSet> texDescSetCache_;

    // ── Async loading ─────────────────────────────────────────────────────
    struct StagedMesh {
        std::vector<WorldVertex> vertices;
        std::vector<uint32_t>   indices;
        std::size_t             imageIndex = SIZE_MAX;
        glm::mat4               modelTransform{1.0f};
    };
    struct StagedTexture {
        std::size_t          imageIndex;
        std::string          displayName;
        std::vector<uint8_t> pixels; // RGBA decoded on background thread
        uint32_t             width  = 0;
        uint32_t             height = 0;
    };

    std::thread        loadThread_;
    std::atomic<bool>  cpuDone_{false};
    std::atomic<bool>  cpuFailed_{false};
    std::atomic<bool>  cancelLoad_{false};
    std::atomic<float> progress_{0.0f};
    mutable std::mutex activityMtx_;
    std::string        activityStr_{"Idle"};

    // Written by background thread before cpuDone_, read by main thread after.
    std::vector<StagedMesh>    stagedMeshes_;
    std::vector<StagedTexture> stagedTextures_;
    std::string                failReason_;

    // GPU upload cursors (main thread only)
    std::size_t              gpuMeshCursor_  = 0;
    std::size_t              gpuTexCursor_   = 0;
    bool                     gpuDescsDone_   = false;
    bool                     gpuPipeDone_    = false;
    std::vector<std::size_t> meshImgIdx_;   // parallel to meshes_
    // ─────────────────────────────────────────────────────────────────────

    void setActivity(float progress, std::string activity);
    void backgroundLoad(std::filesystem::path assetPath);

    void buildPipeline();
    void createSamplerLayoutAndPool();
    WorldTexture    uploadRGBAImage(const uint8_t* pixels, uint32_t w, uint32_t h);
    VkDescriptorSet makeTextureDescSet(VkImageView view);
    void            createFallbackTexture();

    VkShaderModule loadSpirv(const std::filesystem::path& path) const;
    WorldMesh uploadMesh(const std::vector<WorldVertex>& verts, const std::vector<uint32_t>& idx);
    VkBuffer  uploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocation& alloc);
};
