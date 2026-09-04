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

    bool sendJoinRequest(std::string payload);
    std::optional<std::string> consumeJoinResult();

    bool sendCommand(std::string payload);
    std::vector<std::string> drainCommandResults();
    std::optional<std::string> consumeLatestSnapshot();

    // Party-phase traffic (see LanMatchTransport for the host-side counterpart).
    bool sendPartyJoinRequest(std::string payload);
    std::optional<std::string> consumePartyJoinResult();
    std::optional<std::string> consumeLatestPartyRosterSnapshot();
    bool sendPartySetReadyRequest(std::string payload);
    bool sendPartyKickRequest(std::string payload);
    bool sendPartyMatchLoadedReady(std::string payload);
    std::optional<std::string> consumePartyMatchStart();

    // Chat/emote traffic. Unlike the roster (latest-wins snapshot), chat is a stream: every line
    // must be delivered, so these accumulate in a queue drained in order.
    bool sendPartyChatSendRequest(std::string payload);
    std::vector<std::string> drainPartyChatMessages();
    std::vector<std::string> drainPartyChatCommandErrors();

  private:
    boost::asio::io_context& ioContext_;
    std::shared_ptr<LanFramedConnection> connection_;
    std::optional<std::string> latestJoinResult_;
    std::deque<std::string> commandResults_;
    std::optional<std::string> latestSnapshot_;
    std::optional<std::string> latestPartyJoinResult_;
    std::optional<std::string> latestPartyRosterSnapshot_;
    std::optional<std::string> latestPartyMatchStart_;
    std::deque<std::string> partyChatMessages_;
    std::deque<std::string> partyChatCommandErrors_;
};

} // namespace multiplayer
