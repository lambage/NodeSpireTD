#include "utility/StaticGeometryBVH.hpp"

#include "utility/WorldAssetLoader.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {

constexpr std::uint32_t kMaxTrianglesPerLeaf = 8;
constexpr float kRayTMin = 1e-4f;

bool rayTriangle(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& v0, const glm::vec3& v1,
                 const glm::vec3& v2, float& outT, glm::vec3& outBary) {
    const glm::vec3 edge1 = v1 - v0;
    const glm::vec3 edge2 = v2 - v0;
    const glm::vec3 pvec = glm::cross(dir, edge2);
    const float det = glm::dot(edge1, pvec);
    if (std::abs(det) < 1e-9f) {
        return false;
    }
    const float invDet = 1.0f / det;
    const glm::vec3 tvec = origin - v0;
    const float u = glm::dot(tvec, pvec) * invDet;
    if (u < -1e-5f || u > 1.0f + 1e-5f) {
        return false;
    }
    const glm::vec3 qvec = glm::cross(tvec, edge1);
    const float v = glm::dot(dir, qvec) * invDet;
    if (v < -1e-5f || (u + v) > 1.0f + 1e-5f) {
        return false;
    }
    const float t = glm::dot(edge2, qvec) * invDet;
    if (t <= kRayTMin) {
        return false;
    }
    outT = t;
    outBary = glm::vec3(1.0f - u - v, u, v);
    return true;
}

bool rayAabb(const glm::vec3& origin, const glm::vec3& invDir, const glm::vec3& boundsMin, const glm::vec3& boundsMax,
             float tMax) {
    const glm::vec3 t0 = (boundsMin - origin) * invDir;
    const glm::vec3 t1 = (boundsMax - origin) * invDir;
    const glm::vec3 tSmall = glm::min(t0, t1);
    const glm::vec3 tBig = glm::max(t0, t1);
    const float tEnter = std::max(std::max(tSmall.x, tSmall.y), std::max(tSmall.z, kRayTMin));
    const float tExit = std::min(std::min(tBig.x, tBig.y), std::min(tBig.z, tMax));
    return tEnter <= tExit;
}

} // namespace

void StaticGeometryBVH::clear() {
    triangles_.clear();
    nodes_.clear();
}

void StaticGeometryBVH::build(const std::vector<WorldStagedMesh>& meshes) {
    clear();

    std::size_t triangleEstimate = 0;
    for (const WorldStagedMesh& mesh : meshes) {
        triangleEstimate += mesh.indices.size() / 3;
    }
    triangles_.reserve(triangleEstimate);

    for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        const WorldStagedMesh& mesh = meshes[meshIndex];
        const glm::mat4& world = mesh.modelTransform;
        const glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(world)));

        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const std::uint32_t i0 = mesh.indices[i];
            const std::uint32_t i1 = mesh.indices[i + 1];
            const std::uint32_t i2 = mesh.indices[i + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                continue;
            }

            Triangle tri;
            tri.v0 = glm::vec3(world * glm::vec4(mesh.vertices[i0].position, 1.0f));
            tri.v1 = glm::vec3(world * glm::vec4(mesh.vertices[i1].position, 1.0f));
            tri.v2 = glm::vec3(world * glm::vec4(mesh.vertices[i2].position, 1.0f));
            tri.n0 = normalMat * mesh.vertices[i0].normal;
            tri.n1 = normalMat * mesh.vertices[i1].normal;
            tri.n2 = normalMat * mesh.vertices[i2].normal;
            tri.centroid = (tri.v0 + tri.v1 + tri.v2) * (1.0f / 3.0f);
            tri.meshIndex = static_cast<std::uint32_t>(meshIndex);
            triangles_.push_back(tri);
        }
    }

    if (triangles_.empty()) {
        return;
    }

    nodes_.reserve((triangles_.size() / kMaxTrianglesPerLeaf) * 2 + 8);
    buildNode(0, static_cast<std::uint32_t>(triangles_.size()));
}

