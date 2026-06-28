#pragma once

#include "talent/TalentGraph.h"

#include <cstdint>
#include <vector>


namespace NST {

// ---------------------------------------------------------------
// RespecManager
//
// Tracks every talent-point investment made to a single TalentGraph
// so the full investment can be reversed atomically (real-time respec).
//
// Usage:
//   manager.recordInvestment(nodeId, pointsSpent);
//   ...
//   manager.respec();   // resets the graph and clears the ledger
// ---------------------------------------------------------------
class RespecManager {
  public:
    explicit RespecManager(TalentGraph& graph);

    // Record that 'points' points were invested in 'nodeId'.
    // Accumulates on top of any previous investment in the same node.
    void recordInvestment(TalentGraph::NodeId nodeId, uint32_t points);

    // Wipe all point investments:
    //   1. Calls TalentGraph::reset() – zeroes currentPoints on every node.
    //   2. Clears the internal investment ledger.
    // Returns true (reserved for future resource-refund logic).
    bool respec();

    // Total resource points spent since the last respec.
    [[nodiscard]] uint32_t totalPointsInvested() const noexcept;

    // Number of points invested in a specific node (0 if never touched).
    [[nodiscard]] uint32_t pointsInNode(TalentGraph::NodeId nodeId) const noexcept;

    // True if any points have been invested since the last respec.
    [[nodiscard]] bool hasInvestments() const noexcept;

  private:
    struct Investment {
        TalentGraph::NodeId nodeId;
        uint32_t points;
    };

    TalentGraph& m_graph;
    std::vector<Investment> m_investments;
};

} // namespace NST
