#pragma once

#include "multiplayer/LanFramedConnection.hpp"

#include <boost/asio.hpp>

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace multiplayer {

// Client-side counterpart to LanMatchTransport: connects to a host over TCP and exchanges
// length-prefixed, kind-tagged frames. The owner must poll the shared io_context once per
// tick to progress the connection.
class LanMatchClient {
  public:
    explicit LanMatchClient(boost::asio::io_context& ioContext);

    bool connect(const std::string& host, unsigned short port);
    void disconnect();
    bool isConnected() const;

    bool sendCommand(std::string payload);
    std::vector<std::string> drainCommandResults();
    std::optional<std::string> consumeLatestSnapshot();

  private:
    boost::asio::io_context& ioContext_;
    std::shared_ptr<LanFramedConnection> connection_;
    std::deque<std::string> commandResults_;
    std::optional<std::string> latestSnapshot_;
};

} // namespace multiplayer
