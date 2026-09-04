#pragma once

#include "multiplayer/IMatchTransport.hpp"

#include <deque>
#include <unordered_map>

namespace multiplayer {

class LoopbackTransport final : public IMatchTransport {
  public:
    TransportPeerId connectClient();
    bool disconnectClient(TransportPeerId peerId);
    bool sendClientCommand(TransportPeerId peerId, std::string payload);
    std::vector<ReceivedClientCommand> drainClientCommands() override;
    bool sendCommandResult(TransportPeerId peerId, std::string payload) override;
    void publishSnapshot(std::string payload) override;
    std::vector<std::string> drainCommandResults(TransportPeerId peerId);
    std::optional<std::string> consumeLatestSnapshot(TransportPeerId peerId);

  private:
    struct ClientMailbox {
        std::deque<std::string> commandResults;
        std::optional<std::string> latestSnapshot;
    };

    TransportPeerId nextPeerId_ = 1;
    std::deque<ReceivedClientCommand> clientCommands_;
    std::unordered_map<TransportPeerId, ClientMailbox> clients_;
};

} // namespace multiplayer