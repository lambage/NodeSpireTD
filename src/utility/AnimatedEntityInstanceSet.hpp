#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

class AnimatedEntityInstanceSet {
  public:
  struct Instance {
    glm::mat4 transform{1.0f};
    int prototypeIndex = 0;
    std::string debugGroup;
    std::string debugLabel;
  };

  void setTransforms(const std::vector<glm::mat4>& transforms) {
    instances_.clear();
    instances_.reserve(transforms.size());
    for (const glm::mat4& transform : transforms) {
      Instance instance;
      instance.transform = transform;
      instances_.push_back(std::move(instance));
    }
  }

  void setInstances(std::vector<Instance> instances) { instances_ = std::move(instances); }
  void clear() { instances_.clear(); }
  bool empty() const { return instances_.empty(); }
  std::size_t size() const { return instances_.size(); }
  const Instance* instance(std::size_t idx) const {
    if (idx >= instances_.size()) {
      return nullptr;
    }
    return &instances_[idx];
  }

    template <typename MeshT, typename ResolveNodeTransformFn, typename VisitorFn>
    void forEachMeshWorldTransform(const std::vector<MeshT>& templateMeshes,
                                   ResolveNodeTransformFn&& resolveNodeTransform,
                                   VisitorFn&& visitor) const {
    for (std::size_t instanceIdx = 0; instanceIdx < instances_.size(); ++instanceIdx) {
      const Instance& instance = instances_[instanceIdx];
      const glm::mat4& instanceTransform = instance.transform;
            for (std::size_t meshIdx = 0; meshIdx < templateMeshes.size(); ++meshIdx) {
                const MeshT& mesh = templateMeshes[meshIdx];
        if (mesh.templatePrototypeIndex >= 0 && mesh.templatePrototypeIndex != instance.prototypeIndex) {
          continue;
        }
                const glm::mat4 animatedNodeTransform =
            resolveNodeTransform(static_cast<int>(instanceIdx), mesh.sourceNodeIndex, mesh.modelTransform);
                const glm::mat4 world = instanceTransform * animatedNodeTransform;
                visitor(mesh, world, static_cast<int>(instanceIdx), static_cast<int>(meshIdx));
            }
        }
    }

  private:
  std::vector<Instance> instances_;
};
