#include "utility/WorldRenderer.hpp"
#include "utility/TemplateAnimator.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <SFML/Graphics/Image.hpp>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <functional>
#include <array>
#include <mutex>
#include <map>
#include <optional>
#include <set>
#include <limits>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <unordered_set>

// ─── helpers ──────────────────────────────────────────────────────────────────

namespace {

constexpr size_t kMaxMeshes = 512;

std::size_t hashCombine(std::size_t seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

std::size_t makeTextureKey(std::string_view assetId, std::size_t imageIndex) {
    std::size_t seed = std::hash<std::string_view>{}(assetId);
    return hashCombine(seed, imageIndex);
}

bool parseWaypointIndex(std::string_view name, int& outIndex) {
    constexpr std::string_view prefix = "Waypoint_";
    if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix) {
        return false;
    }

    const std::string_view suffix = name.substr(prefix.size());
    int value = 0;
    for (char c : suffix) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value <= 0) return false;

    outIndex = value;
    return true;
}

glm::mat4 makeYFacingTransform(const glm::vec3& position, const glm::vec3& target) {
    glm::vec3 direction = target - position;
    direction.y = 0.0f;

    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq < 1e-6f) {
        return glm::translate(glm::mat4{1.0f}, position);
    }

    direction /= std::sqrt(lengthSq);
    const float yaw = std::atan2(direction.x, direction.z);
    return glm::translate(glm::mat4{1.0f}, position) *
           glm::rotate(glm::mat4{1.0f}, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
}

std::filesystem::path findAssetsRoot(const std::filesystem::path& fromPath) {
    std::filesystem::path probe = fromPath;
    while (!probe.empty()) {
        const auto candidate = probe / "assets";
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        const auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return {};
}

VkBuffer createStagingBuffer(VmaAllocator allocator, VkDeviceSize size, VmaAllocation& outAlloc,
                             VmaAllocationInfo& outInfo) {
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &buffer, &outAlloc, &outInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer.");
    }
    return buffer;
}

void copyBufferImmediate(VkDevice device, VkQueue queue, VkCommandPool pool,
                         VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &ai, &cb);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cb, src, dst, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cb);
}

const auto kModelsPath = std::filesystem::path("assets") / "models";

float maxScaleFromMatrix(const glm::mat4& m) {
    const float sx = glm::length(glm::vec3(m[0]));
    const float sy = glm::length(glm::vec3(m[1]));
    const float sz = glm::length(glm::vec3(m[2]));
    return std::max(sx, std::max(sy, sz));
}

bool raySphereIntersect(const glm::vec3& rayOrigin,
                        const glm::vec3& rayDir,
                        const glm::vec3& center,
                        float radius,
                        float& outT) {
    const glm::vec3 oc = rayOrigin - center;
    const float a = glm::dot(rayDir, rayDir);
    const float b = 2.0f * glm::dot(oc, rayDir);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return false;
    }

    const float sqrtDisc = std::sqrt(disc);
    const float inv2a = 0.5f / a;
    const float t0 = (-b - sqrtDisc) * inv2a;
    const float t1 = (-b + sqrtDisc) * inv2a;

    if (t0 > 1e-4f) {
        outT = t0;
        return true;
    }
    if (t1 > 1e-4f) {
        outT = t1;
        return true;
    }

    return false;
}

} // namespace

// ─── texture helpers ──────────────────────────────────────────────────────────

