#include "multiplayer/LoopbackMatchSession.hpp"

#include "multiplayer/MatchSnapshotBuilder.hpp"

namespace multiplayer {

std::optional<TransportPeerId> LoopbackMatchSession::connectPlayer(PlayerId playerId, float initialBalance) {
    if (!simulation_.registerPlayer(playerId, initialBalance) || !host_.registerPlayer(playerId)) {
        return std::nullopt;
    }
    const TransportPeerId peerId = transport_.connectClient();
    playerByPeer_.emplace(peerId, playerId);
    if (const auto snapshot = MatchSnapshotBuilder::serialize(simulation_)) {
        transport_.publishSnapshot(*snapshot);
    }
    return peerId;
}

bool LoopbackMatchSession::disconnectPlayer(TransportPeerId peerId) {
    const auto playerIt = playerByPeer_.find(peerId);
    if (playerIt == playerByPeer_.end()) {
        return false;
    }
    const PlayerId playerId = playerIt->second;
    playerByPeer_.erase(playerIt);
    host_.unregisterPlayer(playerId);
    simulation_.unregisterPlayer(playerId);
    return transport_.disconnectClient(peerId);
}

void LoopbackMatchSession::advance(float elapsedSeconds, const AuthoritativeCommandHandler& handleCommand) {
    simulation_.advance(elapsedSeconds, [this, &handleCommand](SimulationTick tick, float) {
        for (ReceivedClientCommand& received : transport_.drainClientCommands()) {
            const auto playerIt = playerByPeer_.find(received.peerId);
            if (playerIt == playerByPeer_.end()) {
                continue;
            }
            const auto result = host_.processCommand(
                received.payload, tick, [&handleCommand, expectedPlayerId = playerIt->second](const PlayerCommandRequest& command) {
                    if (command.playerId != expectedPlayerId) {
                        return std::optional{CommandRejectionReason::UnknownPlayer};
                    }
                    return handleCommand ? handleCommand(command)
                                         : std::optional<CommandRejectionReason>{};
                });
            if (result) {
                transport_.sendCommandResult(received.peerId, *result);
            }
        }

        if (tick % kSnapshotIntervalTicks == 0) {
            if (const auto snapshot = MatchSnapshotBuilder::serialize(simulation_)) {
                transport_.publishSnapshot(*snapshot);
            }
        }
    });
}

MatchSimulation& LoopbackMatchSession::simulation() {
    return simulation_;
}

LoopbackTransport& LoopbackMatchSession::transport() {
    return transport_;
}

} // namespace multiplayer