#include "multiplayer/LanFramedConnection.hpp"

namespace multiplayer {

namespace {

std::uint32_t decodeLength(const std::array<unsigned char, LanFramedConnection::kHeaderSize>& header) {
    return (static_cast<std::uint32_t>(header[1]) << 24) | (static_cast<std::uint32_t>(header[2]) << 16) |
           (static_cast<std::uint32_t>(header[3]) << 8) | static_cast<std::uint32_t>(header[4]);
}

std::array<unsigned char, LanFramedConnection::kHeaderSize> encodeHeader(std::uint8_t kind, std::uint32_t length) {
    return {
        kind,
        static_cast<unsigned char>((length >> 24) & 0xFF),
        static_cast<unsigned char>((length >> 16) & 0xFF),
        static_cast<unsigned char>((length >> 8) & 0xFF),
        static_cast<unsigned char>(length & 0xFF),
    };
}

} // namespace

LanFramedConnection::LanFramedConnection(boost::asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

void LanFramedConnection::startReading(FrameHandler onFrame, ErrorHandler onError) {
    onFrame_ = std::move(onFrame);
    onError_ = std::move(onError);
    beginReadHeader();
}

void LanFramedConnection::beginReadHeader() {
    if (closed_) {
        return;
    }
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(readHeader_),
                             [this, self](const boost::system::error_code& errorCode, std::size_t) {
                                 if (closed_) {
                                     return;
                                 }
                                 if (errorCode) {
                                     if (onError_) {
                                         onError_();
                                     }
                                     return;
                                 }

                                 const std::uint8_t kind = readHeader_[0];
                                 const std::uint32_t bodyLength = decodeLength(readHeader_);
                                 readBody_.assign(bodyLength, '\0');
                                 if (bodyLength == 0) {
                                     if (onFrame_) {
                                         onFrame_(kind, readBody_);
                                     }
                                     beginReadHeader();
                                     return;
                                 }

                                 boost::asio::async_read(
                                     socket_, boost::asio::buffer(readBody_.data(), bodyLength),
                                     [this, self, kind](const boost::system::error_code& bodyError, std::size_t) {
                                         if (closed_) {
                                             return;
                                         }
                                         if (bodyError) {
                                             if (onError_) {
                                                 onError_();
                                             }
                                             return;
                                         }
                                         if (onFrame_) {
                                             onFrame_(kind, readBody_);
                                         }
                                         beginReadHeader();
                                     });
                             });
}

void LanFramedConnection::queueWrite(std::uint8_t kind, std::string payload) {
    if (closed_) {
        return;
    }
    const auto header = encodeHeader(kind, static_cast<std::uint32_t>(payload.size()));
    std::string framed(reinterpret_cast<const char*>(header.data()), header.size());
    framed.append(payload);
    outgoing_.push_back(std::move(framed));
    beginWrite();
}

void LanFramedConnection::beginWrite() {
    if (writing_ || outgoing_.empty() || closed_) {
        return;
    }
    writing_ = true;
    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(outgoing_.front()),
                              [this, self](const boost::system::error_code& errorCode, std::size_t) {
                                  writing_ = false;
                                  if (closed_) {
                                      return;
                                  }
                                  if (errorCode) {
                                      if (onError_) {
                                          onError_();
                                      }
                                      return;
                                  }
                                  outgoing_.pop_front();
                                  beginWrite();
                              });
}

void LanFramedConnection::close() {
    closed_ = true;
    boost::system::error_code errorCode;
    socket_.close(errorCode);
}

} // namespace multiplayer