void WorldRenderer::createSamplerLayoutAndPool() {
    // Sampler
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter        = VK_FILTER_LINEAR;
    si.minFilter        = VK_FILTER_LINEAR;
    si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod           = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(ctx_.device(), &si, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture sampler.");

    // Descriptor set layout: binding 0 = base color sampler, binding 1 = skin matrices
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding            = 0;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding            = 1;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 2;
    li.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(ctx_.device(), &li, nullptr, &textureDescLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture descriptor set layout.");

    // Private descriptor pool (freed wholesale in release())
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512}
    };
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets       = 513;
    pi.poolSizeCount = 2;
    pi.pPoolSizes    = poolSizes;
    if (vkCreateDescriptorPool(ctx_.device(), &pi, nullptr, &ownDescPool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture descriptor pool.");

    const VkDeviceSize skinBufferSize = sizeof(glm::mat4) * kMaxSkinJoints;
    VkBufferCreateInfo skinBufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    skinBufInfo.size = skinBufferSize;
    skinBufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    skinBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo skinAllocInfo{};
    skinAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    skinAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(ctx_.allocator(), &skinBufInfo, &skinAllocInfo,
                        &skinPaletteBuffer_, &skinPaletteAlloc_, &allocInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skin palette uniform buffer.");
    }
    skinPaletteMapped_ = allocInfo.pMappedData;
    uploadIdentitySkinPalette();
}

WorldTexture WorldRenderer::uploadRGBAImage(const uint8_t* pixels, uint32_t w, uint32_t h) {
    if (!pixels || w == 0 || h == 0) return {};

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(w) * h * 4;

    // Staging buffer
    VmaAllocation stagingAlloc{};
    VmaAllocationInfo stagingInfo{};
    VkBuffer staging = createStagingBuffer(ctx_.allocator(), byteSize, stagingAlloc, stagingInfo);
    std::memcpy(stagingInfo.pMappedData, pixels, static_cast<size_t>(byteSize));

    // Device-local image
    VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgCI.extent        = {w, h, 1};
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imgAlloc{};
    imgAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    WorldTexture tex{};
    if (vmaCreateImage(ctx_.allocator(), &imgCI, &imgAlloc, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(ctx_.allocator(), staging, stagingAlloc);
        return {};
    }

    // Upload via one-shot command buffer
    VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbAI.commandPool = ctx_.commandPool();
    cbAI.level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;
    VkCommandBuffer cb{};
    vkAllocateCommandBuffers(ctx_.device(), &cbAI, &cb);

    VkCommandBufferBeginInfo cbBI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbBI);

    auto transition = [&](VkImageLayout from, VkImageLayout to,
                          VkAccessFlags src, VkAccessFlags dst,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout        = from;
        b.newLayout        = to;
        b.srcAccessMask    = src;
        b.dstAccessMask    = dst;
        b.image            = tex.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {w, h, 1};
    vkCmdCopyBufferToImage(cb, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(ctx_.graphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.graphicsQueue());
    vkFreeCommandBuffers(ctx_.device(), ctx_.commandPool(), 1, &cb);
    vmaDestroyBuffer(ctx_.allocator(), staging, stagingAlloc);

    // Image view
    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image            = tex.image;
    viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(ctx_.device(), &viewCI, nullptr, &tex.view) != VK_SUCCESS) {
        vmaDestroyImage(ctx_.allocator(), tex.image, tex.allocation);
        return {};
    }
    return tex;
}

VkDescriptorSet WorldRenderer::makeTextureDescSet(VkImageView view) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = ownDescPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &textureDescLayout_;
    VkDescriptorSet set{};
    if (vkAllocateDescriptorSets(ctx_.device(), &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkDescriptorImageInfo ii{};
    ii.sampler     = sampler_;
    ii.imageView   = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo bi{};
    bi.buffer = skinPaletteBuffer_;
    bi.offset = 0;
    bi.range  = sizeof(glm::mat4) * kMaxSkinJoints;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &ii;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bi;

    vkUpdateDescriptorSets(ctx_.device(), 2, writes, 0, nullptr);
    return set;
}

void WorldRenderer::createFallbackTexture() {
    const uint8_t white[4] = {255, 255, 255, 255};
    fallbackTexture_ = uploadRGBAImage(white, 1, 1);
    fallbackDescSet_ = fallbackTexture_.valid() ? makeTextureDescSet(fallbackTexture_.view) : VK_NULL_HANDLE;
}

WorldRenderer::WorldRenderer(lua_State* L, VulkanContext& ctx)
    : L_(L), ctx_(ctx), templateAnimator_(std::make_unique<TemplateAnimator>()) {}

WorldRenderer::~WorldRenderer() {
    release();
}

// ─── activity helpers ─────────────────────────────────────────────────────────

void WorldRenderer::setActivity(float prog, std::string activity) {
    progress_.store(prog, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(activityMtx_);
    activityStr_ = std::move(activity);
}

std::string WorldRenderer::loadActivity() const {
    std::lock_guard<std::mutex> lock(activityMtx_);
    return activityStr_;
}

void WorldRenderer::setAnimatedEntityInstanceTransforms(const std::vector<glm::mat4>& transforms) {
    enemyInstanceTransforms_ = transforms;
}

bool WorldRenderer::hasTemplateAnimation() const {
    return templateAnimator_->hasAnimation();
}

const std::string& WorldRenderer::templateAnimationName() const {
    return templateAnimator_->animationName();
}

int WorldRenderer::templateAnimationClipCount() const {
    return templateAnimator_->animationClipCount();
}

int WorldRenderer::activeTemplateAnimationClipIndex() const {
    return templateAnimator_->activeAnimationClipIndex();
}

std::vector<std::string> WorldRenderer::templateAnimationClipNames() const {
    return templateAnimator_->animationClipNames();
}

bool WorldRenderer::setActiveTemplateAnimationClipByIndex(int clipIndex) {
    return templateAnimator_->setActiveAnimationClipByIndex(clipIndex);
}

bool WorldRenderer::setActiveTemplateAnimationClipByName(const std::string& clipName) {
    return templateAnimator_->setActiveAnimationClipByName(clipName);
}

void WorldRenderer::setCompositeTemplateAnimationMode(bool enabled) {
    templateAnimator_->setCompositeMode(enabled);
}

bool WorldRenderer::compositeTemplateAnimationMode() const {
    return templateAnimator_->compositeMode();
}

const EnemyAnimationDebugInfo& WorldRenderer::templateAnimationDebugInfo() const {
    return templateAnimator_->debugInfo();
}

void WorldRenderer::uploadSkinPalette(const std::vector<glm::mat4>& joints) {
    if (!skinPaletteMapped_ || skinPaletteBuffer_ == VK_NULL_HANDLE) {
        return;
    }

    std::array<glm::mat4, kMaxSkinJoints> palette{};
    for (auto& m : palette) {
        m = glm::mat4{1.0f};
    }

    const std::size_t count = std::min<std::size_t>(joints.size(), kMaxSkinJoints);
    for (std::size_t i = 0; i < count; ++i) {
        palette[i] = joints[i];
    }

    const VkDeviceSize byteSize = sizeof(glm::mat4) * kMaxSkinJoints;
    std::memcpy(skinPaletteMapped_, palette.data(), static_cast<std::size_t>(byteSize));
    vmaFlushAllocation(ctx_.allocator(), skinPaletteAlloc_, 0, byteSize);
}

void WorldRenderer::uploadIdentitySkinPalette() {
    static const std::vector<glm::mat4> kIdentityPalette(1, glm::mat4{1.0f});
    uploadSkinPalette(kIdentityPalette);
}

// ─── public loading interface ─────────────────────────────────────────────────

void WorldRenderer::beginLoad(const std::filesystem::path& assetPath) {
    release();

    // GPU infrastructure that doesn't depend on asset contents
    createSamplerLayoutAndPool();
    createFallbackTexture();

    cancelLoad_.store(false, std::memory_order_relaxed);
    cpuDone_.store(false,    std::memory_order_relaxed);
    cpuFailed_.store(false,  std::memory_order_relaxed);
    progress_.store(0.0f,    std::memory_order_relaxed);
    firstRenderTick_ = true;
    templateAnimator_->reset();
    setActivity(0.0f, "Starting...");

    loadThread_ = std::thread(&WorldRenderer::backgroundLoad, this, assetPath);
}

void WorldRenderer::backgroundLoad(std::filesystem::path assetPath) {
    // ── 1. Parse ──────────────────────────────────────────────────────────
    setActivity(0.01f, "Parsing " + assetPath.filename().string() + "...");

    if (!std::filesystem::exists(assetPath)) {
        failReason_ = "Asset not found: " + assetPath.string();
        cpuFailed_.store(true, std::memory_order_release);
        return;
    }

    constexpr auto kAllExtensions =
        fastgltf::Extensions::KHR_texture_transform          |
        fastgltf::Extensions::KHR_texture_basisu             |
        fastgltf::Extensions::KHR_mesh_quantization          |
        fastgltf::Extensions::EXT_meshopt_compression        |
        fastgltf::Extensions::KHR_lights_punctual            |
        fastgltf::Extensions::EXT_texture_webp               |
        fastgltf::Extensions::KHR_materials_specular         |
        fastgltf::Extensions::KHR_materials_ior              |
        fastgltf::Extensions::KHR_materials_iridescence      |
        fastgltf::Extensions::KHR_materials_volume           |
        fastgltf::Extensions::KHR_materials_transmission     |
        fastgltf::Extensions::KHR_materials_clearcoat        |
        fastgltf::Extensions::KHR_materials_emissive_strength|
        fastgltf::Extensions::KHR_materials_sheen            |
        fastgltf::Extensions::KHR_materials_unlit            |
        fastgltf::Extensions::KHR_materials_anisotropy       |
        fastgltf::Extensions::EXT_mesh_gpu_instancing        |
        fastgltf::Extensions::KHR_materials_dispersion       |
        fastgltf::Extensions::KHR_materials_variants         |
        fastgltf::Extensions::KHR_materials_diffuse_transmission |
        fastgltf::Extensions::GODOT_single_root;

    fastgltf::Parser parser(kAllExtensions);
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(assetPath);
    if (dataResult.error() != fastgltf::Error::None) {
        failReason_ = "Data load error: " + std::string(fastgltf::getErrorName(dataResult.error()));
        cpuFailed_.store(true, std::memory_order_release);
        return;
    }
    auto assetResult = parser.loadGltf(dataResult.get(), assetPath.parent_path(),
                                       fastgltf::Options::LoadExternalBuffers);
    if (assetResult.error() != fastgltf::Error::None) {
        failReason_ = "Parse error: " + std::string(fastgltf::getErrorName(assetResult.error()));
        cpuFailed_.store(true, std::memory_order_release);
        return;
    }

    if (cancelLoad_.load()) return;

    fastgltf::Asset& mapAsset = assetResult.get();

    std::size_t meshCount = 0;
    std::unordered_set<std::size_t> queuedTextureKeys;

    // Convert a fastgltf node's local transform to a glm::mat4
    auto nodeLocalTransform = [](const fastgltf::Node& node) -> glm::mat4 {
        return std::visit(fastgltf::visitor{
            [](const fastgltf::TRS& trs) -> glm::mat4 {
                const glm::mat4 T = glm::translate(glm::mat4{1.0f},
                    glm::vec3(trs.translation.x(), trs.translation.y(), trs.translation.z()));
                // fastgltf quat is stored X,Y,Z,W; glm::quat(w,x,y,z)
                const glm::quat R{trs.rotation.w(), trs.rotation.x(),
                                  trs.rotation.y(), trs.rotation.z()};
                const glm::mat4 S = glm::scale(glm::mat4{1.0f},
                    glm::vec3(trs.scale.x(), trs.scale.y(), trs.scale.z()));
                return T * glm::mat4_cast(R) * S;
            },
            [](const fastgltf::math::fmat4x4& mat) -> glm::mat4 {
                glm::mat4 result;
                std::memcpy(&result, mat.data(), sizeof(glm::mat4));
                return result;
            }
        }, node.transform);
    };

    auto stageTexturesForAsset = [&](const fastgltf::Asset& srcAsset,
                                     const std::filesystem::path& srcPath,
                                     std::string_view assetId,
                                     const std::string& activityLabel) {
        setActivity(0.05f, "Scanning materials for " + activityLabel + "...");
        std::set<std::size_t> neededImgs;
        for (const auto& mesh : srcAsset.meshes) {
            for (const auto& prim : mesh.primitives) {
                if (!prim.materialIndex.has_value()) continue;
                const auto& mat = srcAsset.materials[*prim.materialIndex];
                if (!mat.pbrData.baseColorTexture.has_value()) continue;
                const auto tIdx = mat.pbrData.baseColorTexture->textureIndex;
                if (tIdx < srcAsset.textures.size() && srcAsset.textures[tIdx].imageIndex.has_value()) {
                    neededImgs.insert(*srcAsset.textures[tIdx].imageIndex);
                }
            }
        }

        const auto srcDir = srcPath.parent_path();
        const int totalImgs = static_cast<int>(neededImgs.size());
        int imgsDone = 0;

        for (std::size_t imgIdx : neededImgs) {
            if (cancelLoad_.load()) return;

            const std::size_t textureKey = makeTextureKey(assetId, imgIdx);
            if (queuedTextureKeys.find(textureKey) != queuedTextureKeys.end()) {
                ++imgsDone;
                continue;
            }

            std::string imgName;
            const auto& gltfImg = srcAsset.images[imgIdx];
            if (!gltfImg.name.empty()) {
                imgName = std::string(gltfImg.name);
            } else if (auto* uri = std::get_if<fastgltf::sources::URI>(&gltfImg.data)) {
                imgName = std::filesystem::path(std::string(uri->uri.path())).filename().string();
            } else {
                imgName = "image_" + std::to_string(imgIdx);
            }

            setActivity(0.08f + 0.52f * ((float)imgsDone / std::max(1, totalImgs)),
                        "Decoding " + imgName + " (" + activityLabel + ")");

            sf::Image sfImg;
            bool ok = false;

            std::visit(fastgltf::visitor{
                [&](const fastgltf::sources::URI& src) {
                    ok = sfImg.loadFromFile(srcDir / std::string(src.uri.path()));
                },
                [&](const fastgltf::sources::BufferView& src) {
                    const auto& bv  = srcAsset.bufferViews[src.bufferViewIndex];
                    const auto& buf = srcAsset.buffers[bv.bufferIndex];
                    std::visit(fastgltf::visitor{
                        [&](const fastgltf::sources::Array& d) {
                            ok = sfImg.loadFromMemory(
                                static_cast<const void*>(d.bytes.data() + bv.byteOffset),
                                bv.byteLength);
                        },
                        [](const auto&) {}
                    }, buf.data);
                },
                [&](const fastgltf::sources::Array& src) {
                    ok = sfImg.loadFromMemory(static_cast<const void*>(src.bytes.data()), src.bytes.size());
                },
                [](const auto&) {}
            }, gltfImg.data);

            if (ok) {
                const auto sz = sfImg.getSize();
                StagedTexture st;
                st.imageIndex  = textureKey;
                st.displayName = imgName;
                st.width       = sz.x;
                st.height      = sz.y;
                st.pixels.assign(sfImg.getPixelsPtr(), sfImg.getPixelsPtr() + sz.x * sz.y * 4);
                stagedTextures_.push_back(std::move(st));
                queuedTextureKeys.insert(textureKey);
            }
            ++imgsDone;
        }
    };

    auto processPrimitive = [&](const fastgltf::Asset& srcAsset,
                                std::string_view assetId,
                                const fastgltf::Primitive& primitive,
                                const glm::mat4& worldTransform,
                                std::vector<StagedMesh>& outMeshes,
                                int sourceNodeIndex,
                                int sourceSkinIndex,
                                std::string_view debugGroup,
                                std::string_view debugLabel) {
        if (meshCount >= kMaxMeshes) return;
        if (primitive.type != fastgltf::PrimitiveType::Triangles) return;
        if (!primitive.indicesAccessor.has_value()) return;

        auto posIt = primitive.findAttribute("POSITION");
        if (posIt == primitive.attributes.end()) return;

        auto& posAccessor      = srcAsset.accessors[posIt->accessorIndex];
        const size_t vertCount = posAccessor.count;
        if (vertCount == 0) return;

        std::vector<glm::vec3> positions(vertCount);
        std::vector<glm::vec3> normals(vertCount, {0.0f, 1.0f, 0.0f});
        std::vector<glm::vec2> uvs(vertCount, {0.0f, 0.0f});
        std::vector<glm::u16vec4> joints(vertCount, glm::u16vec4{0, 0, 0, 0});
        std::vector<glm::vec4> weights(vertCount, glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

        fastgltf::copyFromAccessor<glm::vec3>(srcAsset, posAccessor, positions.data());

        auto normIt = primitive.findAttribute("NORMAL");
        if (normIt != primitive.attributes.end()) {
            fastgltf::copyFromAccessor<glm::vec3>(srcAsset, srcAsset.accessors[normIt->accessorIndex], normals.data());
        }

        auto uvIt = primitive.findAttribute("TEXCOORD_0");
        if (uvIt != primitive.attributes.end()) {
            fastgltf::copyFromAccessor<glm::vec2>(srcAsset, srcAsset.accessors[uvIt->accessorIndex], uvs.data());
        }

        auto jointsIt = primitive.findAttribute("JOINTS_0");
        if (jointsIt != primitive.attributes.end()) {
            const auto& jointsAccessor = srcAsset.accessors[jointsIt->accessorIndex];
            if (jointsAccessor.componentType == fastgltf::ComponentType::UnsignedByte) {
                std::vector<glm::u8vec4> tmp(vertCount, glm::u8vec4{0, 0, 0, 0});
                fastgltf::copyFromAccessor<glm::u8vec4>(srcAsset, jointsAccessor, tmp.data());
                for (size_t i = 0; i < vertCount; ++i) {
                    joints[i] = glm::u16vec4{tmp[i].x, tmp[i].y, tmp[i].z, tmp[i].w};
                }
            } else {
                fastgltf::copyFromAccessor<glm::u16vec4>(srcAsset, jointsAccessor, joints.data());
            }
        }

        auto weightsIt = primitive.findAttribute("WEIGHTS_0");
        if (weightsIt != primitive.attributes.end()) {
            fastgltf::copyFromAccessor<glm::vec4>(srcAsset, srcAsset.accessors[weightsIt->accessorIndex], weights.data());
        }

        std::vector<WorldVertex> vertices(vertCount);
        glm::vec3 minPos{std::numeric_limits<float>::max()};
        glm::vec3 maxPos{std::numeric_limits<float>::lowest()};
        for (size_t i = 0; i < vertCount; ++i) {
            vertices[i].position = positions[i];
            vertices[i].normal   = normals[i];
            vertices[i].uv       = uvs[i];
            vertices[i].joints   = joints[i];
            vertices[i].weights  = weights[i];
            minPos = glm::min(minPos, positions[i]);
            maxPos = glm::max(maxPos, positions[i]);
        }

        auto& idxAccessor = srcAsset.accessors[*primitive.indicesAccessor];
        std::vector<uint32_t> indices(idxAccessor.count);
        switch (idxAccessor.componentType) {
            case fastgltf::ComponentType::UnsignedByte: {
                std::vector<uint8_t> tmp(idxAccessor.count);
                fastgltf::copyFromAccessor<uint8_t>(srcAsset, idxAccessor, tmp.data());
                for (size_t i = 0; i < tmp.size(); ++i) indices[i] = tmp[i];
                break;
            }
            case fastgltf::ComponentType::UnsignedShort: {
                std::vector<uint16_t> tmp(idxAccessor.count);
                fastgltf::copyFromAccessor<uint16_t>(srcAsset, idxAccessor, tmp.data());
                for (size_t i = 0; i < tmp.size(); ++i) indices[i] = tmp[i];
                break;
            }
            default:
                fastgltf::copyFromAccessor<uint32_t>(srcAsset, idxAccessor, indices.data());
                break;
        }

        std::size_t imgKey = SIZE_MAX;
        if (primitive.materialIndex.has_value()) {
            const auto& mat = srcAsset.materials[*primitive.materialIndex];
            if (mat.pbrData.baseColorTexture.has_value()) {
                const auto tIdx = mat.pbrData.baseColorTexture->textureIndex;
                if (tIdx < srcAsset.textures.size() && srcAsset.textures[tIdx].imageIndex.has_value()) {
                    imgKey = makeTextureKey(assetId, *srcAsset.textures[tIdx].imageIndex);
                }
            }
        }

        StagedMesh sm;
        sm.vertices       = std::move(vertices);
        sm.indices        = std::move(indices);
        sm.imageIndex     = imgKey;
        sm.modelTransform = worldTransform;
        sm.sourceNodeIndex = sourceNodeIndex;
        sm.sourceSkinIndex = sourceSkinIndex;
        sm.debugGroup = std::string(debugGroup);
        sm.debugLabel = std::string(debugLabel);
        sm.localBoundsCenter = (minPos + maxPos) * 0.5f;
        sm.localBoundsRadius = std::max(0.05f, glm::distance(minPos, maxPos) * 0.5f);
        outMeshes.push_back(std::move(sm));
        ++meshCount;
    };

    struct MarkerState {
        std::optional<glm::vec3> start;
        std::optional<glm::vec3> end;
        std::map<int, glm::vec3> waypoints;
    };
    MarkerState markers;

    auto traverseScene = [&](const fastgltf::Asset& srcAsset,
                             std::string_view assetId,
                             const glm::mat4& rootTransform,
                             bool captureMarkers,
                             std::vector<StagedMesh>& outMeshes,
                             std::string_view debugGroup,
                             std::string_view debugLabelPrefix) {
        std::function<void(std::size_t, const glm::mat4&)> visitNode =
            [&](std::size_t nodeIdx, const glm::mat4& parentWorld) {
                if (cancelLoad_.load()) return;
                if (meshCount >= kMaxMeshes) return;

                const fastgltf::Node& node = srcAsset.nodes[nodeIdx];
                const glm::mat4 world = parentWorld * nodeLocalTransform(node);

                if (captureMarkers && !node.name.empty()) {
                    const std::string nodeName = std::string(node.name);
                    const glm::vec3 markerPos = glm::vec3(world[3]);
                    if (nodeName == "Start") {
                        markers.start = markerPos;
                    } else if (nodeName == "End") {
                        markers.end = markerPos;
                    } else {
                        int waypointIndex = 0;
                        if (parseWaypointIndex(nodeName, waypointIndex)) {
                            markers.waypoints[waypointIndex] = markerPos;
                        }
                    }
                }

                if (node.meshIndex.has_value()) {
                    const int nodeSkinIndex = node.skinIndex.has_value() ? static_cast<int>(*node.skinIndex) : -1;
                    std::string label = std::string(debugLabelPrefix);
                    if (!node.name.empty()) {
                        if (!label.empty()) {
                            label += ":";
                        }
                        label += std::string(node.name);
                    }
                    for (const auto& prim : srcAsset.meshes[*node.meshIndex].primitives) {
                        processPrimitive(srcAsset, assetId, prim, world, outMeshes,
                                         static_cast<int>(nodeIdx), nodeSkinIndex,
                                         debugGroup, label);
                    }
                }

                for (std::size_t childIdx : node.children) {
                    visitNode(childIdx, world);
                }
            };

        if (!srcAsset.scenes.empty()) {
            const std::size_t sceneIdx = srcAsset.defaultScene.has_value() ? *srcAsset.defaultScene : 0;
            for (std::size_t rootIdx : srcAsset.scenes[sceneIdx].nodeIndices) {
                if (cancelLoad_.load()) break;
                visitNode(rootIdx, rootTransform);
            }
        } else {
            for (std::size_t i = 0; i < srcAsset.nodes.size() && !cancelLoad_.load(); ++i) {
                visitNode(i, rootTransform);
            }
        }
    };

    setActivity(0.60f, "Processing map geometry...");
    const std::string mapAssetId = assetPath.lexically_normal().string();
    stageTexturesForAsset(mapAsset, assetPath, mapAssetId, assetPath.filename().string());
    if (cancelLoad_.load()) return;
    traverseScene(mapAsset, mapAssetId, glm::mat4{1.0f}, true, stagedMeshes_, "map", assetPath.stem().string());

    const auto modelsDir = kModelsPath;

    auto loadAndStagePlacedModel = [&](const std::filesystem::path& modelPath,
                                       const glm::mat4& placement,
                                       const std::string& label,
                                       std::vector<StagedMesh>& outMeshes) {
        if (cancelLoad_.load() || !std::filesystem::exists(modelPath)) {
            return;
        }

        setActivity(0.62f, "Loading " + label + " model...");
        auto modelDataResult = fastgltf::GltfDataBuffer::FromPath(modelPath);
        if (modelDataResult.error() != fastgltf::Error::None) {
            spdlog::warn("[WorldRenderer] Failed to read {}: {}", modelPath.string(),
                         fastgltf::getErrorName(modelDataResult.error()));
            return;
        }

        fastgltf::Parser modelParser(kAllExtensions);
        auto modelAssetResult = modelParser.loadGltf(modelDataResult.get(), modelPath.parent_path(),
                                                     fastgltf::Options::LoadExternalBuffers);
        if (modelAssetResult.error() != fastgltf::Error::None) {
            spdlog::warn("[WorldRenderer] Failed to parse {}: {}", modelPath.string(),
                         fastgltf::getErrorName(modelAssetResult.error()));
            return;
        }

        fastgltf::Asset& modelAsset = modelAssetResult.get();
        const std::string modelAssetId = modelPath.lexically_normal().string();
        stageTexturesForAsset(modelAsset, modelPath, modelAssetId, label);
        if (cancelLoad_.load()) return;
        traverseScene(modelAsset, modelAssetId, placement, false, outMeshes, "prop", label);
    };

    auto loadAndStageModelTemplate = [&](const std::filesystem::path& modelPath,
                                         const std::string& label,
                                         std::vector<StagedMesh>& outMeshes) {
        if (cancelLoad_.load() || !std::filesystem::exists(modelPath)) {
            return;
        }

        setActivity(0.62f, "Loading " + label + " model...");
        auto modelDataResult = fastgltf::GltfDataBuffer::FromPath(modelPath);
        if (modelDataResult.error() != fastgltf::Error::None) {
            spdlog::warn("[WorldRenderer] Failed to read {}: {}", modelPath.string(),
                         fastgltf::getErrorName(modelDataResult.error()));
            return;
        }

        fastgltf::Parser modelParser(kAllExtensions);
        auto modelAssetResult = modelParser.loadGltf(modelDataResult.get(), modelPath.parent_path(),
                                                     fastgltf::Options::LoadExternalBuffers);
        if (modelAssetResult.error() != fastgltf::Error::None) {
            spdlog::warn("[WorldRenderer] Failed to parse {}: {}", modelPath.string(),
                         fastgltf::getErrorName(modelAssetResult.error()));
            return;
        }

        fastgltf::Asset& modelAsset = modelAssetResult.get();
        const std::string modelAssetId = modelPath.lexically_normal().string();
        stageTexturesForAsset(modelAsset, modelPath, modelAssetId, label);
        if (cancelLoad_.load()) return;
        traverseScene(modelAsset, modelAssetId, glm::mat4{1.0f}, false, outMeshes, "enemy", label);
        templateAnimator_->initializeFromAsset(modelAsset);
    };

    routePoints_.clear();
    if (markers.start.has_value()) {
        routePoints_.push_back(*markers.start);
    }
    for (const auto& [index, waypoint] : markers.waypoints) {
        (void)index;
        routePoints_.push_back(waypoint);
    }
    if (markers.end.has_value()) {
        routePoints_.push_back(*markers.end);
    }

    if (!modelsDir.empty()) {
        if (markers.start.has_value()) {
            glm::mat4 portalPlacement = glm::translate(glm::mat4{1.0f}, *markers.start);
            if (!markers.waypoints.empty()) {
                portalPlacement = makeYFacingTransform(*markers.start, markers.waypoints.begin()->second);
            }

            std::filesystem::path portalPath = modelsDir / "portal.glb";
            if (!std::filesystem::exists(portalPath)) {
                const auto fallbackPortal = modelsDir / "portal.lgb";
                if (std::filesystem::exists(fallbackPortal)) {
                    portalPath = fallbackPortal;
                }
            }
            loadAndStagePlacedModel(portalPath, portalPlacement, "portal", stagedMeshes_);
        }

        if (markers.end.has_value()) {
            glm::mat4 basePlacement = glm::translate(glm::mat4{1.0f}, *markers.end);
            if (!markers.waypoints.empty()) {
                basePlacement = makeYFacingTransform(*markers.end, markers.waypoints.rbegin()->second);
            }
            loadAndStagePlacedModel(modelsDir / "base.glb", basePlacement, "base", stagedMeshes_);
        }

        loadAndStageModelTemplate(modelsDir / "goblin1.glb", "goblin template", stagedEnemyMeshes_);
    } else {
        spdlog::warn("[WorldRenderer] Could not locate assets directory for portal/base placement. {}", modelsDir.string());
    }

    if (markers.start.has_value() && !markers.waypoints.empty()) {
        spdlog::info("[WorldRenderer] Portal placed at Start, facing Waypoint_1.");
    } else if (markers.start.has_value()) {
        spdlog::warn("[WorldRenderer] Start marker found, but Waypoint_1 missing. Portal placed without facing target.");
    }
    if (markers.end.has_value() && !markers.waypoints.empty()) {
        spdlog::info("[WorldRenderer] Base placed at End, facing last waypoint.");
    } else if (markers.end.has_value()) {
        spdlog::warn("[WorldRenderer] End marker found, but no Waypoint_X markers found. Base placed without facing target.");
    }

    setActivity(0.65f, "Ready — " + std::to_string(stagedMeshes_.size()) + " meshes, " +
                       std::to_string(stagedTextures_.size()) + " textures queued for GPU upload...");
    cpuDone_.store(true, std::memory_order_release);
}

void WorldRenderer::tickLoad() {
    if (loaded_ || loadFailed_) return;

    if (cpuFailed_.load(std::memory_order_acquire)) {
        loadFailed_ = true;
        status_     = failReason_;
        spdlog::error("[WorldRenderer] {}", status_);
        return;
    }
    if (!cpuDone_.load(std::memory_order_acquire)) return;

    // ── GPU: upload all meshes in one batch (fast — just buffer copies) ───
    if (gpuMeshCursor_ < stagedMeshes_.size()) {
        const std::size_t total = stagedMeshes_.size();
        setActivity(0.65f, "Uploading geometry (" + std::to_string(total) + " meshes)...");
        while (gpuMeshCursor_ < total) {
            const auto& sm = stagedMeshes_[gpuMeshCursor_];
            WorldMesh wm = uploadMesh(sm.vertices, sm.indices);
            wm.modelTransform = sm.modelTransform;
            wm.sourceNodeIndex = sm.sourceNodeIndex;
            wm.sourceSkinIndex = sm.sourceSkinIndex;
            wm.localBoundsCenter = sm.localBoundsCenter;
            wm.localBoundsRadius = sm.localBoundsRadius;
            wm.debugGroup = sm.debugGroup;
            wm.debugLabel = sm.debugLabel;
            meshes_.push_back(std::move(wm));
            meshImgIdx_.push_back(sm.imageIndex);
            totalVertices_ += static_cast<int>(sm.vertices.size());
            totalIndices_  += static_cast<int>(sm.indices.size());
            ++gpuMeshCursor_;
        }
        progress_.store(0.73f, std::memory_order_relaxed);
        return;
    }

    if (gpuEnemyMeshCursor_ < stagedEnemyMeshes_.size()) {
        const std::size_t total = stagedEnemyMeshes_.size();
        setActivity(0.73f, "Uploading enemy template geometry (" + std::to_string(total) + " meshes)...");
        while (gpuEnemyMeshCursor_ < total) {
            const auto& sm = stagedEnemyMeshes_[gpuEnemyMeshCursor_];
            WorldMesh wm = uploadMesh(sm.vertices, sm.indices);
            wm.modelTransform = sm.modelTransform;
            wm.sourceNodeIndex = sm.sourceNodeIndex;
            wm.sourceSkinIndex = sm.sourceSkinIndex;
            wm.localBoundsCenter = sm.localBoundsCenter;
            wm.localBoundsRadius = sm.localBoundsRadius;
            wm.debugGroup = sm.debugGroup;
            wm.debugLabel = sm.debugLabel;
            enemyTemplateMeshes_.push_back(std::move(wm));
            enemyMeshImgIdx_.push_back(sm.imageIndex);
            ++gpuEnemyMeshCursor_;
        }
        progress_.store(0.75f, std::memory_order_relaxed);
        return;
    }

    // ── GPU: upload one texture per frame ─────────────────────────────────
    if (gpuTexCursor_ < stagedTextures_.size()) {
        const std::size_t total = stagedTextures_.size();
        const auto& st = stagedTextures_[gpuTexCursor_];
        setActivity(0.73f + 0.20f * ((float)(gpuTexCursor_ + 1) / (float)total),
                    "Uploading " + st.displayName +
                    " (" + std::to_string(gpuTexCursor_ + 1) + "/" + std::to_string(total) + ")");

        WorldTexture wt = uploadRGBAImage(st.pixels.data(), st.width, st.height);
        if (wt.valid()) {
            texDescSetCache_[st.imageIndex] = makeTextureDescSet(wt.view);
            textureCache_[st.imageIndex]    = wt;
        } else {
            texDescSetCache_[st.imageIndex] = fallbackDescSet_;
        }
        ++gpuTexCursor_;
        progress_.store(0.73f + 0.20f * ((float)gpuTexCursor_ /
                                          (float)std::max(std::size_t{1}, total)),
                        std::memory_order_relaxed);
        return;
    }

    // ── Assign descriptor sets to meshes ──────────────────────────────────
    if (!gpuDescsDone_) {
        for (std::size_t i = 0; i < meshes_.size(); ++i) {
            const std::size_t imgIdx = (i < meshImgIdx_.size()) ? meshImgIdx_[i] : SIZE_MAX;
            auto it = (imgIdx != SIZE_MAX) ? texDescSetCache_.find(imgIdx) : texDescSetCache_.end();
            meshes_[i].descriptorSet = (it != texDescSetCache_.end()) ? it->second : fallbackDescSet_;
        }
        for (std::size_t i = 0; i < enemyTemplateMeshes_.size(); ++i) {
            const std::size_t imgIdx = (i < enemyMeshImgIdx_.size()) ? enemyMeshImgIdx_[i] : SIZE_MAX;
            auto it = (imgIdx != SIZE_MAX) ? texDescSetCache_.find(imgIdx) : texDescSetCache_.end();
            enemyTemplateMeshes_[i].descriptorSet = (it != texDescSetCache_.end()) ? it->second : fallbackDescSet_;
        }
        gpuDescsDone_ = true;
    }

    // ── Build pipeline once ───────────────────────────────────────────────
    if (!gpuPipeDone_) {
        setActivity(0.95f, "Building render pipeline...");
        try {
            buildPipeline();
        } catch (const std::exception& ex) {
            loadFailed_ = true;
            status_     = std::string("Pipeline build failed: ") + ex.what();
            return;
        }
        gpuPipeDone_ = true;
        progress_.store(1.0f, std::memory_order_relaxed);
        setActivity(1.0f, "Complete");
    }

    // All done!
    loaded_ = true;
    status_ = "OK - " + std::to_string(meshes_.size()) + " meshes, " +
              std::to_string(totalVertices_) + " verts, " +
              std::to_string(totalIndices_ / 3) + " tris";
    spdlog::info("[WorldRenderer] {}", status_);
}

bool WorldRenderer::pickModel(const glm::vec3& rayOrigin, const glm::vec3& rayDir, WorldPickHit& outHit) const {
    if (!loaded_) {
        return false;
    }

    bool anyHit = false;
    float bestT = std::numeric_limits<float>::max();
    WorldPickHit best{};

    auto testMesh = [&](const WorldMesh& mesh,
                        const glm::mat4& world,
                        int meshIndex,
                        int enemyInstanceIndex) {
        const glm::vec3 worldCenter = glm::vec3(world * glm::vec4(mesh.localBoundsCenter, 1.0f));
        const float worldRadius = mesh.localBoundsRadius * maxScaleFromMatrix(world);

        float t = 0.0f;
        if (!raySphereIntersect(rayOrigin, rayDir, worldCenter, worldRadius, t)) {
            return;
        }

        if (t >= bestT) {
            return;
        }

        anyHit = true;
        bestT = t;
        best.hit = true;
        best.distance = t;
        best.worldPosition = rayOrigin + rayDir * t;
        best.worldNormal = glm::normalize(best.worldPosition - worldCenter);
        if (!std::isfinite(best.worldNormal.x) || !std::isfinite(best.worldNormal.y) || !std::isfinite(best.worldNormal.z)) {
            best.worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        best.group = mesh.debugGroup;
        best.label = mesh.debugLabel;
        best.meshIndex = meshIndex;
        best.nodeIndex = mesh.sourceNodeIndex;
        best.skinIndex = mesh.sourceSkinIndex;
        best.enemyInstanceIndex = enemyInstanceIndex;
    };

    for (std::size_t i = 0; i < meshes_.size(); ++i) {
        const WorldMesh& mesh = meshes_[i];
        testMesh(mesh, mesh.modelTransform, static_cast<int>(i), -1);
    }

    for (std::size_t instanceIdx = 0; instanceIdx < enemyInstanceTransforms_.size(); ++instanceIdx) {
        const glm::mat4& instanceTransform = enemyInstanceTransforms_[instanceIdx];
        for (std::size_t meshIdx = 0; meshIdx < enemyTemplateMeshes_.size(); ++meshIdx) {
            const WorldMesh& mesh = enemyTemplateMeshes_[meshIdx];
            const glm::mat4 animatedNodeTransform = templateAnimator_->resolveNodeTransform(mesh.sourceNodeIndex, mesh.modelTransform);
            const glm::mat4 world = instanceTransform * animatedNodeTransform;
            testMesh(mesh, world, static_cast<int>(meshIdx), static_cast<int>(instanceIdx));
        }
    }

    if (anyHit) {
        outHit = best;
    }
    return anyHit;
}

void WorldRenderer::render(VkCommandBuffer cmd, VkExtent2D extent, const glm::mat4& view) {
    if (!loaded_ || pipeline_ == VK_NULL_HANDLE) return;

    const auto now = std::chrono::steady_clock::now();
    float dtSeconds = 0.0f;
    if (firstRenderTick_) {
        firstRenderTick_ = false;
        lastRenderTick_ = now;
    } else {
        dtSeconds = std::chrono::duration<float>(now - lastRenderTick_).count();
        lastRenderTick_ = now;
    }
    templateAnimator_->update(dtSeconds);

    // ── Projection ────────────────────────────────────────────────────────
    const float aspect = (extent.height > 0)
                             ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                             : 1.0f;

    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f; // Vulkan Y-flip

    // ── Dynamic viewport / scissor ────────────────────────────────────────
    VkViewport viewport{};
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ── Push constants / draw each mesh with its own model transform ─────
    struct PushConstants {
        glm::mat4 mvp;
        glm::mat4 model;
    };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    uploadIdentitySkinPalette();

    // ── Draw each mesh (bind its base-colour texture) ─────────────────────
    for (const WorldMesh& mesh : meshes_) {
        const glm::mat4 mvp = proj * view * mesh.modelTransform;
        const PushConstants pc{mvp, mesh.modelTransform};
        vkCmdPushConstants(cmd, pipelineLayout_,
                            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkDescriptorSet ds = mesh.descriptorSet ? mesh.descriptorSet : fallbackDescSet_;
        if (ds) vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &ds, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    }

    for (const glm::mat4& instanceTransform : enemyInstanceTransforms_) {
        int activeSkinIndex = -2;
        for (const WorldMesh& mesh : enemyTemplateMeshes_) {
            const glm::mat4 animatedNodeTransform = templateAnimator_->resolveNodeTransform(mesh.sourceNodeIndex, mesh.modelTransform);

            if (mesh.sourceSkinIndex != activeSkinIndex) {
                activeSkinIndex = mesh.sourceSkinIndex;
                const std::vector<glm::mat4>* jointPalette = templateAnimator_->skinJointMatricesForSkin(mesh.sourceSkinIndex);
                if (jointPalette) {
                    uploadSkinPalette(*jointPalette);
                } else {
                    uploadIdentitySkinPalette();
                }
            }

            const glm::mat4 world = instanceTransform * animatedNodeTransform;
            const glm::mat4 mvp = proj * view * world;
            const PushConstants pc{mvp, world};
            vkCmdPushConstants(cmd, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

            VkDescriptorSet ds = mesh.descriptorSet ? mesh.descriptorSet : fallbackDescSet_;
            if (ds) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &ds, 0, nullptr);
            }
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }
}

void WorldRenderer::release() {
    // Signal + join background thread before touching GPU resources
    cancelLoad_.store(true, std::memory_order_relaxed);
    if (loadThread_.joinable()) {
        loadThread_.join();
    }

    if (ctx_.device() == VK_NULL_HANDLE) return;

    ctx_.waitIdle();

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx_.device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx_.device(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    if (ownDescPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx_.device(), ownDescPool_, nullptr);
        ownDescPool_     = VK_NULL_HANDLE;
        fallbackDescSet_ = VK_NULL_HANDLE;
        texDescSetCache_.clear();
    }
    if (textureDescLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(ctx_.device(), textureDescLayout_, nullptr);
        textureDescLayout_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(ctx_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (skinPaletteBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx_.allocator(), skinPaletteBuffer_, skinPaletteAlloc_);
        skinPaletteBuffer_ = VK_NULL_HANDLE;
        skinPaletteAlloc_ = nullptr;
        skinPaletteMapped_ = nullptr;
    }

    auto destroyTex = [&](WorldTexture& t) {
        if (t.view)  vkDestroyImageView(ctx_.device(), t.view, nullptr);
        if (t.image) vmaDestroyImage(ctx_.allocator(), t.image, t.allocation);
        t = {};
    };
    destroyTex(fallbackTexture_);
    for (auto& [idx, tex] : textureCache_) destroyTex(tex);
    textureCache_.clear();

    for (WorldMesh& mesh : meshes_) {
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx_.allocator(), mesh.vertexBuffer, mesh.vertexAlloc);
        if (mesh.indexBuffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx_.allocator(), mesh.indexBuffer, mesh.indexAlloc);
    }
    for (WorldMesh& mesh : enemyTemplateMeshes_) {
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx_.allocator(), mesh.vertexBuffer, mesh.vertexAlloc);
        if (mesh.indexBuffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(ctx_.allocator(), mesh.indexBuffer, mesh.indexAlloc);
    }
    meshes_.clear();
    enemyTemplateMeshes_.clear();
    meshImgIdx_.clear();
    enemyMeshImgIdx_.clear();
    enemyInstanceTransforms_.clear();
    routePoints_.clear();
    templateAnimator_->reset();
    firstRenderTick_ = true;

    // Reset async state
    stagedMeshes_.clear();
    stagedEnemyMeshes_.clear();
    stagedTextures_.clear();
    failReason_.clear();
    gpuMeshCursor_ = 0;
    gpuEnemyMeshCursor_ = 0;
    gpuTexCursor_  = 0;
    gpuDescsDone_  = false;
    gpuPipeDone_   = false;
    cpuDone_.store(false,   std::memory_order_relaxed);
    cpuFailed_.store(false, std::memory_order_relaxed);
    loaded_        = false;
    loadFailed_    = false;
    totalVertices_ = 0;
    totalIndices_  = 0;
}
// ─── private ──────────────────────────────────────────────────────────────────

VkBuffer WorldRenderer::uploadBuffer(const void* data, VkDeviceSize size,
                                     VkBufferUsageFlags usage, VmaAllocation& outAlloc) {
    // Staging
    VmaAllocation stagingAlloc = nullptr;
    VmaAllocationInfo stagingInfo{};
    VkBuffer staging = createStagingBuffer(ctx_.allocator(), size, stagingAlloc, stagingInfo);
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    // Device-local buffer
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = size;
    bufInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vmaCreateBuffer(ctx_.allocator(), &bufInfo, &allocInfo, &buffer, &outAlloc, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(ctx_.allocator(), staging, stagingAlloc);
        throw std::runtime_error("Failed to create device-local buffer.");
    }

    copyBufferImmediate(ctx_.device(), ctx_.graphicsQueue(), ctx_.commandPool(), staging, buffer, size);
    vmaDestroyBuffer(ctx_.allocator(), staging, stagingAlloc);
    return buffer;
}

WorldMesh WorldRenderer::uploadMesh(const std::vector<WorldVertex>& vertices,
                                    const std::vector<uint32_t>& indices) {
    WorldMesh mesh;
    mesh.vertexBuffer = uploadBuffer(
        vertices.data(),
        vertices.size() * sizeof(WorldVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        mesh.vertexAlloc);

    mesh.indexBuffer = uploadBuffer(
        indices.data(),
        indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        mesh.indexAlloc);

    mesh.indexCount = static_cast<uint32_t>(indices.size());
    return mesh;
}

VkShaderModule WorldRenderer::loadSpirv(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open SPIR-V: " + path.string());
    }
    const auto size = static_cast<size_t>(file.tellg());
    if (size % 4 != 0) {
        throw std::runtime_error("SPIR-V file size not aligned to 4 bytes: " + path.string());
    }
    file.seekg(0);
    std::vector<uint32_t> code(size / 4);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = size;
    ci.pCode    = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx_.device(), &ci, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module from: " + path.string());
    }
    return module;
}

void WorldRenderer::buildPipeline() {
    VkShaderModule vert = loadSpirv("assets/shaders/mesh.vert.spv");
    VkShaderModule frag = loadSpirv("assets/shaders/mesh.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Vertex input: binding 0 — WorldVertex (pos, normal, uv)
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(WorldVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribs[5]{};
    attribs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WorldVertex, position)};
    attribs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WorldVertex, normal)};
    attribs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(WorldVertex, uv)};
    attribs[3] = {3, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(WorldVertex, joints)};
    attribs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(WorldVertex, weights)};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions    = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Push constants: mvp (mat4) + model (mat4) = 128 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(glm::mat4) * 2;

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &textureDescLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;

    if (vkCreatePipelineLayout(ctx_.device(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(ctx_.device(), vert, nullptr);
        vkDestroyShaderModule(ctx_.device(), frag, nullptr);
        throw std::runtime_error("Failed to create pipeline layout.");
    }

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments    = &blendAttach;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    // Dynamic rendering — no VkRenderPass needed
    const VkFormat colorFmt = ctx_.swapchainColorFormat();
    const VkFormat depthFmt = ctx_.depthFormat();
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &colorFmt;
    renderingInfo.depthAttachmentFormat   = depthFmt;

    VkGraphicsPipelineCreateInfo pipelineCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineCI.pNext               = &renderingInfo;
    pipelineCI.stageCount          = 2;
    pipelineCI.pStages             = stages;
    pipelineCI.pVertexInputState   = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState      = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState   = &multisampling;
    pipelineCI.pDepthStencilState  = &depthStencil;
    pipelineCI.pColorBlendState    = &blending;
    pipelineCI.pDynamicState       = &dynState;
    pipelineCI.layout              = pipelineLayout_;

    VkResult result = vkCreateGraphicsPipelines(
        ctx_.device(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline_);

    vkDestroyShaderModule(ctx_.device(), vert, nullptr);
    vkDestroyShaderModule(ctx_.device(), frag, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline.");
    }
}
