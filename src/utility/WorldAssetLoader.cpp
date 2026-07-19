#include "utility/WorldAssetLoader.hpp"

#include "utility/TemplateAnimator.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <SFML/Graphics/Image.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>

namespace {

constexpr std::size_t kMaxMeshes = 512;

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
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    if (value <= 0) {
        return false;
    }

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

glm::mat4 composePlacementTransform(const glm::vec3& translation,
                                    const glm::vec3& eulerDegrees,
                                    const glm::vec3& scale) {
    glm::mat4 transform = glm::translate(glm::mat4{1.0f}, translation);
    transform = transform * glm::rotate(glm::mat4{1.0f}, glm::radians(eulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = transform * glm::rotate(glm::mat4{1.0f}, glm::radians(eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = transform * glm::rotate(glm::mat4{1.0f}, glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = transform * glm::scale(glm::mat4{1.0f}, scale);
    return transform;
}

} // namespace

bool WorldAssetLoader::load(const std::filesystem::path& assetPath,
                            const WorldAssetSpec& spec,
                            TemplateAnimator& animator,
                            const IsCancelledFn& isCancelled,
                            const ActivityFn& setActivity,
                            WorldAssetLoadResult& outResult,
                            std::string& outFailReason) const {
    outResult = {};
    outFailReason.clear();

    setActivity(0.01f, "Parsing " + assetPath.filename().string() + "...");

    if (!std::filesystem::exists(assetPath)) {
        outFailReason = "Asset not found: " + assetPath.string();
        return false;
    }

    constexpr auto kAllExtensions =
        fastgltf::Extensions::KHR_texture_transform |
        fastgltf::Extensions::KHR_texture_basisu |
        fastgltf::Extensions::KHR_mesh_quantization |
        fastgltf::Extensions::EXT_meshopt_compression |
        fastgltf::Extensions::KHR_lights_punctual |
        fastgltf::Extensions::EXT_texture_webp |
        fastgltf::Extensions::KHR_materials_specular |
        fastgltf::Extensions::KHR_materials_ior |
        fastgltf::Extensions::KHR_materials_iridescence |
        fastgltf::Extensions::KHR_materials_volume |
        fastgltf::Extensions::KHR_materials_transmission |
        fastgltf::Extensions::KHR_materials_clearcoat |
        fastgltf::Extensions::KHR_materials_emissive_strength |
        fastgltf::Extensions::KHR_materials_sheen |
        fastgltf::Extensions::KHR_materials_unlit |
        fastgltf::Extensions::KHR_materials_anisotropy |
        fastgltf::Extensions::EXT_mesh_gpu_instancing |
        fastgltf::Extensions::KHR_materials_dispersion |
        fastgltf::Extensions::KHR_materials_variants |
        fastgltf::Extensions::KHR_materials_diffuse_transmission |
        fastgltf::Extensions::GODOT_single_root;

    fastgltf::Parser parser(kAllExtensions);
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(assetPath);
    if (dataResult.error() != fastgltf::Error::None) {
        outFailReason = "Data load error: " + std::string(fastgltf::getErrorName(dataResult.error()));
        return false;
    }
    auto assetResult = parser.loadGltf(dataResult.get(), assetPath.parent_path(),
                                       fastgltf::Options::LoadExternalBuffers);
    if (assetResult.error() != fastgltf::Error::None) {
        outFailReason = "Parse error: " + std::string(fastgltf::getErrorName(assetResult.error()));
        return false;
    }

    if (isCancelled()) {
        return false;
    }

    fastgltf::Asset& mapAsset = assetResult.get();

    std::size_t meshCount = 0;
    std::unordered_set<std::size_t> queuedTextureKeys;

    auto nodeLocalTransform = [](const fastgltf::Node& node) -> glm::mat4 {
        return std::visit(fastgltf::visitor{
            [](const fastgltf::TRS& trs) -> glm::mat4 {
                const glm::mat4 T = glm::translate(glm::mat4{1.0f},
                    glm::vec3(trs.translation.x(), trs.translation.y(), trs.translation.z()));
                const glm::quat R{trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z()};
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
                if (!prim.materialIndex.has_value()) {
                    continue;
                }
                const auto& mat = srcAsset.materials[*prim.materialIndex];
                if (!mat.pbrData.baseColorTexture.has_value()) {
                    continue;
                }
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
            if (isCancelled()) {
                return;
            }

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

            setActivity(0.08f + 0.52f * (static_cast<float>(imgsDone) / std::max(1, totalImgs)),
                        "Decoding " + imgName + " (" + activityLabel + ")");

            sf::Image sfImg;
            bool ok = false;

            std::visit(fastgltf::visitor{
                [&](const fastgltf::sources::URI& src) {
                    ok = sfImg.loadFromFile(srcDir / std::string(src.uri.path()));
                },
                [&](const fastgltf::sources::BufferView& src) {
                    const auto& bv = srcAsset.bufferViews[src.bufferViewIndex];
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
                WorldStagedTexture st;
                st.imageIndex = textureKey;
                st.displayName = imgName;
                st.width = sz.x;
                st.height = sz.y;
                st.pixels.assign(sfImg.getPixelsPtr(), sfImg.getPixelsPtr() + sz.x * sz.y * 4);
                outResult.textures.push_back(std::move(st));
                queuedTextureKeys.insert(textureKey);
            }
            ++imgsDone;
        }
    };

    auto processPrimitive = [&](const fastgltf::Asset& srcAsset,
                                std::string_view assetId,
                                const fastgltf::Primitive& primitive,
                                const glm::mat4& worldTransform,
                                const glm::mat4& groupRootTransform,
                                int templatePrototypeIndex,
                                std::vector<WorldStagedMesh>& outMeshes,
                                int sourceNodeIndex,
                                int sourceSkinIndex,
                                std::string_view debugGroup,
                                std::string_view debugLabel) {
        if (meshCount >= kMaxMeshes || primitive.type != fastgltf::PrimitiveType::Triangles) {
            return;
        }

        auto posIt = primitive.findAttribute("POSITION");
        if (posIt == primitive.attributes.end()) {
            return;
        }

        auto& posAccessor = srcAsset.accessors[posIt->accessorIndex];
        const size_t vertCount = posAccessor.count;
        if (vertCount == 0) {
            return;
        }

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
            vertices[i].normal = normals[i];
            vertices[i].uv = uvs[i];
            vertices[i].joints = joints[i];
            vertices[i].weights = weights[i];
            minPos = glm::min(minPos, positions[i]);
            maxPos = glm::max(maxPos, positions[i]);
        }

        std::vector<uint32_t> indices;
        if (primitive.indicesAccessor.has_value()) {
            auto& idxAccessor = srcAsset.accessors[*primitive.indicesAccessor];
            indices.resize(idxAccessor.count);
            switch (idxAccessor.componentType) {
                case fastgltf::ComponentType::UnsignedByte: {
                    std::vector<uint8_t> tmp(idxAccessor.count);
                    fastgltf::copyFromAccessor<uint8_t>(srcAsset, idxAccessor, tmp.data());
                    for (size_t i = 0; i < tmp.size(); ++i) {
                        indices[i] = tmp[i];
                    }
                    break;
                }
                case fastgltf::ComponentType::UnsignedShort: {
                    std::vector<uint16_t> tmp(idxAccessor.count);
                    fastgltf::copyFromAccessor<uint16_t>(srcAsset, idxAccessor, tmp.data());
                    for (size_t i = 0; i < tmp.size(); ++i) {
                        indices[i] = tmp[i];
                    }
                    break;
                }
                default:
                    fastgltf::copyFromAccessor<uint32_t>(srcAsset, idxAccessor, indices.data());
                    break;
            }
        } else {
            // GLTF triangles may be non-indexed (drawArrays-style); synthesize a linear index buffer.
            indices.resize(vertCount);
            for (size_t i = 0; i < vertCount; ++i) {
                indices[i] = static_cast<uint32_t>(i);
            }
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

        WorldStagedMesh sm;
        sm.vertices = std::move(vertices);
        sm.indices = std::move(indices);
        sm.imageIndex = imgKey;
        sm.modelTransform = worldTransform;
        sm.groupRootTransform = groupRootTransform;
        sm.templatePrototypeIndex = templatePrototypeIndex;
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
                             int templatePrototypeIndex,
                             bool captureMarkers,
                             std::vector<WorldStagedMesh>& outMeshes,
                             std::string_view debugGroup,
                             std::string_view debugLabelPrefix) {
        std::function<void(std::size_t, const glm::mat4&)> visitNode =
            [&](std::size_t nodeIdx, const glm::mat4& parentWorld) {
                if (isCancelled() || meshCount >= kMaxMeshes) {
                    return;
                }

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
                        processPrimitive(srcAsset, assetId, prim, world, rootTransform, templatePrototypeIndex, outMeshes,
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
                if (isCancelled()) {
                    break;
                }
                visitNode(rootIdx, rootTransform);
            }
        } else {
            for (std::size_t i = 0; i < srcAsset.nodes.size() && !isCancelled(); ++i) {
                visitNode(i, rootTransform);
            }
        }
    };

    setActivity(0.60f, "Processing map geometry...");
    const std::string mapAssetId = assetPath.lexically_normal().string();
    stageTexturesForAsset(mapAsset, assetPath, mapAssetId, assetPath.filename().string());
    if (isCancelled()) {
        return false;
    }
    traverseScene(mapAsset, mapAssetId, glm::mat4{1.0f}, -1, true, outResult.worldMeshes, "map", assetPath.stem().string());

    auto loadAndStagePlacedModel = [&](const std::filesystem::path& modelPath,
                                       const glm::mat4& placement,
                                       const std::string& label,
                                       const std::string& debugGroup,
                                       std::vector<WorldStagedMesh>& outMeshes,
                                       bool required,
                                       bool& outFailed) {
        if (isCancelled()) {
            return;
        }
        if (!std::filesystem::exists(modelPath)) {
            if (required) {
                outFailReason = "Required model not found: " + modelPath.string();
                outFailed = true;
            }
            return;
        }

        setActivity(0.62f, "Loading " + label + " model...");
        auto modelDataResult = fastgltf::GltfDataBuffer::FromPath(modelPath);
        if (modelDataResult.error() != fastgltf::Error::None) {
            if (required) {
                outFailReason = "Failed to read model " + modelPath.string() + ": " +
                                std::string(fastgltf::getErrorName(modelDataResult.error()));
                outFailed = true;
            } else {
                spdlog::warn("[WorldAssetLoader] Failed to read {}: {}", modelPath.string(),
                             fastgltf::getErrorName(modelDataResult.error()));
            }
            return;
        }

        fastgltf::Parser modelParser(kAllExtensions);
        auto modelAssetResult = modelParser.loadGltf(modelDataResult.get(), modelPath.parent_path(),
                                                     fastgltf::Options::LoadExternalBuffers);
        if (modelAssetResult.error() != fastgltf::Error::None) {
            if (required) {
                outFailReason = "Failed to parse model " + modelPath.string() + ": " +
                                std::string(fastgltf::getErrorName(modelAssetResult.error()));
                outFailed = true;
            } else {
                spdlog::warn("[WorldAssetLoader] Failed to parse {}: {}", modelPath.string(),
                             fastgltf::getErrorName(modelAssetResult.error()));
            }
            return;
        }

        fastgltf::Asset& modelAsset = modelAssetResult.get();
        const std::string modelAssetId = modelPath.lexically_normal().string();
        stageTexturesForAsset(modelAsset, modelPath, modelAssetId, label);
        if (isCancelled()) {
            return;
        }
        traverseScene(modelAsset, modelAssetId, placement, -1, false, outMeshes, debugGroup, label);
    };

    auto loadAndStageModelTemplate = [&](const std::filesystem::path& modelPath,
                                         const std::string& label,
                                         const std::string& debugGroup,
                                         int prototypeIndex,
                                         std::vector<WorldStagedMesh>& outMeshes,
                                         bool required,
                                         bool initializeAnimator,
                                         bool& outFailed) {
        if (isCancelled()) {
            return;
        }
        if (!std::filesystem::exists(modelPath)) {
            if (required) {
                outFailReason = "Required animated template model not found: " + modelPath.string();
                outFailed = true;
            }
            return;
        }

        setActivity(0.62f, "Loading " + label + " model...");
        auto modelDataResult = fastgltf::GltfDataBuffer::FromPath(modelPath);
        if (modelDataResult.error() != fastgltf::Error::None) {
            if (required) {
                outFailReason = "Failed to read animated template model " + modelPath.string() + ": " +
                                std::string(fastgltf::getErrorName(modelDataResult.error()));
                outFailed = true;
            } else {
                spdlog::warn("[WorldAssetLoader] Failed to read {}: {}", modelPath.string(),
                             fastgltf::getErrorName(modelDataResult.error()));
            }
            return;
        }

        fastgltf::Parser modelParser(kAllExtensions);
        auto modelAssetResult = modelParser.loadGltf(modelDataResult.get(), modelPath.parent_path(),
                                                     fastgltf::Options::LoadExternalBuffers);
        if (modelAssetResult.error() != fastgltf::Error::None) {
            if (required) {
                outFailReason = "Failed to parse animated template model " + modelPath.string() + ": " +
                                std::string(fastgltf::getErrorName(modelAssetResult.error()));
                outFailed = true;
            } else {
                spdlog::warn("[WorldAssetLoader] Failed to parse {}: {}", modelPath.string(),
                             fastgltf::getErrorName(modelAssetResult.error()));
            }
            return;
        }

        fastgltf::Asset& modelAsset = modelAssetResult.get();
        const std::string modelAssetId = modelPath.lexically_normal().string();
        stageTexturesForAsset(modelAsset, modelPath, modelAssetId, label);
        if (isCancelled()) {
            return;
        }
        traverseScene(modelAsset, modelAssetId, glm::mat4{1.0f}, prototypeIndex, false, outMeshes, debugGroup, label);
        if (initializeAnimator) {
            animator.initializeFromAsset(modelAsset);
        }
    };

    outResult.routePoints.clear();
    if (markers.start.has_value()) {
        outResult.routePoints.push_back(*markers.start);
    }
    for (const auto& [index, waypoint] : markers.waypoints) {
        (void)index;
        outResult.routePoints.push_back(waypoint);
    }
    if (markers.end.has_value()) {
        outResult.routePoints.push_back(*markers.end);
    }

    if (!markers.start.has_value()) {
        outFailReason = "Map marker validation failed: required marker 'Start' was not found.";
        return false;
    }
    if (!markers.end.has_value()) {
        outFailReason = "Map marker validation failed: required marker 'End' was not found.";
        return false;
    }
    if (markers.waypoints.empty()) {
        outFailReason = "Map marker validation failed: at least one waypoint marker 'Waypoint_N' is required.";
        return false;
    }

    bool loadFailed = false;

    glm::vec3 startPos = *markers.start;
    glm::vec3 endPos = *markers.end;
    const glm::vec3 firstWaypoint = markers.waypoints.begin()->second;
    const glm::vec3 lastWaypoint = markers.waypoints.rbegin()->second;

    glm::mat4 startPlacement = makeYFacingTransform(startPos, firstWaypoint);
    glm::mat4 endPlacement = makeYFacingTransform(endPos, lastWaypoint);

    loadAndStagePlacedModel(spec.startModelPath,
                            startPlacement,
                            "start",
                            "start",
                            outResult.worldMeshes,
                            true,
                            loadFailed);
    if (loadFailed) {
        return false;
    }

    loadAndStagePlacedModel(spec.endModelPath,
                            endPlacement,
                            "end",
                            "end",
                            outResult.worldMeshes,
                            true,
                            loadFailed);
    if (loadFailed) {
        return false;
    }

    for (const auto& extra : spec.extraWorldModels) {
        glm::vec3 anchorPos{0.0f, 0.0f, 0.0f};
        glm::vec3 facingTarget{0.0f, 0.0f, 1.0f};
        bool hasAnchor = false;

        if (extra.anchor == WorldMarkerAnchor::Start) {
            anchorPos = startPos;
            facingTarget = firstWaypoint;
            hasAnchor = true;
        } else if (extra.anchor == WorldMarkerAnchor::End) {
            anchorPos = endPos;
            facingTarget = lastWaypoint;
            hasAnchor = true;
        }

        const glm::vec3 translation = (hasAnchor ? anchorPos : glm::vec3{0.0f, 0.0f, 0.0f}) + extra.positionOffset;
        glm::mat4 placement = composePlacementTransform(translation, extra.eulerDegrees, extra.scale);

        if (extra.facePath && hasAnchor) {
            glm::mat4 facing = makeYFacingTransform(translation, facingTarget);
            facing = facing * glm::rotate(glm::mat4{1.0f}, glm::radians(extra.eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            facing = facing * glm::rotate(glm::mat4{1.0f}, glm::radians(extra.eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            facing = facing * glm::scale(glm::mat4{1.0f}, extra.scale);
            placement = facing;
        }

        loadAndStagePlacedModel(extra.modelPath,
                                placement,
                                extra.debugLabel.empty() ? extra.modelPath.filename().string() : extra.debugLabel,
                                extra.debugGroup,
                                outResult.worldMeshes,
                                false,
                                loadFailed);
        if (loadFailed) {
            return false;
        }
    }

    if (spec.animatedTemplateModelPaths.empty()) {
        outFailReason = "World asset validation failed: at least one animated template model path is required.";
        return false;
    }

    for (std::size_t i = 0; i < spec.animatedTemplateModelPaths.size(); ++i) {
        const bool initializeAnimator = (i == 0);
        const std::string label = "animated template " + std::to_string(i + 1);
        loadAndStageModelTemplate(spec.animatedTemplateModelPaths[i],
                                  label,
                                  "template",
                                  static_cast<int>(i),
                                  outResult.templateMeshes,
                                  true,
                                  initializeAnimator,
                                  loadFailed);
        if (loadFailed) {
            return false;
        }
    }

    for (std::size_t i = 0; i < spec.towerTemplateModels.size(); ++i) {
        const WorldTemplateModelSpec& towerTemplate = spec.towerTemplateModels[i];
        const std::string label = "tower template:" + towerTemplate.id;
        const std::string debugGroup = "tower_template:" + towerTemplate.id;
        loadAndStageModelTemplate(towerTemplate.modelPath,
                                  label,
                                  debugGroup,
                                  static_cast<int>(i),
                                  outResult.towerTemplateMeshes,
                                  true,
                                  false,
                                  loadFailed);
        if (loadFailed) {
            return false;
        }
    }

    return true;
}
