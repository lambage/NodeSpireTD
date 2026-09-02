#pragma once

#include "multiplayer/IMatchTransport.hpp"
#include "multiplayer/LanFramedConnection.hpp"

#include <boost/asio.hpp>

#include <deque>
#include <unordered_map>
#include <vector>

namespace multiplayer {

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

    // Peers accepted since the last drain. The caller is responsible for mapping each one to
    // a player identity (e.g. via a join-request handshake) before trusting its commands.
    std::vector<TransportPeerId> drainAcceptedPeers();
    bool disconnectPeer(TransportPeerId peerId);

    std::vector<ReceivedClientCommand> drainClientCommands() override;
    bool sendCommandResult(TransportPeerId peerId, std::string payload) override;
    void publishSnapshot(std::string payload) override;

  private:
    void beginAccept();

    boost::asio::io_context& ioContext_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::unordered_map<TransportPeerId, std::shared_ptr<LanFramedConnection>> connections_;
    std::vector<TransportPeerId> acceptedPeers_;
    std::deque<ReceivedClientCommand> pendingCommands_;
    TransportPeerId nextPeerId_ = 1;
};

} // namespace multiplayer
