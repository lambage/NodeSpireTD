#pragma once

#include "multiplayer/IMatchTransport.hpp"
#include "multiplayer/LanFramedConnection.hpp"

#include <boost/asio.hpp>

#include <deque>
#include <unordered_map>
#include <vector>

namespace multiplayer {

// A join request received from a peer that has not yet been validated. The caller decodes
// the payload, checks protocol version and content manifest, then calls markPeerJoined() and
// sendJoinResult() (accept) or sendJoinResult() and disconnectPeer() (reject).
struct PendingJoinRequest {
    TransportPeerId peerId = 0;
    std::string payload;
};

// Generic {peer, payload} pair for party-phase frames (join/ready/kick/loaded-ready intents).
struct PartyPeerFrame {
    TransportPeerId peerId = 0;
    std::string payload;
};

// Host-side LAN transport: accepts TCP connections from remote game clients and exchanges
// length-prefixed, kind-tagged frames (see LanFramedConnection). The owner must poll the
// shared io_context once per tick to progress accepts, reads, and writes; this class never
// spawns threads of its own.
class LanMatchTransport final : public IMatchTransport {
  public:
    explicit LanMatchTransport(boost::asio::io_context& ioContext);

    bool listen(unsigned short port);
    void stop();
    // Like stop(), but also discards queued accepted-peer/join-request/command state so a scene
    // can safely reuse the same transport instance across separate matches.
    void reset();
    unsigned short listenPort() const;

    // Peers accepted since the last drain. Each one is unjoined: drainClientCommands() and
    // publishSnapshot() ignore it, and sendCommandResult() fails for it, until markPeerJoined()
    // is called for its peer ID.
    std::vector<TransportPeerId> drainAcceptedPeers();
    std::vector<PendingJoinRequest> drainJoinRequests();
    bool markPeerJoined(TransportPeerId peerId);
    bool sendJoinResult(TransportPeerId peerId, std::string payload);
    bool disconnectPeer(TransportPeerId peerId);

    // Peers whose connection dropped unexpectedly (not via an explicit disconnectPeer() call)
    // since the last drain. The owner (MultiplayerSession) uses this to remove the peer's party
    // membership so a stale roster slot doesn't linger forever, and to make room for that same
    // player (by profile UUID) to reconnect. disconnectPeer() itself does not enqueue here --
    // callers that initiate a disconnect (kick, join-rejection) already know why and handle their
    // own cleanup.
    std::vector<TransportPeerId> drainDisconnectedPeers();

    // Party-phase traffic. A peer must complete the party join handshake (markPeerPartyJoined)
    // before its ready/kick intents are trusted; this is independent of markPeerJoined()/joined,
    // which gates match-level (post-party) command traffic instead.
    std::vector<PartyPeerFrame> drainPartyJoinRequests();
    std::vector<PartyPeerFrame> drainPartySetReadyRequests();
    std::vector<PartyPeerFrame> drainPartyKickRequests();
    std::vector<PartyPeerFrame> drainPartyMatchLoadedReady();
    bool markPeerPartyJoined(TransportPeerId peerId);
    bool sendPartyJoinResult(TransportPeerId peerId, std::string payload);
    void broadcastPartyRosterSnapshot(std::string payload);
    void broadcastPartyMatchStart(std::string payload);

    // Chat/emote traffic. Like ready/kick, a peer must be party-joined before its chat intents
    // are trusted (enforced in handleFrame(), not by the caller).
    std::vector<PartyPeerFrame> drainPartyChatSendRequests();
    void broadcastPartyChatMessage(std::string payload);
    bool sendPartyChatCommandError(TransportPeerId peerId, std::string payload);

    std::vector<ReceivedClientCommand> drainClientCommands() override;
    bool sendCommandResult(TransportPeerId peerId, std::string payload) override;
    void publishSnapshot(std::string payload) override;

  private:
    struct PeerConnection {
        std::shared_ptr<LanFramedConnection> connection;
        bool joined = false;
        bool partyJoined = false;
    };

    void beginAccept();
    void handleFrame(TransportPeerId peerId, std::uint8_t kind, std::string payload);

    boost::asio::io_context& ioContext_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::unordered_map<TransportPeerId, PeerConnection> connections_;
    std::vector<TransportPeerId> acceptedPeers_;
    std::deque<PendingJoinRequest> pendingJoinRequests_;
    std::deque<ReceivedClientCommand> pendingCommands_;
    std::deque<PartyPeerFrame> pendingPartyJoinRequests_;
    std::deque<PartyPeerFrame> pendingPartySetReady_;
    std::deque<PartyPeerFrame> pendingPartyKick_;
    std::deque<PartyPeerFrame> pendingPartyMatchLoadedReady_;
    std::deque<PartyPeerFrame> pendingPartyChatSend_;
    std::vector<TransportPeerId> disconnectedPeers_;
    TransportPeerId nextPeerId_ = 1;
};

} // namespace multiplayer
