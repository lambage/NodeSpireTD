#include "multiplayer/MultiplayerSession.hpp"

#include "multiplayer/PartyProtocolAdapter.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

namespace multiplayer {

bool MultiplayerSession::hostParty(unsigned short port, std::string displayName, std::string playerUuid) {
    leaveParty();
    if (!hostTransport_.listen(port)) {
        return false;
    }
    localDisplayName_ = std::move(displayName);
    localPlayerUuid_ = std::move(playerUuid);
    role_ = MultiplayerRole::Host;
    resetToSolo();
    broadcastSystemMessage("Party host service started.");
    return true;
}

bool MultiplayerSession::joinParty(const std::string& address, unsigned short port, std::string displayName,
                                    std::string playerUuid) {
    leaveParty();
    if (!client_.connect(address, port)) {
        return false;
    }

    localDisplayName_ = std::move(displayName);
    localPlayerUuid_ = std::move(playerUuid);
    PartyJoinRequest request;
    request.displayName = localDisplayName_;
    request.playerUuid = localPlayerUuid_;
    const auto serialized = PartyProtocolAdapter::serializePartyJoinRequest(request);
    if (!serialized || !client_.sendPartyJoinRequest(*serialized)) {
        client_.disconnect();
        return false;
    }
    role_ = MultiplayerRole::Client;
    clientJoinPending_ = true;
    return true;
}

void MultiplayerSession::leaveParty() {
    hostTransport_.reset();
    client_.disconnect();
    peerToPlayerId_.clear();
    clientJoinPending_ = false;
    pendingMatchStartAnnouncement_.reset();
    role_ = MultiplayerRole::Solo;
    resetToSolo();
}

void MultiplayerSession::resetToSolo() {
    partyGate_ = LocalHostPartyGate();
    remoteRoster_ = PartyRosterSnapshot{};
    localPlayerId_ = partyGate_.addMember(localDisplayName_, /*isHost=*/true, localPlayerUuid_);
    pendingChatMessages_.clear();
    pendingChatErrors_.clear();
}

PartyRosterSnapshot MultiplayerSession::roster() const {
    return isClient() ? remoteRoster_ : partyGate_.snapshot();
}

bool MultiplayerSession::setLocalReady(bool ready) {
    if (isClient()) {
        const auto serialized = PartyProtocolAdapter::serializePartySetReadyRequest(PartySetReadyRequest{ready});
        return serialized && client_.sendPartySetReadyRequest(*serialized);
    }
    const bool ok = partyGate_.setReady(localPlayerId_, ready);
    if (ok) {
        broadcastRoster();
    }
    return ok;
}

bool MultiplayerSession::kickMember(PlayerId targetPlayerId) {
    // Only the host's gate is authoritative; a client cannot kick regardless of what its own
    // (host-pushed) roster view claims about itself.
    if (!isHost() || !partyGate_.canKick(localPlayerId_, targetPlayerId)) {
        return false;
    }
    const std::string targetName = partyGate_.displayNameForPlayer(targetPlayerId);
    partyGate_.removeMember(targetPlayerId);
    for (auto it = peerToPlayerId_.begin(); it != peerToPlayerId_.end(); ++it) {
        if (it->second == targetPlayerId) {
            notifyPeerKicked(it->first);
            hostTransport_.disconnectPeer(it->first);
            peerToPlayerId_.erase(it);
            break;
        }
    }
    broadcastSystemMessage(targetName + " was kicked from the party.");
    broadcastRoster();
    return true;
}

bool MultiplayerSession::announceMatchStart(std::string levelName, std::string levelScriptPath,
                                            std::string levelAssetPath) {
    if (!isHost()) {
        return false;
    }
    partyGate_.resetLoadedFlags();
    PartyMatchStartAnnouncement announcement{std::move(levelName), std::move(levelScriptPath),
                                             std::move(levelAssetPath)};
    if (const auto serialized = PartyProtocolAdapter::serializePartyMatchStartAnnouncement(announcement)) {
        hostTransport_.broadcastPartyMatchStart(*serialized);
    }
    broadcastRoster();
    return true;
}

std::optional<PartyMatchStartAnnouncement> MultiplayerSession::consumeMatchStartAnnouncement() {
    if (!pendingMatchStartAnnouncement_) {
        return std::nullopt;
    }
    return std::exchange(pendingMatchStartAnnouncement_, std::nullopt);
}

void MultiplayerSession::signalLocalLoadedReady() {
    if (isClient()) {
        const auto serialized = PartyProtocolAdapter::serializePartyMatchLoadedReady(PartyMatchLoadedReady{});
        if (serialized) {
            client_.sendPartyMatchLoadedReady(*serialized);
        }
        return;
    }
    if (partyGate_.setLoaded(localPlayerId_, true)) {
        broadcastRoster();
    }
}

bool MultiplayerSession::allMembersLoadedReady() const {
    if (isClient()) {
        return !remoteRoster_.members.empty() &&
               std::all_of(remoteRoster_.members.begin(), remoteRoster_.members.end(),
                           [](const PartyMemberState& member) { return member.loaded; });
    }
    return partyGate_.allLoaded();
}

std::optional<PlayerId> MultiplayerSession::playerIdForPeer(TransportPeerId peerId) const {
    const auto it = peerToPlayerId_.find(peerId);
    return it == peerToPlayerId_.end() ? std::nullopt : std::optional<PlayerId>(it->second);
}

bool MultiplayerSession::sendChatMessage(const std::string& rawText) {
    if (isClient()) {
        const auto serialized = PartyProtocolAdapter::serializePartyChatSendRequest(PartyChatSendRequest{rawText});
        return serialized && client_.sendPartyChatSendRequest(*serialized);
    }

    // Host and solo both dispatch locally through the same command table so `/roar` behaves
    // identically regardless of role.
    const auto result = chatDispatcher_.process(rawText, localDisplayName_);
    if (result.broadcast) {
        PartyChatMessage chatMessage{localPlayerId_, localDisplayName_, result.broadcast->text,
                                     result.broadcast->isEmote};
        pendingChatMessages_.push_back(chatMessage);
        if (isHost()) {
            if (const auto serialized = PartyProtocolAdapter::serializePartyChatMessage(chatMessage)) {
                hostTransport_.broadcastPartyChatMessage(*serialized);
            }
        }
        return true;
    }
    if (result.localError) {
        pendingChatErrors_.push_back(*result.localError);
    }
    return false;
}

std::vector<PartyChatMessage> MultiplayerSession::consumeChatMessages() {
    return std::exchange(pendingChatMessages_, {});
}

std::vector<std::string> MultiplayerSession::consumeChatErrors() {
    return std::exchange(pendingChatErrors_, {});
}

void MultiplayerSession::broadcastRoster() {
    if (role_ != MultiplayerRole::Host) {
        return;
    }
    if (const auto serialized = PartyProtocolAdapter::serializePartyRosterSnapshot(partyGate_.snapshot())) {
        hostTransport_.broadcastPartyRosterSnapshot(*serialized);
    }
}

void MultiplayerSession::broadcastSystemMessage(std::string text) {
    PartyChatMessage message{0, "System", std::move(text), false};
    pendingChatMessages_.push_back(message);
    if (isHost()) {
        if (const auto serialized = PartyProtocolAdapter::serializePartyChatMessage(message)) {
            hostTransport_.broadcastPartyChatMessage(*serialized);
        }
    }
}

void MultiplayerSession::sendSystemMessageToPeer(TransportPeerId peerId, std::string text) {
    const PartyChatMessage message{0, "System", std::move(text), false};
    if (const auto serialized = PartyProtocolAdapter::serializePartyChatMessage(message)) {
        hostTransport_.sendPartyChatMessage(peerId, *serialized);
    }
}

void MultiplayerSession::notifyPeerKicked(TransportPeerId peerId) {
    if (const auto serialized = PartyProtocolAdapter::serializePartyChatCommandError(
            PartyChatCommandError{"You were kicked from the party."})) {
        hostTransport_.sendPartyChatCommandError(peerId, *serialized);
    }
}

void MultiplayerSession::update() {
    ioContext_.poll();
    if (role_ == MultiplayerRole::Host) {
        pumpHostSide();
    } else if (role_ == MultiplayerRole::Client) {
        pumpClientSide();
    }
}

void MultiplayerSession::pumpHostSide() {
    bool rosterChanged = false;

    for (const auto peerId : hostTransport_.drainDisconnectedPeers()) {
        const auto peerIt = peerToPlayerId_.find(peerId);
        if (peerIt == peerToPlayerId_.end()) {
            continue;
        }
        const std::string name = partyGate_.displayNameForPlayer(peerIt->second);
        spdlog::info("MultiplayerSession[host]: peer {} (player {}) disconnected; freeing their party slot.", peerId,
                     peerIt->second);
        partyGate_.removeMember(peerIt->second);
        peerToPlayerId_.erase(peerIt);
        broadcastSystemMessage(name + " disconnected.");
        rosterChanged = true;
    }

    for (auto& request : hostTransport_.drainPartyJoinRequests()) {
        const auto decoded = PartyProtocolAdapter::decodePartyJoinRequest(request.payload);
        PartyJoinResult result = PartyJoinRejected{PartyJoinRejectionReason::Unspecified};
        std::string joinedDisplayName;
        if (decoded.request) {
            result = partyGate_.evaluateJoin(*decoded.request);
        }

        if (auto* accepted = std::get_if<PartyJoinAccepted>(&result)) {
            const PlayerId playerId =
                partyGate_.addMember(decoded.request->displayName, /*isHost=*/false, decoded.request->playerUuid);
            accepted->playerId = playerId;
            accepted->roster = partyGate_.snapshot();
            peerToPlayerId_[request.peerId] = playerId;
            hostTransport_.markPeerPartyJoined(request.peerId);
            spdlog::info("MultiplayerSession[host]: peer {} joined party as player {} ('{}').", request.peerId,
                         playerId, decoded.request->displayName);
            joinedDisplayName = decoded.request->displayName;
        }

        if (const auto serialized = PartyProtocolAdapter::serializePartyJoinResult(result)) {
            hostTransport_.sendPartyJoinResult(request.peerId, *serialized);
        }
        if (!joinedDisplayName.empty()) {
            broadcastSystemMessage(joinedDisplayName + " connected.");
            sendSystemMessageToPeer(request.peerId, "Welcome to the party, " + joinedDisplayName + ".");
        } else if (std::holds_alternative<PartyJoinRejected>(result)) {
            hostTransport_.disconnectPeer(request.peerId);
        }
    }

    for (auto& message : hostTransport_.drainPartySetReadyRequests()) {
        const auto playerIt = peerToPlayerId_.find(message.peerId);
        if (playerIt == peerToPlayerId_.end()) {
            continue;
        }
        if (const auto request = PartyProtocolAdapter::decodePartySetReadyRequest(message.payload)) {
            rosterChanged |= partyGate_.setReady(playerIt->second, request->ready);
        }
    }

    for (auto& message : hostTransport_.drainPartyKickRequests()) {
        const auto requesterIt = peerToPlayerId_.find(message.peerId);
        if (requesterIt == peerToPlayerId_.end()) {
            continue;
        }
        const auto request = PartyProtocolAdapter::decodePartyKickRequest(message.payload);
        if (request && partyGate_.canKick(requesterIt->second, request->targetPlayerId)) {
            const std::string targetName = partyGate_.displayNameForPlayer(request->targetPlayerId);
            partyGate_.removeMember(request->targetPlayerId);
            for (auto it = peerToPlayerId_.begin(); it != peerToPlayerId_.end(); ++it) {
                if (it->second == request->targetPlayerId) {
                    notifyPeerKicked(it->first);
                    hostTransport_.disconnectPeer(it->first);
                    peerToPlayerId_.erase(it);
                    break;
                }
            }
            broadcastSystemMessage(targetName + " was kicked from the party.");
            rosterChanged = true;
        }
    }

    for (auto& message : hostTransport_.drainPartyMatchLoadedReady()) {
        const auto playerIt = peerToPlayerId_.find(message.peerId);
        if (playerIt == peerToPlayerId_.end()) {
            continue;
        }
        if (PartyProtocolAdapter::decodePartyMatchLoadedReady(message.payload)) {
            rosterChanged |= partyGate_.setLoaded(playerIt->second, true);
        }
    }

    for (auto& message : hostTransport_.drainPartyChatSendRequests()) {
        const auto senderIt = peerToPlayerId_.find(message.peerId);
        if (senderIt == peerToPlayerId_.end()) {
            continue;
        }
        const auto request = PartyProtocolAdapter::decodePartyChatSendRequest(message.payload);
        if (!request) {
            continue;
        }
        const std::string speakerName = partyGate_.displayNameForPlayer(senderIt->second);
        const auto result = chatDispatcher_.process(request->text, speakerName);
        if (result.broadcast) {
            PartyChatMessage chatMessage{senderIt->second, speakerName, result.broadcast->text,
                                         result.broadcast->isEmote};
            pendingChatMessages_.push_back(chatMessage);
            if (const auto serialized = PartyProtocolAdapter::serializePartyChatMessage(chatMessage)) {
                hostTransport_.broadcastPartyChatMessage(*serialized);
            }
        } else if (result.localError) {
            if (const auto serialized =
                    PartyProtocolAdapter::serializePartyChatCommandError(PartyChatCommandError{*result.localError})) {
                hostTransport_.sendPartyChatCommandError(message.peerId, *serialized);
            }
        }
    }

    if (rosterChanged) {
        broadcastRoster();
    }
}

void MultiplayerSession::pumpClientSide() {
    if (!clientJoinPending_ && !client_.isConnected()) {
        // Connection dropped after a successful join; fall back to solo so Lobby.lua can offer a
        // rejoin via Find Party (the host, if still up, will recognize the same profile UUID).
        spdlog::warn("MultiplayerSession[client]: connection to host lost.");
        role_ = MultiplayerRole::Solo;
        resetToSolo();
        // Pushed after resetToSolo() clears the queues, so it survives to be shown once chat is
        // next visible (e.g. after the player hosts/joins again).
        pendingChatMessages_.push_back(PartyChatMessage{0, "System", "Lost connection to the host.", false});
        return;
    }

    if (clientJoinPending_) {
        if (const auto payload = client_.consumePartyJoinResult()) {
            clientJoinPending_ = false;
            if (const auto result = PartyProtocolAdapter::decodePartyJoinResult(*payload)) {
                if (const auto* accepted = std::get_if<PartyJoinAccepted>(&*result)) {
                    localPlayerId_ = accepted->playerId;
                    remoteRoster_ = accepted->roster;
                } else {
                    spdlog::warn("MultiplayerSession[client]: party join rejected by host.");
                    client_.disconnect();
                    role_ = MultiplayerRole::Solo;
                    resetToSolo();
                    return;
                }
            }
        } else if (!client_.isConnected()) {
            clientJoinPending_ = false;
            spdlog::warn("MultiplayerSession[client]: connection lost while awaiting party join result.");
            role_ = MultiplayerRole::Solo;
            resetToSolo();
            return;
        }
    }

    if (const auto payload = client_.consumeLatestPartyRosterSnapshot()) {
        if (const auto decoded = PartyProtocolAdapter::decodePartyRosterSnapshot(*payload)) {
            remoteRoster_ = *decoded;
        }
    }

    if (const auto payload = client_.consumePartyMatchStart()) {
        if (const auto announcement = PartyProtocolAdapter::decodePartyMatchStartAnnouncement(*payload)) {
            pendingMatchStartAnnouncement_ = announcement;
        }
    }

    for (auto& payload : client_.drainPartyChatMessages()) {
        if (const auto message = PartyProtocolAdapter::decodePartyChatMessage(payload)) {
            pendingChatMessages_.push_back(*message);
        }
    }
    for (auto& payload : client_.drainPartyChatCommandErrors()) {
        if (const auto error = PartyProtocolAdapter::decodePartyChatCommandError(payload)) {
            pendingChatErrors_.push_back(error->message);
        }
    }
}

} // namespace multiplayer
