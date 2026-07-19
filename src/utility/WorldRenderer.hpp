#pragma once

#include "VulkanContext.hpp"
#include "utility/AnimatedEntityInstanceSet.hpp"
#include "utility/WorldAssetLoader.hpp"
#include "utility/WorldGeometryTypes.hpp"
#include "utility/TemplateAnimationDebugInfo.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <volk.h>

struct lua_State;
class TemplateAnimator;

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
    glm::mat4       groupRootTransform{1.0f};
    int             templatePrototypeIndex = -1;
    int             sourceNodeIndex = -1;
    int             sourceSkinIndex = -1;
    glm::vec3       localBoundsCenter{0.0f, 0.0f, 0.0f};
    float           localBoundsRadius = 0.5f;
    std::string     debugGroup;
    std::string     debugLabel;
};

struct WorldPickHit {
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 worldPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 worldNormal{0.0f, 1.0f, 0.0f};
    std::string group;
    std::string label;
    int meshIndex = -1;
    int nodeIndex = -1;
    int skinIndex = -1;
    int instanceIndex = -1;
};

struct WorldPickOptions {
    float staticRadiusScale = 1.0f;
    float staticRadiusPadding = 0.0f;
    float staticMinRadius = 0.05f;
    float dynamicRadiusScale = 1.45f;
    float dynamicRadiusPadding = 0.35f;
    float dynamicMinRadius = 0.65f;
};

struct WorldPickDebugSphere {
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    int instanceIndex = -1;
    std::string group;
    std::string label;
};

class WorldRenderer {
  public:
    explicit WorldRenderer(lua_State* L, VulkanContext& ctx);
    ~WorldRenderer();

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Non-blocking. Starts background parse+decode; GPU uploads happen via tickLoad().
    void beginLoad(const std::filesystem::path& assetPath, const WorldAssetSpec& spec = {});

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
    bool hasTemplateAnimation() const;
    const std::string& templateAnimationName() const;
    int templateAnimationClipCount() const;
    int activeTemplateAnimationClipIndex() const;
    std::vector<std::string> templateAnimationClipNames() const;
    bool setActiveTemplateAnimationClipByIndex(int clipIndex);
    bool setActiveTemplateAnimationClipByName(const std::string& clipName);
    void setCompositeTemplateAnimationMode(bool enabled);
    bool compositeTemplateAnimationMode() const;

    bool hasEnemyAnimation() const { return hasTemplateAnimation(); }
    const std::string& enemyAnimationName() const { return templateAnimationName(); }
    int enemyAnimationClipCount() const { return templateAnimationClipCount(); }
    int activeEnemyAnimationClipIndex() const { return activeTemplateAnimationClipIndex(); }
    std::vector<std::string> enemyAnimationClipNames() const { return templateAnimationClipNames(); }
    bool setActiveEnemyAnimationClipByIndex(int clipIndex) { return setActiveTemplateAnimationClipByIndex(clipIndex); }
    bool setActiveEnemyAnimationClipByName(const std::string& clipName) { return setActiveTemplateAnimationClipByName(clipName); }
    void setPlayAllEnemyAnimationClips(bool enabled) { setCompositeTemplateAnimationMode(enabled); }
    bool playAllEnemyAnimationClips() const { return compositeTemplateAnimationMode(); }

    void setAnimatedEntityInstanceTransforms(const std::vector<glm::mat4>& transforms);
    void setTowerInstanceTransforms(const std::vector<AnimatedEntityInstanceSet::Instance>& instances);
    bool setWorldModelTransformByDebugGroup(const std::string& debugGroup, const glm::mat4& transform);
    void setHighlightedInstances(int hoveredInstanceIndex, int selectedInstanceIndex);
    void setEnemyInstanceTransforms(const std::vector<glm::mat4>& transforms) { setAnimatedEntityInstanceTransforms(transforms); }
    const std::vector<glm::vec3>& routePoints() const { return routePoints_; }
    bool hasAnimatedEntityTemplate() const { return !enemyTemplateMeshes_.empty(); }
    bool hasEnemyTemplate() const { return hasAnimatedEntityTemplate(); }
    bool pickModel(const glm::vec3& rayOrigin,
                   const glm::vec3& rayDir,
                   WorldPickHit& outHit,
                   const WorldPickOptions& options = {}) const;
    std::vector<WorldPickDebugSphere> buildDynamicPickDebugSpheres(const WorldPickOptions& options = {}) const;
    const EnemyAnimationDebugInfo& templateAnimationDebugInfo() const;
    const EnemyAnimationDebugInfo& enemyAnimationDebugInfo() const { return templateAnimationDebugInfo(); }

