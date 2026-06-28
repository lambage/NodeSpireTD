#pragma once

#include "talent/StatModifier.h"

#include <cstdint>
#include <string>
#include <vector>


namespace NST {

// ---------------------------------------------------------------
// A single node inside a TalentGraph.
// Each point spent in a node applies all of its modifiers once
// (the modifier list represents one "level" worth of bonuses).
// ---------------------------------------------------------------
struct TalentNode {
    uint32_t id{0};
    std::string name;
    std::string description;

    uint32_t maxPoints{1};     // How many times this node can be leveled
    uint32_t currentPoints{0}; // Current investment (0 = locked)
    uint32_t pointCost{1};     // Resource cost per level

    // Modifiers granted per level of this node.
    std::vector<StatModifier> modifiers;

    // IDs of nodes that must have at least one point before this
    // node can be unlocked (filled by TalentGraph::addDependency).
    std::vector<uint32_t> prerequisites;

    // Flat position hint for the node-graph UI (ImNodes).
    float posX{0.0f};
    float posY{0.0f};

    [[nodiscard]] bool isUnlocked() const noexcept {
        return currentPoints > 0;
    }
    [[nodiscard]] bool isMaxed() const noexcept {
        return currentPoints >= maxPoints;
    }
};

} // namespace NST
