#include "multiplayer/LocalMatchHost.hpp"

#include <variant>

namespace multiplayer {

bool LocalMatchHost::registerPlayer(PlayerId playerId) {
    if (!commandGate_.registerPlayer(playerId)) {
        return false;
    }
    joinGate_.noteRegisteredPlayer(playerId);
    return true;
}

bool LocalMatchHost::unregisterPlayer(PlayerId playerId) {
    if (!commandGate_.unregisterPlayer(playerId)) {
        return false;
    }
    joinGate_.noteUnregisteredPlayer();
    return true;
}

std::optional<std::string> LocalMatchHost::processCommand(std::string_view payload,
                                                           SimulationTick currentTick,
                                                           const AuthoritativeCommandHandler& handleCommand) {
    const DecodedPlayerCommand decoded = MatchProtocolAdapter::decodePlayerCommand(payload);
    if (!decoded.command) {
        return MatchProtocolAdapter::serializePlayerCommandResult(
            CommandRejected{0, 0, CommandRejectionReason::InvalidPayload});
    }

    const PlayerCommandResult result = commandGate_.validateAndApply(*decoded.command, currentTick, handleCommand);
    return MatchProtocolAdapter::serializePlayerCommandResult(result);
}

std::optional<LocalMatchHost::JoinRequestOutcome> LocalMatchHost::processJoinRequest(
    std::string_view payload, std::string_view expectedContentManifestSha256, SimulationTick currentTick) {
    const DecodedJoinMatchRequest decoded = MatchProtocolAdapter::decodeJoinMatchRequest(payload);
    if (!decoded.request) {
        const auto serialized =
            MatchProtocolAdapter::serializeJoinMatchResult(JoinMatchRejected{JoinRejectionReason::Unspecified});
        if (!serialized) {
            return std::nullopt;
        }
        return JoinRequestOutcome{*serialized, std::nullopt};
    }

    const JoinMatchResult result = joinGate_.evaluate(*decoded.request, expectedContentManifestSha256, currentTick);
    const auto serialized = MatchProtocolAdapter::serializeJoinMatchResult(result);
    if (!serialized) {
        return std::nullopt;
    }

    std::optional<PlayerId> acceptedPlayerId;
    if (const auto* accepted = std::get_if<JoinMatchAccepted>(&result)) {
        acceptedPlayerId = accepted->playerId;
    }
    return JoinRequestOutcome{*serialized, acceptedPlayerId};
}

} // namespace multiplayer