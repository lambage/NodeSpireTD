#pragma once

#include "multiplayer/MatchProtocol.hpp"

#include <cstddef>
#include <string_view>

namespace multiplayer {

// Pure validation logic for the join handshake: no I/O, no protobuf, no simulation access.
// LocalMatchHost owns decode/encode and wires this into the transport; LocalMatchHost::
// registerPlayer()/unregisterPlayer() keep noteRegisteredPlayer()/noteUnregisteredPlayer() in
// sync so capacity and id allocation account for every player, not just ones that joined
// remotely (e.g. the local host player).
class LocalHostJoinGate {
  public:
    static constexpr std::size_t kDefaultMaxPlayers = 8;

    explicit LocalHostJoinGate(std::size_t maxPlayers = kDefaultMaxPlayers) : maxPlayers_(maxPlayers) {}

    void noteRegisteredPlayer(PlayerId playerId) {
        ++registeredPlayerCount_;
        if (playerId >= nextPlayerId_) {
            nextPlayerId_ = playerId + 1;
        }
    }

    void noteUnregisteredPlayer() {
        if (registeredPlayerCount_ > 0) {
            --registeredPlayerCount_;
        }
    }

    // Does not mutate any state -- the caller must still call LocalMatchHost::registerPlayer()
    // with the accepted id so a later evaluate() sees the updated capacity/id allocation.
    JoinMatchResult evaluate(const JoinMatchRequest& request, std::string_view expectedContentManifestSha256,
                             SimulationTick currentTick) const {
        if (request.protocolVersion != kMatchProtocolVersion) {
            return JoinMatchRejected{JoinRejectionReason::ProtocolVersionUnsupported};
        }
        if (request.contentManifest.gameplayContentSha256 != expectedContentManifestSha256) {
            return JoinMatchRejected{JoinRejectionReason::ContentManifestMismatch};
        }
        if (registeredPlayerCount_ >= maxPlayers_) {
            return JoinMatchRejected{JoinRejectionReason::MatchFull};
        }
        return JoinMatchAccepted{nextPlayerId_, currentTick};
    }

  private:
    std::size_t maxPlayers_;
    std::size_t registeredPlayerCount_ = 0;
    PlayerId nextPlayerId_ = 1;
};

} // namespace multiplayer
