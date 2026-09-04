#pragma once

#include "multiplayer/PartyProtocol.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace multiplayer {

// Host-authoritative party roster: pure state + validation, no I/O. Whoever owns the transport
// (LobbyScene today, a dedicated party host object once transport wiring lands) decodes wire
// messages, calls into this gate, then serializes/broadcasts the resulting PartyRosterSnapshot.
// Mirrors LocalHostJoinGate's split of evaluate() (read-only check) vs. explicit mutation calls.
class LocalHostPartyGate {
  public:
    explicit LocalHostPartyGate(std::size_t capacity = kPartyCapacity) : capacity_(capacity) {}

    // Does not mutate state -- the caller must still call addMember() with the same request on
    // acceptance so a later evaluateJoin() sees the updated membership count.
    PartyJoinResult evaluateJoin(const PartyJoinRequest& request) const {
        if (request.protocolVersion != kPartyProtocolVersion) {
            return PartyJoinRejected{PartyJoinRejectionReason::ProtocolVersionUnsupported};
        }
        if (request.displayName.empty() || request.displayName.size() > kMaxPartyDisplayNameLength) {
            return PartyJoinRejected{PartyJoinRejectionReason::DisplayNameInvalid};
        }
        if (!request.playerUuid.empty() && isActiveUuid(request.playerUuid)) {
            return PartyJoinRejected{PartyJoinRejectionReason::AlreadyConnected};
        }
        if (members_.size() >= capacity_) {
            return PartyJoinRejected{PartyJoinRejectionReason::PartyFull};
        }
        return PartyJoinAccepted{nextPlayerId_, snapshot()};
    }

    // Caller is responsible for calling this only after evaluateJoin() returned an acceptance
    // for the same request (capacity/duplicate-uuid are not re-checked here). If playerUuid was
    // seen before (a returning player reconnecting after a drop), the same PlayerId is reused
    // instead of minting a new one, so the identity carries across the reconnect.
    PlayerId addMember(std::string_view displayName, bool isHost, std::string_view playerUuid = {}) {
        PlayerId playerId;
        if (!playerUuid.empty()) {
            const auto knownIt = uuidToPlayerId_.find(std::string(playerUuid));
            if (knownIt != uuidToPlayerId_.end()) {
                playerId = knownIt->second;
            } else {
                playerId = nextPlayerId_++;
                uuidToPlayerId_.emplace(std::string(playerUuid), playerId);
            }
        } else {
            playerId = nextPlayerId_++;
        }
        members_.push_back(PartyMemberState{playerId, std::string(displayName), isHost, false});
        return playerId;
    }

    bool removeMember(PlayerId playerId) {
        const auto it = std::find_if(members_.begin(), members_.end(),
                                      [playerId](const PartyMemberState& m) { return m.playerId == playerId; });
        if (it == members_.end()) {
            return false;
        }
        members_.erase(it);
        return true;
    }

    bool setReady(PlayerId playerId, bool ready) {
        const auto it = std::find_if(members_.begin(), members_.end(),
                                      [playerId](const PartyMemberState& m) { return m.playerId == playerId; });
        if (it == members_.end()) {
            return false;
        }
        it->ready = ready;
        return true;
    }

    bool setLoaded(PlayerId playerId, bool loaded) {
        const auto it = std::find_if(members_.begin(), members_.end(),
                                      [playerId](const PartyMemberState& m) { return m.playerId == playerId; });
        if (it == members_.end()) {
            return false;
        }
        it->loaded = loaded;
        return true;
    }

    // Called before announcing a new match start so stale loaded flags from a previous match
    // don't make the barrier appear already satisfied.
    void resetLoadedFlags() {
        for (auto& member : members_) {
            member.loaded = false;
        }
    }

    bool allLoaded() const {
        return !members_.empty() &&
               std::all_of(members_.begin(), members_.end(), [](const PartyMemberState& m) { return m.loaded; });
    }

    // Chat/emote and gameplay commands never route through here -- this gate only ever answers
    // membership/ready/kick questions, keeping the social channel decoupled from match authority.
    bool canKick(PlayerId requestingPlayerId, PlayerId targetPlayerId) const {
        if (requestingPlayerId == targetPlayerId) {
            return false;
        }
        const auto it = std::find_if(members_.begin(), members_.end(), [requestingPlayerId](const PartyMemberState& m) {
            return m.playerId == requestingPlayerId;
        });
        return it != members_.end() && it->isHost;
    }

    PartyRosterSnapshot snapshot() const {
        return PartyRosterSnapshot{members_, static_cast<std::uint32_t>(capacity_)};
    }

    std::size_t memberCount() const { return members_.size(); }
    std::size_t capacity() const { return capacity_; }

    std::string displayNameForPlayer(PlayerId playerId) const {
        const auto it = std::find_if(members_.begin(), members_.end(),
                                      [playerId](const PartyMemberState& m) { return m.playerId == playerId; });
        return it != members_.end() ? it->displayName : std::string();
    }

  private:
    bool isActiveUuid(const std::string& playerUuid) const {
        const auto knownIt = uuidToPlayerId_.find(playerUuid);
        if (knownIt == uuidToPlayerId_.end()) {
            return false;
        }
        return std::any_of(members_.begin(), members_.end(),
                            [playerId = knownIt->second](const PartyMemberState& m) { return m.playerId == playerId; });
    }

    std::size_t capacity_;
    std::vector<PartyMemberState> members_;
    // Deliberately never erased on removeMember(): keeping the uuid -> PlayerId mapping alive for
    // the gate's lifetime lets a player who drops and reconnects (same PlayerProfile UUID) come
    // back as the same PlayerId rather than a brand-new one.
    std::unordered_map<std::string, PlayerId> uuidToPlayerId_;
    PlayerId nextPlayerId_ = 1;
};

} // namespace multiplayer
