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

// Host-side LAN transport: accepts TCP connections from remote game clients and exchanges
// length-prefixed, kind-tagged frames (see LanFramedConnection). The owner must poll the
// shared io_context once per tick to progress accepts, reads, and writes; this class never
// spawns threads of its own.
class LanMatchTransport final : public IMatchTransport {
  public:
    explicit LanMatchTransport(boost::asio::io_context& ioContext);

    bool listen(unsigned short port);
    void stop();
    unsigned short listenPort() const;

    // Peers accepted since the last drain. Each one is unjoined: drainClientCommands() and
    // publishSnapshot() ignore it, and sendCommandResult() fails for it, until markPeerJoined()
    // is called for its peer ID.
    std::vector<TransportPeerId> drainAcceptedPeers();
    std::vector<PendingJoinRequest> drainJoinRequests();
    bool markPeerJoined(TransportPeerId peerId);
    bool sendJoinResult(TransportPeerId peerId, std::string payload);
    bool disconnectPeer(TransportPeerId peerId);

    std::vector<ReceivedClientCommand> drainClientCommands() override;
    bool sendCommandResult(TransportPeerId peerId, std::string payload) override;
    void publishSnapshot(std::string payload) override;

  private:
    struct PeerConnection {
        std::shared_ptr<LanFramedConnection> connection;
        bool joined = false;
    };

    void beginAccept();
    void handleFrame(TransportPeerId peerId, std::uint8_t kind, std::string payload);

    boost::asio::io_context& ioContext_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::unordered_map<TransportPeerId, PeerConnection> connections_;
    std::vector<TransportPeerId> acceptedPeers_;
    std::deque<PendingJoinRequest> pendingJoinRequests_;
    std::deque<ReceivedClientCommand> pendingCommands_;
    TransportPeerId nextPeerId_ = 1;
};

} // namespace multiplayer
