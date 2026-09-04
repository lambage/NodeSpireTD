#include "multiplayer/LoopbackTransport.hpp"

#include <utility>

namespace multiplayer {

TransportPeerId LoopbackTransport::connectClient() {
    const TransportPeerId peerId = nextPeerId_++;
    clients_.emplace(peerId, ClientMailbox{});
    return peerId;
}

bool LoopbackTransport::disconnectClient(TransportPeerId peerId) {
    return clients_.erase(peerId) != 0;
}

bool LoopbackTransport::sendClientCommand(TransportPeerId peerId, std::string payload) {
    if (payload.empty() || !clients_.contains(peerId)) {
        return false;
    }
    clientCommands_.push_back({peerId, std::move(payload)});
    return true;
}

std::vector<ReceivedClientCommand> LoopbackTransport::drainClientCommands() {
    std::vector<ReceivedClientCommand> commands;
    commands.reserve(clientCommands_.size());
    while (!clientCommands_.empty()) {
        commands.push_back(std::move(clientCommands_.front()));
        clientCommands_.pop_front();
    }
    return commands;
}

bool LoopbackTransport::sendCommandResult(TransportPeerId peerId, std::string payload) {
    const auto clientIt = clients_.find(peerId);
    if (payload.empty() || clientIt == clients_.end()) {
        return false;
    }
    clientIt->second.commandResults.push_back(std::move(payload));
    return true;
}

void LoopbackTransport::publishSnapshot(std::string payload) {
    if (payload.empty()) {
        return;
    }
    for (auto& [peerId, mailbox] : clients_) {
        (void)peerId;
        mailbox.latestSnapshot = payload;
    }
}

std::vector<std::string> LoopbackTransport::drainCommandResults(TransportPeerId peerId) {
    const auto clientIt = clients_.find(peerId);
    if (clientIt == clients_.end()) {
        return {};
    }
    std::vector<std::string> results;
    results.reserve(clientIt->second.commandResults.size());
    while (!clientIt->second.commandResults.empty()) {
        results.push_back(std::move(clientIt->second.commandResults.front()));
        clientIt->second.commandResults.pop_front();
    }
    return results;
}

std::optional<std::string> LoopbackTransport::consumeLatestSnapshot(TransportPeerId peerId) {
    const auto clientIt = clients_.find(peerId);
    if (clientIt == clients_.end() || !clientIt->second.latestSnapshot) {
        return std::nullopt;
    }
    return std::exchange(clientIt->second.latestSnapshot, std::nullopt);
}

} // namespace multiplayer