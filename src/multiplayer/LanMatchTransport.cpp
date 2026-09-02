#include "multiplayer/LanMatchTransport.hpp"

namespace multiplayer {

LanMatchTransport::LanMatchTransport(boost::asio::io_context& ioContext)
    : ioContext_(ioContext), acceptor_(ioContext) {}

bool LanMatchTransport::listen(unsigned short port) {
    boost::system::error_code errorCode;
    const boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);

    acceptor_.open(endpoint.protocol(), errorCode);
    if (errorCode) {
        return false;
    }
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), errorCode);
    acceptor_.bind(endpoint, errorCode);
    if (errorCode) {
        return false;
    }
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, errorCode);
    if (errorCode) {
        return false;
    }

    beginAccept();
    return true;
}

void LanMatchTransport::stop() {
    boost::system::error_code errorCode;
    acceptor_.close(errorCode);
    for (auto& [peerId, connection] : connections_) {
        (void)peerId;
        connection->close();
    }
    connections_.clear();
}

unsigned short LanMatchTransport::listenPort() const {
    boost::system::error_code errorCode;
    const auto endpoint = acceptor_.local_endpoint(errorCode);
    return errorCode ? 0 : endpoint.port();
}

void LanMatchTransport::beginAccept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioContext_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& errorCode) {
        if (errorCode == boost::asio::error::operation_aborted) {
            // The acceptor was closed via stop(); do not re-arm.
            return;
        }
        if (!errorCode) {
            auto connection = std::make_shared<LanFramedConnection>(std::move(*socket));
            const TransportPeerId peerId = nextPeerId_++;
            connections_.emplace(peerId, connection);
            acceptedPeers_.push_back(peerId);
            connection->startReading(
                [this, peerId](std::uint8_t kind, std::string payload) {
                    if (kind == static_cast<std::uint8_t>(LanFrameKind::ClientCommand)) {
                        pendingCommands_.push_back({peerId, std::move(payload)});
                    }
                },
                [this, peerId]() { connections_.erase(peerId); });
        }
        beginAccept();
    });
}

std::vector<TransportPeerId> LanMatchTransport::drainAcceptedPeers() {
    std::vector<TransportPeerId> peers;
    peers.swap(acceptedPeers_);
    return peers;
}

bool LanMatchTransport::disconnectPeer(TransportPeerId peerId) {
    const auto connectionIt = connections_.find(peerId);
    if (connectionIt == connections_.end()) {
        return false;
    }
    connectionIt->second->close();
    connections_.erase(connectionIt);
    return true;
}

std::vector<ReceivedClientCommand> LanMatchTransport::drainClientCommands() {
    std::vector<ReceivedClientCommand> commands;
    commands.reserve(pendingCommands_.size());
    while (!pendingCommands_.empty()) {
        commands.push_back(std::move(pendingCommands_.front()));
        pendingCommands_.pop_front();
    }
    return commands;
}

bool LanMatchTransport::sendCommandResult(TransportPeerId peerId, std::string payload) {
    const auto connectionIt = connections_.find(peerId);
    if (payload.empty() || connectionIt == connections_.end()) {
        return false;
    }
    connectionIt->second->queueWrite(static_cast<std::uint8_t>(LanFrameKind::CommandResult), std::move(payload));
    return true;
}

void LanMatchTransport::publishSnapshot(std::string payload) {
    if (payload.empty()) {
        return;
    }
    for (auto& [peerId, connection] : connections_) {
        (void)peerId;
        connection->queueWrite(static_cast<std::uint8_t>(LanFrameKind::Snapshot), payload);
    }
}

} // namespace multiplayer
