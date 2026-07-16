#include "utility/WorldRenderer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <SFML/Graphics/Image.hpp>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>
#include <set>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>

// ─── helpers ──────────────────────────────────────────────────────────────────

namespace {

constexpr size_t kMaxMeshes = 512;

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

    // Descriptor set layout: binding 0 = combined image sampler (fragment)
    VkDescriptorSetLayoutBinding bind{};
    bind.binding            = 0;
    bind.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount    = 1;
    bind.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings    = &bind;
    if (vkCreateDescriptorSetLayout(ctx_.device(), &li, nullptr, &textureDescLayout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture descriptor set layout.");

    // Private descriptor pool (freed wholesale in release())
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets       = 513;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &ps;
    if (vkCreateDescriptorPool(ctx_.device(), &pi, nullptr, &ownDescPool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture descriptor pool.");
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

    VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr.dstSet          = set;
    wr.dstBinding      = 0;
    wr.descriptorCount = 1;
    wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo      = &ii;
    vkUpdateDescriptorSets(ctx_.device(), 1, &wr, 0, nullptr);
    return set;
}

void WorldRenderer::createFallbackTexture() {
    const uint8_t white[4] = {255, 255, 255, 255};
    fallbackTexture_ = uploadRGBAImage(white, 1, 1);
    fallbackDescSet_ = fallbackTexture_.valid() ? makeTextureDescSet(fallbackTexture_.view) : VK_NULL_HANDLE;
}

WorldRenderer::WorldRenderer(VulkanContext& ctx) : ctx_(ctx) {}

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

    fastgltf::Asset& asset   = assetResult.get();
    const auto       assetDir = assetPath.parent_path();

    // ── 2. Collect needed image indices ───────────────────────────────────
    setActivity(0.05f, "Scanning materials...");
    std::set<std::size_t> neededImgs;
    for (const auto& mesh : asset.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (!prim.materialIndex.has_value()) continue;
            const auto& mat = asset.materials[*prim.materialIndex];
            if (!mat.pbrData.baseColorTexture.has_value()) continue;
            const auto tIdx = mat.pbrData.baseColorTexture->textureIndex;
            if (tIdx < asset.textures.size() && asset.textures[tIdx].imageIndex.has_value())
                neededImgs.insert(*asset.textures[tIdx].imageIndex);
        }
    }

    // ── 3. Decode images on the CPU ───────────────────────────────────────
    const int totalImgs = static_cast<int>(neededImgs.size());
    int       imgsDone  = 0;

    for (std::size_t imgIdx : neededImgs) {
        if (cancelLoad_.load()) return;

        std::string imgName;
        const auto& gltfImg = asset.images[imgIdx];
        if (!gltfImg.name.empty()) {
            imgName = std::string(gltfImg.name);
        } else if (auto* uri = std::get_if<fastgltf::sources::URI>(&gltfImg.data)) {
            imgName = std::filesystem::path(std::string(uri->uri.path())).filename().string();
        } else {
            imgName = "image_" + std::to_string(imgIdx);
        }

        setActivity(0.08f + 0.52f * ((float)imgsDone / std::max(1, totalImgs)),
                    "Decoding " + imgName +
                    " (" + std::to_string(imgsDone + 1) + "/" + std::to_string(totalImgs) + ")");

        sf::Image sfImg;
        bool ok = false;

        std::visit(fastgltf::visitor{
            [&](const fastgltf::sources::URI& src) {
                ok = sfImg.loadFromFile(assetDir / std::string(src.uri.path()));
            },
            [&](const fastgltf::sources::BufferView& src) {
                const auto& bv  = asset.bufferViews[src.bufferViewIndex];
                const auto& buf = asset.buffers[bv.bufferIndex];
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
            st.imageIndex  = imgIdx;
            st.displayName = imgName;
            st.width       = sz.x;
            st.height      = sz.y;
            st.pixels.assign(sfImg.getPixelsPtr(), sfImg.getPixelsPtr() + sz.x * sz.y * 4);
            stagedTextures_.push_back(std::move(st));
        }
        ++imgsDone;
    }

    if (cancelLoad_.load()) return;

    // ── 4. Build mesh CPU data ────────────────────────────────────────────
    setActivity(0.60f, "Processing geometry...");

    std::size_t meshCount = 0;
    for (const auto& mesh : asset.meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (meshCount >= kMaxMeshes) break;
            if (primitive.type != fastgltf::PrimitiveType::Triangles) continue;
            if (!primitive.indicesAccessor.has_value()) continue;

            auto posIt = primitive.findAttribute("POSITION");
            if (posIt == primitive.attributes.end()) continue;

            auto& posAccessor      = asset.accessors[posIt->accessorIndex];
            const size_t vertCount = posAccessor.count;
            if (vertCount == 0) continue;

            std::vector<glm::vec3> positions(vertCount);
            std::vector<glm::vec3> normals(vertCount, {0.0f, 1.0f, 0.0f});
            std::vector<glm::vec2> uvs(vertCount, {0.0f, 0.0f});

            fastgltf::copyFromAccessor<glm::vec3>(asset, posAccessor, positions.data());

            auto normIt = primitive.findAttribute("NORMAL");
            if (normIt != primitive.attributes.end())
                fastgltf::copyFromAccessor<glm::vec3>(asset, asset.accessors[normIt->accessorIndex], normals.data());

            auto uvIt = primitive.findAttribute("TEXCOORD_0");
            if (uvIt != primitive.attributes.end())
                fastgltf::copyFromAccessor<glm::vec2>(asset, asset.accessors[uvIt->accessorIndex], uvs.data());

            std::vector<WorldVertex> vertices(vertCount);
            for (size_t i = 0; i < vertCount; ++i) {
                vertices[i].position = positions[i];
                vertices[i].normal   = normals[i];
                vertices[i].uv       = uvs[i];
            }

            auto& idxAccessor = asset.accessors[*primitive.indicesAccessor];
            std::vector<uint32_t> indices(idxAccessor.count);
            switch (idxAccessor.componentType) {
                case fastgltf::ComponentType::UnsignedByte: {
                    std::vector<uint8_t> tmp(idxAccessor.count);
                    fastgltf::copyFromAccessor<uint8_t>(asset, idxAccessor, tmp.data());
                    for (size_t i = 0; i < tmp.size(); ++i) indices[i] = tmp[i];
                    break;
                }
                case fastgltf::ComponentType::UnsignedShort: {
                    std::vector<uint16_t> tmp(idxAccessor.count);
                    fastgltf::copyFromAccessor<uint16_t>(asset, idxAccessor, tmp.data());
                    for (size_t i = 0; i < tmp.size(); ++i) indices[i] = tmp[i];
                    break;
                }
                default:
                    fastgltf::copyFromAccessor<uint32_t>(asset, idxAccessor, indices.data());
                    break;
            }

            // Resolve material → image index
            std::size_t imgIdx = SIZE_MAX;
            if (primitive.materialIndex.has_value()) {
                const auto& mat = asset.materials[*primitive.materialIndex];
                if (mat.pbrData.baseColorTexture.has_value()) {
                    const auto tIdx = mat.pbrData.baseColorTexture->textureIndex;
                    if (tIdx < asset.textures.size() && asset.textures[tIdx].imageIndex.has_value())
                        imgIdx = *asset.textures[tIdx].imageIndex;
                }
            }

            StagedMesh sm;
            sm.vertices   = std::move(vertices);
            sm.indices    = std::move(indices);
            sm.imageIndex = imgIdx;
            stagedMeshes_.push_back(std::move(sm));
            ++meshCount;
        }
        if (meshCount >= kMaxMeshes) break;
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
            meshes_.push_back(uploadMesh(sm.vertices, sm.indices));
            meshImgIdx_.push_back(sm.imageIndex);
            totalVertices_ += static_cast<int>(sm.vertices.size());
            totalIndices_  += static_cast<int>(sm.indices.size());
            ++gpuMeshCursor_;
        }
        progress_.store(0.73f, std::memory_order_relaxed);
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
    status_ = "OK — " + std::to_string(meshes_.size()) + " meshes, " +
              std::to_string(totalVertices_) + " verts, " +
              std::to_string(totalIndices_ / 3) + " tris";
    spdlog::info("[WorldRenderer] {}", status_);
}
void WorldRenderer::render(VkCommandBuffer cmd, VkExtent2D extent, const glm::mat4& view) {
    if (!loaded_ || pipeline_ == VK_NULL_HANDLE) return;

    // ── Projection ────────────────────────────────────────────────────────
    const float aspect = (extent.height > 0)
                             ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                             : 1.0f;

    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.05f, 2000.0f);
    proj[1][1] *= -1.0f; // Vulkan Y-flip

    const glm::mat4 model{1.0f};
    const glm::mat4 mvp = proj * view * model;

    // ── Dynamic viewport / scissor ────────────────────────────────────────
    VkViewport viewport{};
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ── Push constants ────────────────────────────────────────────────────
    struct PushConstants {
        glm::mat4 mvp;
        glm::mat4 model;
    } pc{mvp, model};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(cmd, pipelineLayout_,
                        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

    // ── Draw each mesh (bind its base-colour texture) ─────────────────────
    for (const WorldMesh& mesh : meshes_) {
        VkDescriptorSet ds = mesh.descriptorSet ? mesh.descriptorSet : fallbackDescSet_;
        if (ds) vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &ds, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
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
    meshes_.clear();
    meshImgIdx_.clear();

    // Reset async state
    stagedMeshes_.clear();
    stagedTextures_.clear();
    failReason_.clear();
    gpuMeshCursor_ = 0;
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

    VkVertexInputAttributeDescription attribs[3]{};
    attribs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WorldVertex, position)};
    attribs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WorldVertex, normal)};
    attribs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(WorldVertex, uv)};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
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
