#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace NST {

// ---------------------------------------------------------------
// TowerStatsComponent
// Cached, pre-calculated tower statistics stored per ECS entity.
// Recalculated once when a TalentUpgradedEvent is observed; never
// evaluated per-frame.
// ---------------------------------------------------------------
struct TowerStatsComponent {
    float attackSpeed{1.0f};  // attacks per second
    float damage{10.0f};      // damage per hit
    float range{5.0f};        // tile radius
    float aoe{0.0f};          // AoE blast radius (0 = single target)
};

// ---------------------------------------------------------------
// TalentTreeComponent
// Ties an entity to a named talent tree and tracks resource usage.
// ---------------------------------------------------------------
struct TalentTreeComponent {
    uint32_t towerId{0};           // matches TalentUpgradedEvent::towerId
    uint32_t availablePoints{0};   // unspent talent points
    uint32_t spentPoints{0};       // total invested points
    char     treeId[64]{};         // e.g. "archer_tower"
};

// ---------------------------------------------------------------
// TransformComponent
// World-space position and uniform scale for a tower entity.
// ---------------------------------------------------------------
struct TransformComponent {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

} // namespace NST
