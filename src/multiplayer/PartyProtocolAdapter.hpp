#pragma once

#include "multiplayer/PartyProtocol.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace multiplayer {

enum class PartyJoinRequestDecodeError {
    None,
    PayloadTooLarge,
    MalformedPayload,
    ProtocolVersionOutOfRange,
};

struct DecodedPartyJoinRequest {
    std::optional<PartyJoinRequest> request;
    PartyJoinRequestDecodeError error = PartyJoinRequestDecodeError::None;
};

// Serializes/parses PartyProtocol.hpp structs to/from the nodespire.party.v1 wire format.
// Mirrors MatchProtocolAdapter's shape but stays a separate class/schema on purpose: party
// messages must never be confused with (or accidentally decoded as) match commands.
class PartyProtocolAdapter {
  public:
    static constexpr std::size_t kMaxJoinRequestBytes = 4 * 1024;
    static constexpr std::size_t kMaxRosterBytes = 64 * 1024;
    static constexpr std::size_t kMaxChatBytes = 1 * 1024;

    static std::optional<std::string> serializePartyJoinRequest(const PartyJoinRequest& request);
    static DecodedPartyJoinRequest decodePartyJoinRequest(std::string_view payload);

    static std::optional<std::string> serializePartyJoinResult(const PartyJoinResult& result);
    static std::optional<PartyJoinResult> decodePartyJoinResult(std::string_view payload);

    static std::optional<std::string> serializePartyRosterSnapshot(const PartyRosterSnapshot& roster);
    static std::optional<PartyRosterSnapshot> decodePartyRosterSnapshot(std::string_view payload);

    static std::optional<std::string> serializePartySetReadyRequest(const PartySetReadyRequest& request);
    static std::optional<PartySetReadyRequest> decodePartySetReadyRequest(std::string_view payload);

    static std::optional<std::string> serializePartyLeaveNotice(const PartyLeaveNotice& notice);
    static std::optional<PartyLeaveNotice> decodePartyLeaveNotice(std::string_view payload);

    static std::optional<std::string> serializePartyKickRequest(const PartyKickRequest& request);
    static std::optional<PartyKickRequest> decodePartyKickRequest(std::string_view payload);

    static std::optional<std::string> serializePartyMatchStartAnnouncement(const PartyMatchStartAnnouncement& announcement);
    static std::optional<PartyMatchStartAnnouncement> decodePartyMatchStartAnnouncement(std::string_view payload);

    static std::optional<std::string> serializePartyMatchLoadedReady(const PartyMatchLoadedReady& notice);
    static std::optional<PartyMatchLoadedReady> decodePartyMatchLoadedReady(std::string_view payload);

    static std::optional<std::string> serializePartyChatSendRequest(const PartyChatSendRequest& request);
    static std::optional<PartyChatSendRequest> decodePartyChatSendRequest(std::string_view payload);

    static std::optional<std::string> serializePartyChatMessage(const PartyChatMessage& message);
    static std::optional<PartyChatMessage> decodePartyChatMessage(std::string_view payload);

    static std::optional<std::string> serializePartyChatCommandError(const PartyChatCommandError& error);
    static std::optional<PartyChatCommandError> decodePartyChatCommandError(std::string_view payload);
};

} // namespace multiplayer
