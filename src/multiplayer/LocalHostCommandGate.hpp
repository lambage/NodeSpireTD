#pragma once

#include "multiplayer/MatchProtocol.hpp"

#include <functional>
#include <optional>
#include <unordered_map>

namespace multiplayer {

class LocalHostCommandGate {
  public:
    using DomainValidationFn = std::function<std::optional<CommandRejectionReason>(const PlayerCommandRequest&)>;

    bool registerPlayer(PlayerId playerId) {
        if (playerId == 0 || players_.contains(playerId)) {
            return false;
        }
        players_.emplace(playerId, PlayerCommandState{});
        return true;
    }

    bool unregisterPlayer(PlayerId playerId) {
        return players_.erase(playerId) != 0;
    }

    bool hasPlayer(PlayerId playerId) const {
        return players_.contains(playerId);
    }

    PlayerCommandResult validateAndApply(const PlayerCommandRequest& request,
                                         SimulationTick currentTick,
                                         const DomainValidationFn& validateDomainCommand) {
        if (request.protocolVersion != kMatchProtocolVersion) {
            return reject(request, CommandRejectionReason::UnsupportedProtocolVersion);
        }

        const auto playerIt = players_.find(request.playerId);
        if (playerIt == players_.end()) {
            return reject(request, CommandRejectionReason::UnknownPlayer);
        }

        PlayerCommandState& playerState = playerIt->second;
        if (request.sequence == 0 || request.sequence <= playerState.lastProcessedSequence) {
            return reject(request, CommandRejectionReason::DuplicateOrOutOfOrderSequence);
        }

        // Consume a well-formed request before domain validation so a rejected request cannot be replayed.
        playerState.lastProcessedSequence = request.sequence;

        if (validateDomainCommand) {
            if (const auto rejection = validateDomainCommand(request)) {
                return reject(request, *rejection);
            }
        }

        return CommandAccepted{request.playerId, request.sequence, currentTick};
    }

  private:
    struct PlayerCommandState {
        CommandSequence lastProcessedSequence = 0;
    };

    static CommandRejected reject(const PlayerCommandRequest& request, CommandRejectionReason reason) {
        return CommandRejected{request.playerId, request.sequence, reason};
    }

    std::unordered_map<PlayerId, PlayerCommandState> players_;
};

} // namespace multiplayer