std::uint32_t StaticGeometryBVH::buildNode(std::uint32_t first, std::uint32_t count) {
    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(nodes_.size());
    nodes_.emplace_back();

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    glm::vec3 centroidMin(std::numeric_limits<float>::max());
    glm::vec3 centroidMax(std::numeric_limits<float>::lowest());
    for (std::uint32_t i = first; i < first + count; ++i) {
        const Triangle& tri = triangles_[i];
        boundsMin = glm::min(boundsMin, glm::min(tri.v0, glm::min(tri.v1, tri.v2)));
        boundsMax = glm::max(boundsMax, glm::max(tri.v0, glm::max(tri.v1, tri.v2)));
        centroidMin = glm::min(centroidMin, tri.centroid);
        centroidMax = glm::max(centroidMax, tri.centroid);
    }

    {
        Node& node = nodes_[nodeIndex];
        node.boundsMin = boundsMin;
        node.boundsMax = boundsMax;
        node.firstTriangle = first;
        node.triangleCount = count;
    }

    if (count <= kMaxTrianglesPerLeaf) {
        return nodeIndex;
    }

    const glm::vec3 extent = centroidMax - centroidMin;
    int axis = 0;
    if (extent.y > extent.x) {
        axis = 1;
    }
    if (extent.z > ((axis == 0) ? extent.x : extent.y)) {
        axis = 2;
    }
    if (extent[axis] <= 1e-6f) {
        return nodeIndex;
    }

    const auto begin = triangles_.begin() + static_cast<std::ptrdiff_t>(first);
    const auto end = begin + static_cast<std::ptrdiff_t>(count);
    const auto mid = begin + static_cast<std::ptrdiff_t>(count / 2);
    std::nth_element(begin, mid, end, [axis](const Triangle& a, const Triangle& b) {
        return a.centroid[axis] < b.centroid[axis];
    });

    const std::uint32_t leftCount = count / 2;
    const std::uint32_t leftChild = buildNode(first, leftCount);
    const std::uint32_t rightChild = buildNode(first + leftCount, count - leftCount);

    Node& node = nodes_[nodeIndex];
    node.triangleCount = 0;
    node.leftChild = leftChild;
    node.rightChild = rightChild;
    return nodeIndex;
}

bool StaticGeometryBVH::raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir, Hit& outHit) const {
    if (nodes_.empty() || triangles_.empty()) {
        return false;
    }

    const float dirLenSq = glm::dot(rayDir, rayDir);
    if (dirLenSq <= 1e-12f) {
        return false;
    }
    const glm::vec3 dir = rayDir / std::sqrt(dirLenSq);
    const glm::vec3 invDir(1.0f / ((dir.x != 0.0f) ? dir.x : 1e-20f), 1.0f / ((dir.y != 0.0f) ? dir.y : 1e-20f),
                           1.0f / ((dir.z != 0.0f) ? dir.z : 1e-20f));

    float bestT = std::numeric_limits<float>::max();
    bool anyHit = false;

    std::uint32_t stack[128];
    int stackSize = 0;
    stack[stackSize++] = 0;

    while (stackSize > 0) {
        const Node& node = nodes_[stack[--stackSize]];
        if (!rayAabb(rayOrigin, invDir, node.boundsMin, node.boundsMax, bestT)) {
            continue;
        }

        if (node.triangleCount == 0) {
            if (stackSize + 2 <= static_cast<int>(std::size(stack))) {
                stack[stackSize++] = node.leftChild;
                stack[stackSize++] = node.rightChild;
            }
            continue;
        }

        for (std::uint32_t i = node.firstTriangle; i < node.firstTriangle + node.triangleCount; ++i) {
            const Triangle& tri = triangles_[i];
            float t = 0.0f;
            glm::vec3 bary{};
            if (!rayTriangle(rayOrigin, dir, tri.v0, tri.v1, tri.v2, t, bary) || t >= bestT) {
                continue;
            }

            bestT = t;
            anyHit = true;
            outHit.distance = t;
            outHit.position = rayOrigin + dir * t;
            glm::vec3 normal = bary.x * tri.n0 + bary.y * tri.n1 + bary.z * tri.n2;
            const float normalLenSq = glm::dot(normal, normal);
            normal = (normalLenSq > 1e-12f) ? (normal / std::sqrt(normalLenSq)) : glm::vec3(0.0f, 1.0f, 0.0f);
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z)) {
                normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            outHit.normal = normal;
            outHit.meshIndex = tri.meshIndex;
        }
    }

    return anyHit;
}
