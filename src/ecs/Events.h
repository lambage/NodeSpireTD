#pragma once

#include <cstdint>
#include <vector>

#include "talent/StatModifier.h"

namespace NST {

// ---------------------------------------------------------------
// TalentUpgradedEvent
//
// Emitted after a point is successfully invested in a talent node.
// The TowerStatsSystem listens for this event and recalculates the
// TowerStatsComponent for the affected tower entity.
// ---------------------------------------------------------------
struct TalentUpgradedEvent {
    uint32_t towerId;                        // entity identifier
    uint32_t nodeId;                         // which node was leveled
    uint32_t newPointsInNode;                // updated currentPoints
    std::vector<StatModifier> activeModifiers; // full modifier list (all unlocked nodes)
};

// ---------------------------------------------------------------
// TalentRespecEvent
//
// Emitted after RespecManager::respec() completes.
// The TowerStatsSystem clears all modifiers for the tower and
// resets its TowerStatsComponent to base values.
// ---------------------------------------------------------------
struct TalentRespecEvent {
    uint32_t towerId;
};

} // namespace NST
