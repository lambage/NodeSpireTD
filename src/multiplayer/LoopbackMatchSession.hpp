#pragma once

#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/LoopbackTransport.hpp"
#include "multiplayer/MatchSimulation.hpp"

#include <functional>
#include <optional>
#include <unordered_map>

namespace multiplayer {

class LoopbackMatchSession {
  public:
    using AuthoritativeCommandHandler = LocalMatchHost::AuthoritativeCommandHandler;

    std::optional<TransportPeerId> connectPlayer(PlayerId playerId, float initialBalance);
    bool disconnectPlayer(TransportPeerId peerId);
    void advance(float elapsedSeconds, const AuthoritativeCommandHandler& handleCommand);

    MatchSimulation& simulation();
    LoopbackTransport& transport();

  private:
    static constexpr SimulationTick kSnapshotIntervalTicks = 3;

    MatchSimulation simulation_;
    LocalMatchHost host_;
    LoopbackTransport transport_;
    std::unordered_map<TransportPeerId, PlayerId> playerByPeer_;
};

} // namespace multiplayer