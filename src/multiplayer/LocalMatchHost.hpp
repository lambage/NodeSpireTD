#pragma once

#include "multiplayer/LocalHostCommandGate.hpp"
#include "multiplayer/LocalHostJoinGate.hpp"
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

    LocalMatchHost(std::size_t maxPlayers = LocalHostJoinGate::kDefaultMaxPlayers) : joinGate_(maxPlayers) {}

    bool registerPlayer(PlayerId playerId);
    bool unregisterPlayer(PlayerId playerId);

    // The handler validates and applies accepted commands atomically in the authoritative simulation.
    std::optional<std::string> processCommand(std::string_view payload,
                                              SimulationTick currentTick,
                                              const AuthoritativeCommandHandler& handleCommand);

    // Result of validating one raw JoinMatchRequest payload. serializedResult is always populated
    // on success and must be sent back to the peer regardless of accept/reject. acceptedPlayerId
    // is set only on acceptance -- the caller must still call registerPlayer(*acceptedPlayerId)
    // (and any simulation-side registration) before marking the transport peer as joined.
    struct JoinRequestOutcome {
        std::string serializedResult;
        std::optional<PlayerId> acceptedPlayerId;
    };

    // Returns nullopt only if the (rejection or acceptance) result itself failed to serialize.
    std::optional<JoinRequestOutcome> processJoinRequest(std::string_view payload,
                                                         std::string_view expectedContentManifestSha256,
                                                         SimulationTick currentTick);

  private:
    LocalHostCommandGate commandGate_;
    LocalHostJoinGate joinGate_;
};

} // namespace multiplayer