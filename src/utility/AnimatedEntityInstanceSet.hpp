#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

class AnimatedEntityInstanceSet {
  public:
    void setTransforms(const std::vector<glm::mat4>& transforms) { transforms_ = transforms; }
    void clear() { transforms_.clear(); }
    bool empty() const { return transforms_.empty(); }
    std::size_t size() const { return transforms_.size(); }

    template <typename MeshT, typename ResolveNodeTransformFn, typename VisitorFn>
    void forEachMeshWorldTransform(const std::vector<MeshT>& templateMeshes,
                                   ResolveNodeTransformFn&& resolveNodeTransform,
                                   VisitorFn&& visitor) const {
        for (std::size_t instanceIdx = 0; instanceIdx < transforms_.size(); ++instanceIdx) {
            const glm::mat4& instanceTransform = transforms_[instanceIdx];
            for (std::size_t meshIdx = 0; meshIdx < templateMeshes.size(); ++meshIdx) {
                const MeshT& mesh = templateMeshes[meshIdx];
                const glm::mat4 animatedNodeTransform =
                    resolveNodeTransform(mesh.sourceNodeIndex, mesh.modelTransform);
                const glm::mat4 world = instanceTransform * animatedNodeTransform;
                visitor(mesh, world, static_cast<int>(instanceIdx), static_cast<int>(meshIdx));
            }
        }
    }

  private:
    std::vector<glm::mat4> transforms_;
};
