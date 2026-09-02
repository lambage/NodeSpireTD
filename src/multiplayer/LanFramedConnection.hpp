#pragma once

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace multiplayer {

// Frame kind tag distinguishing the LAN transport's send channels on the wire. This lives
// outside the Protobuf schema: it lets one TCP stream carry both command results and
// snapshots without changing PlayerCommandResult/MatchSnapshot definitions.
enum class LanFrameKind : std::uint8_t {
    ClientCommand = 0,
    CommandResult = 1,
    Snapshot = 2,
    JoinRequest = 3,
    JoinResult = 4,
};

// Owns the read/write pump for one TCP connection using a shared wire format:
// [1-byte frame kind][4-byte big-endian length][payload]. Used by both the LAN host
// (one per accepted peer) and the LAN client (one connection to the host).
class LanFramedConnection : public std::enable_shared_from_this<LanFramedConnection> {
  public:
    using FrameHandler = std::function<void(std::uint8_t kind, std::string payload)>;
    using ErrorHandler = std::function<void()>;

    explicit LanFramedConnection(boost::asio::ip::tcp::socket socket);

    // Begins the self-rearming read loop; onFrame fires per decoded frame, onError once on
    // the first read/write failure (the connection is unusable afterward).
    void startReading(FrameHandler onFrame, ErrorHandler onError);
    void queueWrite(std::uint8_t kind, std::string payload);
    void close();

    static constexpr std::size_t kHeaderSize = 5;

  private:
    void beginReadHeader();
    void beginWrite();

    boost::asio::ip::tcp::socket socket_;
    FrameHandler onFrame_;
    ErrorHandler onError_;
    std::array<unsigned char, kHeaderSize> readHeader_{};
    std::string readBody_;
    std::deque<std::string> outgoing_;
    bool writing_ = false;
    bool closed_ = false;
};

} // namespace multiplayer
