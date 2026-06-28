#include "serialization/TalentTreeLoader.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace NST {

namespace {

// ---------------------------------------------------------------
// Helper: map JSON string to StatType enum.
// ---------------------------------------------------------------
StatType statTypeFromString(const std::string& s) {
    if (s == "attack_speed") return StatType::AttackSpeed;
    if (s == "damage")       return StatType::Damage;
    if (s == "range")        return StatType::Range;
    if (s == "aoe")          return StatType::AoE;
    throw std::invalid_argument("Unknown stat type: " + s);
}

// ---------------------------------------------------------------
// Helper: map JSON string to ModifierOp enum.
// ---------------------------------------------------------------
ModifierOp modifierOpFromString(const std::string& s) {
    if (s == "additive")       return ModifierOp::Additive;
    if (s == "multiplicative") return ModifierOp::Multiplicative;
    if (s == "override")       return ModifierOp::Override;
    throw std::invalid_argument("Unknown modifier op: " + s);
}

// ---------------------------------------------------------------
// Core parse logic shared by both public entry points.
// ---------------------------------------------------------------
std::optional<TalentGraph> parseJson(const nlohmann::json& j) {
    TalentGraph graph;

    // Parse nodes.
    for (const auto& nodeJson : j.at("nodes")) {
        TalentNode node;
        node.id          = nodeJson.at("id").get<uint32_t>();
        node.name        = nodeJson.at("name").get<std::string>();
        node.description = nodeJson.value("description", "");
        node.maxPoints   = nodeJson.value("max_points",  1u);
        node.pointCost   = nodeJson.value("point_cost",  1u);
        node.posX        = nodeJson.value("pos_x", 0.0f);
        node.posY        = nodeJson.value("pos_y", 0.0f);

        if (nodeJson.contains("modifiers")) {
            for (const auto& modJson : nodeJson.at("modifiers")) {
                StatModifier mod;
                mod.id    = modJson.value("id", "");
                mod.stat  = statTypeFromString(modJson.at("stat").get<std::string>());
                mod.op    = modifierOpFromString(modJson.value("op", "additive"));
                mod.value = modJson.at("value").get<float>();
                node.modifiers.push_back(std::move(mod));
            }
        }

        if (!graph.addNode(std::move(node))) {
            // Duplicate ID – fail loudly so data issues surface quickly.
            return std::nullopt;
        }
    }

    // Parse dependency edges.
    if (j.contains("edges")) {
        for (const auto& edgeJson : j.at("edges")) {
            uint32_t from = edgeJson.at("from").get<uint32_t>();
            uint32_t to   = edgeJson.at("to").get<uint32_t>();
            if (!graph.addDependency(from, to)) {
                return std::nullopt; // missing node or cycle detected
            }
        }
    }

    return graph;
}

} // anonymous namespace

// ---------------------------------------------------------------
std::optional<TalentGraph>
TalentTreeLoader::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        return parseJson(j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------
std::optional<TalentGraph>
TalentTreeLoader::loadFromString(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        return parseJson(j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace NST
