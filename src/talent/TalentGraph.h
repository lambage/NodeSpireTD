#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "talent/TalentNode.h"

namespace NST {

// ---------------------------------------------------------------
// Directed acyclic graph modelling an MMO-style talent tree.
//
// Nodes represent individual talents or specifications.
// A directed edge  (A → B)  means "A is a prerequisite of B".
//
// The graph:
//   * Prevents circular dependencies.
//   * Enforces per-node level caps (maxPoints).
//   * Validates all prerequisites before allowing a node unlock.
// ---------------------------------------------------------------
class TalentGraph {
public:
    using NodeId = uint32_t;

    // ----- Mutation ------------------------------------------------

    /// Add a node. Returns false if a node with the same id already exists.
    bool addNode(TalentNode node);

    /// Remove a node and all edges connected to it.
    bool removeNode(NodeId id);

    /// Add a prerequisite dependency: 'from' must be unlocked before 'to'.
    /// Rejected if either node is missing or the edge would create a cycle.
    bool addDependency(NodeId from, NodeId to);

    // ----- Talent point management ---------------------------------

    /// Returns true when all prerequisites are met and the node is
    /// not yet at maxPoints.
    [[nodiscard]] bool canUnlockNode(NodeId id) const;

    /// Spend one point in the node (checks prerequisites + cap).
    /// Dispatches no events; callers must emit TalentUpgradedEvent.
    bool addPointToNode(NodeId id);

    /// Convenience alias for addPointToNode.
    bool unlockNode(NodeId id);

    /// Reset all node point counts to zero (used by RespecManager).
    void reset();

    // ----- Accessors -----------------------------------------------
    [[nodiscard]] const TalentNode* getNode(NodeId id) const;
    [[nodiscard]] TalentNode*       getNode(NodeId id);

    [[nodiscard]] const std::unordered_map<NodeId, TalentNode>&        nodes() const noexcept;
    [[nodiscard]] const std::vector<std::pair<NodeId, NodeId>>&        edges() const noexcept;

    /// Returns all modifiers that are currently active (from unlocked nodes).
    [[nodiscard]] std::vector<StatModifier> collectActiveModifiers() const;

private:
    /// DFS from 'start'; returns true if 'target' is reachable – used to
    /// detect cycles before inserting a new edge.
    [[nodiscard]] bool hasCircularDependency(NodeId start, NodeId target) const;

    std::unordered_map<NodeId, TalentNode>   m_nodes;
    std::vector<std::pair<NodeId, NodeId>>   m_edges;  // (from, to) prerequisite pairs
};

} // namespace NST
