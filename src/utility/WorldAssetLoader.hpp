#pragma once

#include "utility/WorldGeometryTypes.hpp"

#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

class TemplateAnimator;

enum class WorldMarkerAnchor {
    None,
    Start,
    End
};

struct WorldModelPlacementSpec {
    std::filesystem::path modelPath;
    std::string debugGroup = "prop";
    std::string debugLabel;
    WorldMarkerAnchor anchor = WorldMarkerAnchor::None;
    bool facePath = false;
    glm::vec3 positionOffset{0.0f, 0.0f, 0.0f};
    glm::vec3 eulerDegrees{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct WorldUiTextureSpec {
    std::string id;
    std::filesystem::path texturePath;
};

struct WorldTemplateModelSpec {
    std::string id;
    std::filesystem::path modelPath;
};

struct WorldAssetSpec {
    std::filesystem::path startModelPath{};
    std::filesystem::path endModelPath{};
    std::vector<std::filesystem::path> animatedTemplateModelPaths{};
    std::vector<WorldTemplateModelSpec> towerTemplateModels{};
    std::vector<WorldModelPlacementSpec> extraWorldModels;
    std::vector<WorldUiTextureSpec> uiTextures;
};

struct WorldStagedMesh {
    std::vector<WorldVertex> vertices;
    std::vector<uint32_t> indices;
    std::size_t imageIndex = SIZE_MAX;
    glm::mat4 modelTransform{1.0f};
    glm::mat4 groupRootTransform{1.0f};
    int templatePrototypeIndex = -1;
    int sourceNodeIndex = -1;
    int sourceSkinIndex = -1;
    std::string debugGroup;
    std::string debugLabel;
    glm::vec3 localBoundsCenter{0.0f, 0.0f, 0.0f};
    float localBoundsRadius = 0.5f;
};

struct WorldStagedTexture {
    std::size_t imageIndex = SIZE_MAX;
    std::string displayName;
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Tower placement region types
enum class TowerPlacementRegionType {
    Ground = 0,
    Cliff = 1,
    Water = 2
};

inline std::string towerPlacementRegionTypeToString(TowerPlacementRegionType type) {
    switch (type) {
        case TowerPlacementRegionType::Ground:  return "ground";
        case TowerPlacementRegionType::Cliff:   return "cliff";
        case TowerPlacementRegionType::Water:   return "water";
        default:                                 return "unknown";
    }
}

inline bool towerPlacementRegionTypeFromString(std::string_view str, TowerPlacementRegionType& outType) {
    if (str == "ground") {
        outType = TowerPlacementRegionType::Ground;
        return true;
    } else if (str == "cliff") {
        outType = TowerPlacementRegionType::Cliff;
        return true;
    } else if (str == "water") {
        outType = TowerPlacementRegionType::Water;
        return true;
    }
    return false;
}

struct TowerPlacementRegion {
    std::string name;
    TowerPlacementRegionType type = TowerPlacementRegionType::Ground;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    glm::vec3 center{0.0f};
};

struct WorldAssetLoadResult {
    std::vector<WorldStagedMesh> worldMeshes;
    std::vector<WorldStagedMesh> templateMeshes;
    std::vector<WorldStagedMesh> towerTemplateMeshes;
    std::vector<WorldStagedTexture> textures;
    std::vector<glm::vec3> routePoints;
    std::vector<TowerPlacementRegion> placementRegions;
};

class WorldAssetLoader {
  public:
    using IsCancelledFn = std::function<bool()>;
    using ActivityFn = std::function<void(float, const std::string&)>;

    // animators is indexed by template prototype index (i.e. index into
    // spec.animatedTemplateModelPaths) -- one independent TemplateAnimator per animated enemy
    // template, since each template can have its own skeleton/bind pose even when clip names
    // (Idle/Walking/Death) coincide. Resized and populated by this call; any previous contents
    // are discarded. towerAnimators uses spec.towerTemplateModels indices and contains an
    // animator only when that tower model defines the optional tower_animation clip.
    bool load(const std::filesystem::path& assetPath,
              const WorldAssetSpec& spec,
              std::vector<std::unique_ptr<TemplateAnimator>>& animators,
              std::vector<std::unique_ptr<TemplateAnimator>>& towerAnimators,
              const IsCancelledFn& isCancelled,
              const ActivityFn& setActivity,
              WorldAssetLoadResult& outResult,
              std::string& outFailReason) const;
};
