#include "talent/RespecManager.h"

#include <algorithm>
#include <numeric>

namespace NST {

RespecManager::RespecManager(TalentGraph& graph)
    : m_graph(graph) {}

// ---------------------------------------------------------------
void RespecManager::recordInvestment(TalentGraph::NodeId nodeId, uint32_t points) {
    for (auto& inv : m_investments) {
        if (inv.nodeId == nodeId) {
            inv.points += points;
            return;
        }
    }
    m_investments.push_back({nodeId, points});
}

// ---------------------------------------------------------------
bool RespecManager::respec() {
    m_graph.reset();
    m_investments.clear();
    return true;
}

// ---------------------------------------------------------------
uint32_t RespecManager::totalPointsInvested() const noexcept {
    return static_cast<uint32_t>(
        std::accumulate(m_investments.cbegin(), m_investments.cend(), 0u,
            [](uint32_t acc, const Investment& inv) { return acc + inv.points; })
    );
}

// ---------------------------------------------------------------
uint32_t RespecManager::pointsInNode(TalentGraph::NodeId nodeId) const noexcept {
    auto it = std::ranges::find_if(m_investments,
        [nodeId](const Investment& inv) { return inv.nodeId == nodeId; });
    return it != m_investments.end() ? it->points : 0u;
}

// ---------------------------------------------------------------
bool RespecManager::hasInvestments() const noexcept {
    return !m_investments.empty();
}

} // namespace NST
