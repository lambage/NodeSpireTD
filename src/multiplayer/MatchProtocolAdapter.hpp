#pragma once

#include "multiplayer/MatchProtocol.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace multiplayer {

enum class CommandDecodeError {
    None,
    PayloadTooLarge,
    MalformedPayload,
    ProtocolVersionOutOfRange,
    CommandNotSet,
    InvalidTargetingMode,
};

struct DecodedPlayerCommand {
    std::optional<PlayerCommandRequest> command;
    CommandDecodeError error = CommandDecodeError::None;
};

class MatchProtocolAdapter {
  public:
    static constexpr std::size_t kMaxPlayerCommandBytes = 64 * 1024;

    static std::optional<std::string> serializePlayerCommand(const PlayerCommandRequest& command);
    static DecodedPlayerCommand decodePlayerCommand(std::string_view payload);
};

} // namespace multiplayer