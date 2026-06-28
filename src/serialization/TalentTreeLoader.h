#pragma once

#include "talent/TalentGraph.h"

#include <filesystem>
#include <optional>
#include <string>


namespace NST {

// ---------------------------------------------------------------
// TalentTreeLoader
//
// Deserializes talent tree configurations from external JSON files.
// No hard-coded data: every node, edge, and modifier lives in JSON.
//
// Expected JSON schema (see data/talent_trees/*.json for examples):
//
//  {
//    "tree_id"    : "archer_tower",
//    "tree_name"  : "Archer Tower Talents",
//    "nodes" : [
//      {
//        "id"          : 1,
//        "name"        : "Rapid Fire",
//        "description" : "Increases attack speed.",
//        "max_points"  : 3,
//        "point_cost"  : 1,
//        "pos_x"       : 0.0,
//        "pos_y"       : 0.0,
//        "modifiers"   : [
//          { "id":"rf_as", "stat":"attack_speed", "op":"additive", "value":0.1 }
//        ]
//      }
//    ],
//    "edges" : [
//      { "from": 1, "to": 2 }
//    ]
//  }
//
// Supported stat strings  : "attack_speed", "damage", "range", "aoe"
// Supported op strings    : "additive", "multiplicative", "override"
// ---------------------------------------------------------------
class TalentTreeLoader {
  public:
    /// Load a TalentGraph from a JSON file on disk.
    /// Returns std::nullopt if the file cannot be opened or parsed.
    [[nodiscard]] static std::optional<TalentGraph> loadFromFile(const std::filesystem::path& path);

    /// Load a TalentGraph from an in-memory JSON string.
    /// Returns std::nullopt on parse error.
    [[nodiscard]] static std::optional<TalentGraph> loadFromString(const std::string& json);
};

} // namespace NST
