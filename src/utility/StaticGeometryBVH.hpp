#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

struct WorldStagedMesh;

// World-space triangle BVH over the static level geometry. Placement validation fires dozens of
// rays per frame (cursor pick + footprint probes + slide/bisect probes); a brute-force scan of
// every staged triangle collapses framerate on large levels.
class StaticGeometryBVH {
  public:
    struct Hit {
        float distance = 0.0f;
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        std::uint32_t meshIndex = 0;
    };

    void build(const std::vector<WorldStagedMesh>& meshes);
    void clear();
    bool empty() const { return triangles_.empty(); }
    bool raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir, Hit& outHit) const;

  private:
    struct Triangle {
        glm::vec3 v0{0.0f};
        glm::vec3 v1{0.0f};
        glm::vec3 v2{0.0f};
        glm::vec3 n0{0.0f, 1.0f, 0.0f};
        glm::vec3 n1{0.0f, 1.0f, 0.0f};
        glm::vec3 n2{0.0f, 1.0f, 0.0f};
        glm::vec3 centroid{0.0f};
        std::uint32_t meshIndex = 0;
    };

    struct Node {
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        std::uint32_t firstTriangle = 0;
        std::uint32_t triangleCount = 0; // 0 means interior node
        std::uint32_t leftChild = 0;
        std::uint32_t rightChild = 0;
    };

    std::uint32_t buildNode(std::uint32_t first, std::uint32_t count);

    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
};