    void render(VkCommandBuffer cmd, VkExtent2D extent, const glm::mat4& view);
    void release();

  private:
    lua_State* L_;
    VulkanContext& ctx_;

    std::vector<WorldMesh> meshes_;
    std::vector<WorldMesh> enemyTemplateMeshes_;
    std::vector<WorldMesh> towerTemplateMeshes_;
    bool        loaded_        = false;
    bool        loadFailed_    = false;
    std::string status_;
    int         totalVertices_ = 0;
    int         totalIndices_  = 0;

    // Pipeline
    VkDescriptorSetLayout textureDescLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_    = VK_NULL_HANDLE;
    VkPipeline            pipeline_          = VK_NULL_HANDLE;
    VkPipelineLayout      highlightPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline            highlightPipeline_ = VK_NULL_HANDLE;

    // Textures
    VkSampler        sampler_         = VK_NULL_HANDLE;
    VkDescriptorPool ownDescPool_     = VK_NULL_HANDLE;
    VkDescriptorSet  fallbackDescSet_ = VK_NULL_HANDLE;
    WorldTexture     fallbackTexture_;
    std::unordered_map<std::size_t, WorldTexture>    textureCache_;
    std::unordered_map<std::size_t, VkDescriptorSet> texDescSetCache_;

    // ── Async loading ─────────────────────────────────────────────────────
    WorldAssetLoader assetLoader_;
    std::thread        loadThread_;
    std::atomic<bool>  cpuDone_{false};
    std::atomic<bool>  cpuFailed_{false};
    std::atomic<bool>  cancelLoad_{false};
    std::atomic<float> progress_{0.0f};
    mutable std::mutex activityMtx_;
    std::string        activityStr_{"Idle"};

    // Written by background thread before cpuDone_, read by main thread after.
    std::vector<WorldStagedMesh>    stagedMeshes_;
    std::vector<WorldStagedMesh>    stagedEnemyMeshes_;
    std::vector<WorldStagedMesh>    stagedTowerMeshes_;
    std::vector<WorldStagedTexture> stagedTextures_;
    WorldAssetSpec            assetSpec_;
    std::string                failReason_;

    // GPU upload cursors (main thread only)
    std::size_t              gpuMeshCursor_  = 0;
    std::size_t              gpuEnemyMeshCursor_ = 0;
    std::size_t              gpuTowerMeshCursor_ = 0;
    std::size_t              gpuTexCursor_   = 0;
    bool                     gpuDescsDone_   = false;
    bool                     gpuPipeDone_    = false;
    std::vector<std::size_t> meshImgIdx_;   // parallel to meshes_
    std::vector<std::size_t> enemyMeshImgIdx_; // parallel to enemyTemplateMeshes_
    std::vector<std::size_t> towerMeshImgIdx_; // parallel to towerTemplateMeshes_

    AnimatedEntityInstanceSet animatedEntityInstances_;
    AnimatedEntityInstanceSet towerInstances_;
    std::vector<float> animatedEntityPhaseOffsetsSeconds_;
    int hoveredInstanceIndex_ = -1;
    int selectedInstanceIndex_ = -1;
    std::vector<glm::vec3> routePoints_;

    static constexpr uint32_t kMaxSkinJoints = 128;
    VkBuffer      skinPaletteBuffer_ = VK_NULL_HANDLE;
    VmaAllocation skinPaletteAlloc_  = nullptr;
    void*         skinPaletteMapped_ = nullptr;

    std::unique_ptr<TemplateAnimator> templateAnimator_;
    bool firstRenderTick_ = true;
    std::chrono::steady_clock::time_point lastRenderTick_{};

    void uploadSkinPalette(const std::vector<glm::mat4>& joints);
    void uploadIdentitySkinPalette();
    // ─────────────────────────────────────────────────────────────────────

    void setActivity(float progress, std::string activity);
    void backgroundLoad(std::filesystem::path assetPath);

    void buildPipeline();
    void buildHighlightPipeline();
    void createSamplerLayoutAndPool();
    WorldTexture    uploadRGBAImage(const uint8_t* pixels, uint32_t w, uint32_t h);
    VkDescriptorSet makeTextureDescSet(VkImageView view);
    void            createFallbackTexture();

    VkShaderModule loadSpirv(const std::filesystem::path& path) const;
    WorldMesh uploadMesh(const std::vector<WorldVertex>& verts, const std::vector<uint32_t>& idx);
    VkBuffer  uploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocation& alloc);
};
