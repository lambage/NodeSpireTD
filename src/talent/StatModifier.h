#pragma once

#include <cstdint>
#include <string>

namespace NST {

// ---------------------------------------------------------------
// Stat types tracked by TowerStatsComponent.
// ---------------------------------------------------------------
enum class StatType : uint32_t {
    AttackSpeed = 0,
    Damage,
    Range,
    AoE,
    Count
};

// ---------------------------------------------------------------
// How a modifier is applied when stats are recalculated.
//   Additive       : stat += value
//   Multiplicative : stat *= (1 + value)
//   Override       : stat  = value  (applied last)
// ---------------------------------------------------------------
enum class ModifierOp {
    Additive,
    Multiplicative,
    Override,
};

// ---------------------------------------------------------------
// A single modifier produced by one level of a TalentNode.
// ---------------------------------------------------------------
struct StatModifier {
    std::string id; // unique identifier (e.g. "rapid_fire_as_1")
    StatType stat{StatType::Damage};
    ModifierOp op{ModifierOp::Additive};
    float value{0.0f};
};

} // namespace NST
