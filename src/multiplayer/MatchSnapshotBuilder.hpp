#pragma once

#include "multiplayer/MatchSimulation.hpp"

#include <optional>
#include <string>

namespace multiplayer {

class MatchSnapshotBuilder {
  public:
    static std::optional<std::string> serialize(const MatchSimulation& simulation);
};

} // namespace multiplayer