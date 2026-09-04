#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace multiplayer {

using TransportPeerId = std::uint64_t;

struct ReceivedClientCommand {
    TransportPeerId peerId = 0;
    std::string payload;
};

class IMatchTransport {
  public:
    virtual ~IMatchTransport() = default;

    virtual std::vector<ReceivedClientCommand> drainClientCommands() = 0;
    virtual bool sendCommandResult(TransportPeerId peerId, std::string payload) = 0;
    virtual void publishSnapshot(std::string payload) = 0;
};

} // namespace multiplayer