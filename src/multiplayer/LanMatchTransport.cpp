#include "multiplayer/LanMatchTransport.hpp"

#include <spdlog/spdlog.h>

namespace multiplayer {

LanMatchTransport::LanMatchTransport(boost::asio::io_context& ioContext)
    : ioContext_(ioContext), acceptor_(ioContext) {}

bool LanMatchTransport::listen(unsigned short port) {
    boost::system::error_code errorCode;
    const boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);

    acceptor_.open(endpoint.protocol(), errorCode);
    if (errorCode) {
        spdlog::error("LanMatchTransport: failed to open acceptor on port {}: {}", port, errorCode.message());
        return false;
    }
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), errorCode);
    acceptor_.bind(endpoint, errorCode);
    if (errorCode) {
        spdlog::error("LanMatchTransport: failed to bind to port {}: {}", port, errorCode.message());
        return false;
    }
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, errorCode);
    if (errorCode) {
        spdlog::error("LanMatchTransport: failed to listen on port {}: {}", port, errorCode.message());
        return false;
    }

    spdlog::info("LanMatchTransport: listening for co-op connections on port {}.", port);
    beginAccept();
    return true;
}

void LanMatchTransport::stop() {
    boost::system::error_code errorCode;
    acceptor_.close(errorCode);
    for (auto& [peerId, peerConnection] : connections_) {
        (void)peerId;
        peerConnection.connection->close();
    }
    connections_.clear();
}

void LanMatchTransport::reset() {
    stop();
    acceptedPeers_.clear();
    pendingJoinRequests_.clear();
    pendingCommands_.clear();
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
            boost::system::error_code endpointError;
            const auto remoteEndpoint = socket->remote_endpoint(endpointError);
            const TransportPeerId peerId = nextPeerId_++;
            spdlog::info("LanMatchTransport: peer {} connected from {}.", peerId,
                         endpointError ? std::string("<unknown>")
                                       : remoteEndpoint.address().to_string() + ":" +
                                             std::to_string(remoteEndpoint.port()));
            auto connection = std::make_shared<LanFramedConnection>(std::move(*socket));
            connections_.emplace(peerId, PeerConnection{connection, false});
            acceptedPeers_.push_back(peerId);
            connection->startReading(
                [this, peerId](std::uint8_t kind, std::string payload) {
                    handleFrame(peerId, kind, std::move(payload));
                },
                [this, peerId]() {
                    spdlog::warn("LanMatchTransport: peer {} disconnected unexpectedly.", peerId);
                    connections_.erase(peerId);
                });
        } else {
            spdlog::error("LanMatchTransport: accept failed: {}", errorCode.message());
        }
        beginAccept();
    });
}

void LanMatchTransport::handleFrame(TransportPeerId peerId, std::uint8_t kind, std::string payload) {
    const auto connectionIt = connections_.find(peerId);
    if (connectionIt == connections_.end()) {
        return;
    }

    if (kind == static_cast<std::uint8_t>(LanFrameKind::JoinRequest)) {
        pendingJoinRequests_.push_back({peerId, std::move(payload)});
        return;
    }
    if (kind == static_cast<std::uint8_t>(LanFrameKind::ClientCommand)) {
        if (!connectionIt->second.joined) {
            // A peer must complete the join handshake before its commands are trusted.
            spdlog::warn("LanMatchTransport: peer {} sent a command before completing the join handshake; "
                         "disconnecting.",
                         peerId);
            disconnectPeer(peerId);
            return;
        }
        pendingCommands_.push_back({peerId, std::move(payload)});
        return;
    }
    // CommandResult/Snapshot/JoinResult are host-to-client kinds; ignore them from a client.
}

std::vector<TransportPeerId> LanMatchTransport::drainAcceptedPeers() {
    std::vector<TransportPeerId> peers;
    peers.swap(acceptedPeers_);
    return peers;
}

std::vector<PendingJoinRequest> LanMatchTransport::drainJoinRequests() {
    std::vector<PendingJoinRequest> requests;
    requests.reserve(pendingJoinRequests_.size());
    while (!pendingJoinRequests_.empty()) {
        requests.push_back(std::move(pendingJoinRequests_.front()));
        pendingJoinRequests_.pop_front();
    }
    return requests;
}

bool LanMatchTransport::markPeerJoined(TransportPeerId peerId) {
    const auto connectionIt = connections_.find(peerId);
    if (connectionIt == connections_.end()) {
        return false;
    }
    connectionIt->second.joined = true;
    return true;
}

bool LanMatchTransport::sendJoinResult(TransportPeerId peerId, std::string payload) {
    const auto connectionIt = connections_.find(peerId);
    if (payload.empty() || connectionIt == connections_.end()) {
        return false;
    }
    connectionIt->second.connection->queueWrite(static_cast<std::uint8_t>(LanFrameKind::JoinResult), std::move(payload));
    return true;
}

bool LanMatchTransport::disconnectPeer(TransportPeerId peerId) {
    const auto connectionIt = connections_.find(peerId);
    if (connectionIt == connections_.end()) {
        return false;
    }
    connectionIt->second.connection->close();
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
    if (payload.empty() || connectionIt == connections_.end() || !connectionIt->second.joined) {
        return false;
    }
    connectionIt->second.connection->queueWrite(static_cast<std::uint8_t>(LanFrameKind::CommandResult), std::move(payload));
    return true;
}

void LanMatchTransport::publishSnapshot(std::string payload) {
    if (payload.empty()) {
        return;
    }
    for (auto& [peerId, peerConnection] : connections_) {
        (void)peerId;
        if (!peerConnection.joined) {
            continue;
        }
        peerConnection.connection->queueWrite(static_cast<std::uint8_t>(LanFrameKind::Snapshot), payload);
    }
}

} // namespace multiplayer
