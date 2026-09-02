#include "multiplayer/LanMatchClient.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace multiplayer {

LanMatchClient::LanMatchClient(boost::asio::io_context& ioContext) : ioContext_(ioContext) {}

bool LanMatchClient::connect(const std::string& host, unsigned short port) {
    spdlog::info("LanMatchClient: connecting to host {}:{}...", host, port);
    boost::asio::ip::tcp::socket socket(ioContext_);
    boost::asio::ip::tcp::resolver resolver(ioContext_);
    boost::system::error_code errorCode;

    const auto endpoints = resolver.resolve(host, std::to_string(port), errorCode);
    if (errorCode) {
        spdlog::error("LanMatchClient: failed to resolve host {}:{}: {}", host, port, errorCode.message());
        return false;
    }
    boost::asio::connect(socket, endpoints, errorCode);
    if (errorCode) {
        spdlog::error("LanMatchClient: failed to connect to host {}:{}: {}", host, port, errorCode.message());
        return false;
    }

    spdlog::info("LanMatchClient: connected to host {}:{}.", host, port);
    connection_ = std::make_shared<LanFramedConnection>(std::move(socket));
    connection_->startReading(
        [this](std::uint8_t kind, std::string payload) {
            if (kind == static_cast<std::uint8_t>(LanFrameKind::CommandResult)) {
                commandResults_.push_back(std::move(payload));
            } else if (kind == static_cast<std::uint8_t>(LanFrameKind::Snapshot)) {
                latestSnapshot_ = std::move(payload);
            } else if (kind == static_cast<std::uint8_t>(LanFrameKind::JoinResult)) {
                latestJoinResult_ = std::move(payload);
            }
        },
        [this]() {
            spdlog::warn("LanMatchClient: connection to host lost.");
            connection_.reset();
        });
    return true;
}

bool LanMatchClient::sendJoinRequest(std::string payload) {
    if (payload.empty() || !connection_) {
        return false;
    }
    connection_->queueWrite(static_cast<std::uint8_t>(LanFrameKind::JoinRequest), std::move(payload));
    return true;
}

std::optional<std::string> LanMatchClient::consumeJoinResult() {
    if (!latestJoinResult_) {
        return std::nullopt;
    }
    return std::exchange(latestJoinResult_, std::nullopt);
}

void LanMatchClient::disconnect() {
    if (connection_) {
        connection_->close();
        connection_.reset();
    }
}

bool LanMatchClient::isConnected() const {
    return connection_ != nullptr;
}

bool LanMatchClient::sendCommand(std::string payload) {
    if (payload.empty() || !connection_) {
        return false;
    }
    connection_->queueWrite(static_cast<std::uint8_t>(LanFrameKind::ClientCommand), std::move(payload));
    return true;
}

std::vector<std::string> LanMatchClient::drainCommandResults() {
    std::vector<std::string> results;
    results.reserve(commandResults_.size());
    while (!commandResults_.empty()) {
        results.push_back(std::move(commandResults_.front()));
        commandResults_.pop_front();
    }
    return results;
}

std::optional<std::string> LanMatchClient::consumeLatestSnapshot() {
    if (!latestSnapshot_) {
        return std::nullopt;
    }
    return std::exchange(latestSnapshot_, std::nullopt);
}

} // namespace multiplayer
