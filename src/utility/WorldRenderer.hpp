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

// Identifies what kind of instanced entity a pick/hover/selection result refers to.
// This is the ONLY place callers should branch on "is this a tower or an enemy" --
// the picking/highlight code itself is shared and kind-agnostic; only the index's
// *meaning* (which list to look it up in) depends on this tag.
enum class WorldEntityKind {
    None = 0,
    Enemy,
    Tower,
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
    // entityKind says which instance list `instanceIndex` refers to (or None for static
    // level geometry / no instance). Enemies and towers share this single index space
    // conceptually but are never ambiguous because the kind always travels with the index.
    WorldEntityKind entityKind = WorldEntityKind::None;
    int instanceIndex = -1;
};

struct WorldPickOptions {
    float staticRadiusScale = 1.0f;
    float staticRadiusPadding = 0.0f;
    float staticMinRadius = 0.05f;
    // Used by enemies -- generous, distance-forgiving pick radius so they remain clickable
    // no matter how far the camera moves away, unlike inert static level geometry.
    float instancedRadiusScale = 1.45f;
    float instancedRadiusPadding = 0.35f;
    float instancedMinRadius = 0.65f;
    // Towers are stationary and visually smaller/tighter than enemies, so they get their
    // own (smaller) instanced radius tuning. Both still flow through the exact same
    // picking/highlight code path as enemies -- only these tuning knobs differ by kind.
    float towerRadiusScale = 1.0f;
    float towerRadiusPadding = 0.10f;
    float towerMinRadius = 0.35f;
};

struct WorldPickDebugSphere {
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    WorldEntityKind entityKind = WorldEntityKind::None;
    int instanceIndex = -1;
    std::string group;
    std::string label;
};

struct TowerPreviewPanel {
    int prototypeIndex = -1;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
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
    void setHighlightedInstances(WorldEntityKind hoveredKind, int hoveredInstanceIndex,
                                 WorldEntityKind selectedKind, int selectedInstanceIndex);
    void setEnemyInstanceTransforms(const std::vector<glm::mat4>& transforms) { setAnimatedEntityInstanceTransforms(transforms); }
    const std::vector<glm::vec3>& routePoints() const { return routePoints_; }
    bool hasAnimatedEntityTemplate() const { return !enemyTemplateMeshes_.empty(); }
    bool hasEnemyTemplate() const { return hasAnimatedEntityTemplate(); }
    bool pickModel(const glm::vec3& rayOrigin,
                   const glm::vec3& rayDir,
                   WorldPickHit& outHit,
                   const WorldPickOptions& options = {}) const;
    // Precise triangle-level raycast against the static world geometry (terrain, rocks, cliffs, etc.).
    // Unlike pickModel() (which uses bounding-sphere approximations tuned for selecting towers/enemies),
    // this walks the actual CPU-side triangle data retained in stagedMeshes_ for accurate terrain height sampling.
    bool raycastStaticGeometry(const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir,
                              WorldPickHit& outHit) const;
    std::vector<WorldPickDebugSphere> buildDynamicPickDebugSpheres(const WorldPickOptions& options = {}) const;
    const EnemyAnimationDebugInfo& templateAnimationDebugInfo() const;
    const EnemyAnimationDebugInfo& enemyAnimationDebugInfo() const { return templateAnimationDebugInfo(); }
    const std::vector<TowerPlacementRegion>& placementRegions() const { return placementRegions_; }

    void render(VkCommandBuffer cmd, VkExtent2D extent, const glm::mat4& view);
    void renderTowerPreviewPanels(VkCommandBuffer cmd,
                                  VkExtent2D extent,
                                  const std::vector<TowerPreviewPanel>& panels,
                                  float spinRadians);
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
    WorldEntityKind hoveredEntityKind_ = WorldEntityKind::None;
    int hoveredInstanceIndex_ = -1;
    WorldEntityKind selectedEntityKind_ = WorldEntityKind::None;
    int selectedInstanceIndex_ = -1;
    std::vector<glm::vec3> routePoints_;
    std::vector<TowerPlacementRegion> placementRegions_;

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
    bool computeTowerPrototypeBounds(int prototypeIndex, glm::vec3& outCenter, float& outRadius) const;
};
