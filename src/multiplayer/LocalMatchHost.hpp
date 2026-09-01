#pragma once

#include "multiplayer/LocalHostCommandGate.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace multiplayer {

class LocalMatchHost {
  public:
    using AuthoritativeCommandHandler =
        std::function<std::optional<CommandRejectionReason>(const PlayerCommandRequest&)>;

    bool registerPlayer(PlayerId playerId);
    bool unregisterPlayer(PlayerId playerId);

    // The handler validates and applies accepted commands atomically in the authoritative simulation.
    std::optional<std::string> processCommand(std::string_view payload,
                                              SimulationTick currentTick,
                                              const AuthoritativeCommandHandler& handleCommand);

  private:
    LocalHostCommandGate commandGate_;
};

} // namespace multiplayer