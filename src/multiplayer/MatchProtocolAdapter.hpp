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

enum class JoinRequestDecodeError {
    None,
    PayloadTooLarge,
    MalformedPayload,
    ProtocolVersionOutOfRange,
};

struct DecodedJoinMatchRequest {
    std::optional<JoinMatchRequest> request;
    JoinRequestDecodeError error = JoinRequestDecodeError::None;
};

class MatchProtocolAdapter {
  public:
    static constexpr std::size_t kMaxPlayerCommandBytes = 64 * 1024;
    static constexpr std::size_t kMaxJoinRequestBytes = 4 * 1024;

    static std::optional<std::string> serializePlayerCommand(const PlayerCommandRequest& command);
    static DecodedPlayerCommand decodePlayerCommand(std::string_view payload);
    static std::optional<std::string> serializePlayerCommandResult(const PlayerCommandResult& result);

    static std::optional<std::string> serializeJoinMatchRequest(const JoinMatchRequest& request);
    static DecodedJoinMatchRequest decodeJoinMatchRequest(std::string_view payload);
    static std::optional<std::string> serializeJoinMatchResult(const JoinMatchResult& result);
    static std::optional<JoinMatchResult> decodeJoinMatchResult(std::string_view payload);
};

} // namespace multiplayer