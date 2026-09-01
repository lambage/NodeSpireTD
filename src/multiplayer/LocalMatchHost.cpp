#include "multiplayer/LocalMatchHost.hpp"

namespace multiplayer {

bool LocalMatchHost::registerPlayer(PlayerId playerId) {
    return commandGate_.registerPlayer(playerId);
}

bool LocalMatchHost::unregisterPlayer(PlayerId playerId) {
    return commandGate_.unregisterPlayer(playerId);
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

} // namespace multiplayer