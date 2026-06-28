#include "talent/TalentGraph.h"

#include <algorithm>
#include <stack>

namespace NST {

// ---------------------------------------------------------------
bool TalentGraph::addNode(TalentNode node) {
    auto id = node.id;
    return m_nodes.emplace(id, std::move(node)).second;
}

// ---------------------------------------------------------------
bool TalentGraph::removeNode(NodeId id) {
    if (!m_nodes.contains(id)) {
        return false;
    }

    m_nodes.erase(id);

    // Remove edges that reference the deleted node.
    std::erase_if(m_edges, [id](const auto& e) { return e.first == id || e.second == id; });

    // Remove stale prerequisites from remaining nodes.
    for (auto& [nid, node] : m_nodes) {
        std::erase(node.prerequisites, id);
    }

    return true;
}

// ---------------------------------------------------------------
bool TalentGraph::addDependency(NodeId from, NodeId to) {
    if (!m_nodes.contains(from) || !m_nodes.contains(to)) {
        return false;
    }

    // 'to' depends on 'from'; adding (from→to) is fine unless 'to' can
    // already reach 'from' (which would create a cycle).
    if (hasCircularDependency(to, from)) {
        return false;
    }

    // Deduplicate edges.
    for (const auto& [f, t] : m_edges) {
        if (f == from && t == to) {
            return true; // already present
        }
    }

    m_edges.emplace_back(from, to);
    m_nodes.at(to).prerequisites.push_back(from);
    return true;
}

// ---------------------------------------------------------------
bool TalentGraph::canUnlockNode(NodeId id) const {
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) {
        return false;
    }

    const auto& node = it->second;
    if (node.isMaxed()) {
        return false;
    }

    for (auto prereqId : node.prerequisites) {
        auto prereqIt = m_nodes.find(prereqId);
        if (prereqIt == m_nodes.end() || !prereqIt->second.isUnlocked()) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------
bool TalentGraph::addPointToNode(NodeId id) {
    if (!canUnlockNode(id)) {
        return false;
    }
    m_nodes.at(id).currentPoints++;
    return true;
}

// ---------------------------------------------------------------
bool TalentGraph::unlockNode(NodeId id) {
    return addPointToNode(id);
}

// ---------------------------------------------------------------
void TalentGraph::reset() {
    for (auto& [id, node] : m_nodes) {
        node.currentPoints = 0;
    }
}

// ---------------------------------------------------------------
const TalentNode* TalentGraph::getNode(NodeId id) const {
    auto it = m_nodes.find(id);
    return it != m_nodes.end() ? &it->second : nullptr;
}

TalentNode* TalentGraph::getNode(NodeId id) {
    auto it = m_nodes.find(id);
    return it != m_nodes.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------
const std::unordered_map<TalentGraph::NodeId, TalentNode>& TalentGraph::nodes() const noexcept {
    return m_nodes;
}

const std::vector<std::pair<TalentGraph::NodeId, TalentGraph::NodeId>>& TalentGraph::edges() const noexcept {
    return m_edges;
}

// ---------------------------------------------------------------
std::vector<StatModifier> TalentGraph::collectActiveModifiers() const {
    std::vector<StatModifier> result;
    for (const auto& [id, node] : m_nodes) {
        if (!node.isUnlocked()) {
            continue;
        }
        // Each invested point contributes one full set of modifiers.
        for (uint32_t p = 0; p < node.currentPoints; ++p) {
            for (const auto& mod : node.modifiers) {
                result.push_back(mod);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------
bool TalentGraph::hasCircularDependency(NodeId start, NodeId target) const {
    // Iterative DFS following the edge direction (from → to).
    std::stack<NodeId> stack;
    stack.push(start);

    while (!stack.empty()) {
        NodeId current = stack.top();
        stack.pop();

        if (current == target) {
            return true;
        }

        for (const auto& [f, t] : m_edges) {
            if (f == current) {
                stack.push(t);
            }
        }
    }
    return false;
}

} // namespace NST
