#pragma once

#include "utility/WorldGeometryTypes.hpp"

#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
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

struct WorldAssetSpec {
    std::filesystem::path startModelPath = std::filesystem::path("assets") / "models" / "portal" / "portal.glb";
    std::filesystem::path endModelPath = std::filesystem::path("assets") / "models" / "base" / "base.glb";
    std::vector<std::filesystem::path> animatedTemplateModelPaths{
        std::filesystem::path("assets") / "models" / "enemy" / "goblin1.glb"
    };
    std::vector<WorldModelPlacementSpec> extraWorldModels;
    std::vector<WorldUiTextureSpec> uiTextures;
};

struct WorldStagedMesh {
    std::vector<WorldVertex> vertices;
    std::vector<uint32_t> indices;
    std::size_t imageIndex = SIZE_MAX;
    glm::mat4 modelTransform{1.0f};
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

struct WorldAssetLoadResult {
    std::vector<WorldStagedMesh> worldMeshes;
    std::vector<WorldStagedMesh> templateMeshes;
    std::vector<WorldStagedTexture> textures;
    std::vector<glm::vec3> routePoints;
};

class WorldAssetLoader {
  public:
    using IsCancelledFn = std::function<bool()>;
    using ActivityFn = std::function<void(float, const std::string&)>;

    bool load(const std::filesystem::path& assetPath,
              const WorldAssetSpec& spec,
              TemplateAnimator& animator,
              const IsCancelledFn& isCancelled,
              const ActivityFn& setActivity,
              WorldAssetLoadResult& outResult,
              std::string& outFailReason) const;
};
