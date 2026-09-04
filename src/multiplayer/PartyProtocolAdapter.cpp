#include "multiplayer/PartyProtocolAdapter.hpp"

#include "nodespire/party/v1/party.pb.h"

#include <limits>
#include <type_traits>

namespace multiplayer {
namespace {

using WireJoinRequest = nodespire::party::v1::PartyJoinRequest;
using WireJoinResult = nodespire::party::v1::PartyJoinResult;
using WireJoinRejectedReason = nodespire::party::v1::PartyJoinRejected::Reason;
using WireRosterSnapshot = nodespire::party::v1::PartyRosterSnapshot;
using WireMemberState = nodespire::party::v1::PartyMemberState;
using WireSetReadyRequest = nodespire::party::v1::PartySetReadyRequest;
using WireLeaveNotice = nodespire::party::v1::PartyLeaveNotice;
using WireKickRequest = nodespire::party::v1::PartyKickRequest;
using WireMatchStartAnnouncement = nodespire::party::v1::PartyMatchStartAnnouncement;
using WireMatchLoadedReady = nodespire::party::v1::PartyMatchLoadedReady;
using WireChatSendRequest = nodespire::party::v1::PartyChatSendRequest;
using WireChatMessage = nodespire::party::v1::PartyChatMessage;
using WireChatCommandError = nodespire::party::v1::PartyChatCommandError;

WireJoinRejectedReason toWireJoinRejectionReason(PartyJoinRejectionReason reason) {
    switch (reason) {
    case PartyJoinRejectionReason::Unspecified:
        return nodespire::party::v1::PartyJoinRejected::REASON_UNSPECIFIED;
    case PartyJoinRejectionReason::ProtocolVersionUnsupported:
        return nodespire::party::v1::PartyJoinRejected::PROTOCOL_VERSION_UNSUPPORTED;
    case PartyJoinRejectionReason::PartyFull:
        return nodespire::party::v1::PartyJoinRejected::PARTY_FULL;
    case PartyJoinRejectionReason::DisplayNameInvalid:
        return nodespire::party::v1::PartyJoinRejected::DISPLAY_NAME_INVALID;
    case PartyJoinRejectionReason::AlreadyConnected:
        return nodespire::party::v1::PartyJoinRejected::ALREADY_CONNECTED;
    }
    return nodespire::party::v1::PartyJoinRejected::REASON_UNSPECIFIED;
}

PartyJoinRejectionReason fromWireJoinRejectionReason(WireJoinRejectedReason reason) {
    switch (reason) {
    case nodespire::party::v1::PartyJoinRejected::PROTOCOL_VERSION_UNSUPPORTED:
        return PartyJoinRejectionReason::ProtocolVersionUnsupported;
    case nodespire::party::v1::PartyJoinRejected::PARTY_FULL:
        return PartyJoinRejectionReason::PartyFull;
    case nodespire::party::v1::PartyJoinRejected::DISPLAY_NAME_INVALID:
        return PartyJoinRejectionReason::DisplayNameInvalid;
    case nodespire::party::v1::PartyJoinRejected::ALREADY_CONNECTED:
        return PartyJoinRejectionReason::AlreadyConnected;
    default:
        return PartyJoinRejectionReason::Unspecified;
    }
}

void toWireRoster(const PartyRosterSnapshot& roster, WireRosterSnapshot& wireRoster) {
    wireRoster.set_capacity(roster.capacity);
    for (const auto& member : roster.members) {
        WireMemberState* wireMember = wireRoster.add_members();
        wireMember->set_player_id(member.playerId);
        wireMember->set_display_name(member.displayName);
        wireMember->set_is_host(member.isHost);
        wireMember->set_ready(member.ready);
        wireMember->set_loaded(member.loaded);
    }
}

PartyRosterSnapshot fromWireRoster(const WireRosterSnapshot& wireRoster) {
    PartyRosterSnapshot roster;
    roster.capacity = wireRoster.capacity();
    roster.members.reserve(static_cast<std::size_t>(wireRoster.members_size()));
    for (const auto& wireMember : wireRoster.members()) {
        roster.members.push_back(PartyMemberState{wireMember.player_id(), wireMember.display_name(),
                                                   wireMember.is_host(), wireMember.ready(), wireMember.loaded()});
    }
    return roster;
}

} // namespace

std::optional<std::string> PartyProtocolAdapter::serializePartyJoinRequest(const PartyJoinRequest& request) {
    WireJoinRequest wireRequest;
    wireRequest.set_protocol_version(request.protocolVersion);
    wireRequest.set_display_name(request.displayName);
    wireRequest.set_player_uuid(request.playerUuid);

    if (wireRequest.ByteSizeLong() > kMaxJoinRequestBytes) {
        return std::nullopt;
    }

    std::string bytes;
    return wireRequest.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

DecodedPartyJoinRequest PartyProtocolAdapter::decodePartyJoinRequest(std::string_view payload) {
    if (payload.size() > kMaxJoinRequestBytes) {
        return {.request = std::nullopt, .error = PartyJoinRequestDecodeError::PayloadTooLarge};
    }

    WireJoinRequest wireRequest;
    if (!wireRequest.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return {.request = std::nullopt, .error = PartyJoinRequestDecodeError::MalformedPayload};
    }
    if (wireRequest.protocol_version() > std::numeric_limits<std::uint16_t>::max()) {
        return {.request = std::nullopt, .error = PartyJoinRequestDecodeError::ProtocolVersionOutOfRange};
    }

    PartyJoinRequest request;
    request.protocolVersion = static_cast<std::uint16_t>(wireRequest.protocol_version());
    request.displayName = wireRequest.display_name();
    request.playerUuid = wireRequest.player_uuid();
    return {.request = std::move(request), .error = PartyJoinRequestDecodeError::None};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyJoinResult(const PartyJoinResult& result) {
    WireJoinResult wireResult;
    std::visit(
        [&wireResult](const auto& joinResult) {
            using Result = std::decay_t<decltype(joinResult)>;
            if constexpr (std::is_same_v<Result, PartyJoinAccepted>) {
                auto* accepted = wireResult.mutable_accepted();
                accepted->set_player_id(joinResult.playerId);
                toWireRoster(joinResult.roster, *accepted->mutable_roster());
            } else {
                wireResult.mutable_rejected()->set_reason(toWireJoinRejectionReason(joinResult.reason));
            }
        },
        result);

    std::string bytes;
    return wireResult.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyJoinResult> PartyProtocolAdapter::decodePartyJoinResult(std::string_view payload) {
    WireJoinResult wireResult;
    if (!wireResult.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }

    switch (wireResult.result_case()) {
    case WireJoinResult::kAccepted:
        return PartyJoinResult{
            PartyJoinAccepted{wireResult.accepted().player_id(), fromWireRoster(wireResult.accepted().roster())}};
    case WireJoinResult::kRejected:
        return PartyJoinResult{PartyJoinRejected{fromWireJoinRejectionReason(wireResult.rejected().reason())}};
    case WireJoinResult::RESULT_NOT_SET:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> PartyProtocolAdapter::serializePartyRosterSnapshot(const PartyRosterSnapshot& roster) {
    WireRosterSnapshot wireRoster;
    toWireRoster(roster, wireRoster);

    if (wireRoster.ByteSizeLong() > kMaxRosterBytes) {
        return std::nullopt;
    }

    std::string bytes;
    return wireRoster.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyRosterSnapshot> PartyProtocolAdapter::decodePartyRosterSnapshot(std::string_view payload) {
    if (payload.size() > kMaxRosterBytes) {
        return std::nullopt;
    }

    WireRosterSnapshot wireRoster;
    if (!wireRoster.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return fromWireRoster(wireRoster);
}

std::optional<std::string> PartyProtocolAdapter::serializePartySetReadyRequest(const PartySetReadyRequest& request) {
    WireSetReadyRequest wireRequest;
    wireRequest.set_ready(request.ready);

    std::string bytes;
    return wireRequest.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartySetReadyRequest> PartyProtocolAdapter::decodePartySetReadyRequest(std::string_view payload) {
    WireSetReadyRequest wireRequest;
    if (!wireRequest.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartySetReadyRequest{wireRequest.ready()};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyLeaveNotice(const PartyLeaveNotice& notice) {
    WireLeaveNotice wireNotice;
    wireNotice.set_player_id(notice.playerId);

    std::string bytes;
    return wireNotice.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyLeaveNotice> PartyProtocolAdapter::decodePartyLeaveNotice(std::string_view payload) {
    WireLeaveNotice wireNotice;
    if (!wireNotice.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyLeaveNotice{wireNotice.player_id()};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyKickRequest(const PartyKickRequest& request) {
    WireKickRequest wireRequest;
    wireRequest.set_target_player_id(request.targetPlayerId);

    std::string bytes;
    return wireRequest.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyKickRequest> PartyProtocolAdapter::decodePartyKickRequest(std::string_view payload) {
    WireKickRequest wireRequest;
    if (!wireRequest.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyKickRequest{wireRequest.target_player_id()};
}

std::optional<std::string>
PartyProtocolAdapter::serializePartyMatchStartAnnouncement(const PartyMatchStartAnnouncement& announcement) {
    WireMatchStartAnnouncement wireAnnouncement;
    wireAnnouncement.set_level_name(announcement.levelName);
    wireAnnouncement.set_level_script_path(announcement.levelScriptPath);
    wireAnnouncement.set_level_asset_path(announcement.levelAssetPath);

    std::string bytes;
    return wireAnnouncement.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyMatchStartAnnouncement>
PartyProtocolAdapter::decodePartyMatchStartAnnouncement(std::string_view payload) {
    WireMatchStartAnnouncement wireAnnouncement;
    if (!wireAnnouncement.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyMatchStartAnnouncement{wireAnnouncement.level_name(), wireAnnouncement.level_script_path(),
                                       wireAnnouncement.level_asset_path()};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyMatchLoadedReady(const PartyMatchLoadedReady& notice) {
    WireMatchLoadedReady wireNotice;
    wireNotice.set_ready(notice.ready);

    std::string bytes;
    return wireNotice.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyMatchLoadedReady> PartyProtocolAdapter::decodePartyMatchLoadedReady(std::string_view payload) {
    WireMatchLoadedReady wireNotice;
    if (!wireNotice.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyMatchLoadedReady{wireNotice.ready()};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyChatSendRequest(const PartyChatSendRequest& request) {
    WireChatSendRequest wireRequest;
    wireRequest.set_text(request.text);

    if (wireRequest.ByteSizeLong() > kMaxChatBytes) {
        return std::nullopt;
    }

    std::string bytes;
    return wireRequest.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyChatSendRequest> PartyProtocolAdapter::decodePartyChatSendRequest(std::string_view payload) {
    if (payload.size() > kMaxChatBytes) {
        return std::nullopt;
    }
    WireChatSendRequest wireRequest;
    if (!wireRequest.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyChatSendRequest{wireRequest.text()};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyChatMessage(const PartyChatMessage& message) {
    WireChatMessage wireMessage;
    wireMessage.set_player_id(message.playerId);
    wireMessage.set_display_name(message.displayName);
    wireMessage.set_text(message.text);
    wireMessage.set_is_emote(message.isEmote);

    if (wireMessage.ByteSizeLong() > kMaxChatBytes) {
        return std::nullopt;
    }

    std::string bytes;
    return wireMessage.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyChatMessage> PartyProtocolAdapter::decodePartyChatMessage(std::string_view payload) {
    if (payload.size() > kMaxChatBytes) {
        return std::nullopt;
    }
    WireChatMessage wireMessage;
    if (!wireMessage.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyChatMessage{wireMessage.player_id(), wireMessage.display_name(), wireMessage.text(),
                            wireMessage.is_emote()};
}

std::optional<std::string> PartyProtocolAdapter::serializePartyChatCommandError(const PartyChatCommandError& error) {
    WireChatCommandError wireError;
    wireError.set_message(error.message);

    if (wireError.ByteSizeLong() > kMaxChatBytes) {
        return std::nullopt;
    }

    std::string bytes;
    return wireError.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

std::optional<PartyChatCommandError> PartyProtocolAdapter::decodePartyChatCommandError(std::string_view payload) {
    if (payload.size() > kMaxChatBytes) {
        return std::nullopt;
    }
    WireChatCommandError wireError;
    if (!wireError.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return std::nullopt;
    }
    return PartyChatCommandError{wireError.message()};
}

} // namespace multiplayer
