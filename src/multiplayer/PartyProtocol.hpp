#pragma once

#include "multiplayer/MatchProtocol.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace multiplayer {

inline constexpr std::uint16_t kPartyProtocolVersion = 1;

// Single source of truth for the pre-match party size limit. Referenced by LocalHostPartyGate
// (host-side enforcement, authoritative) and exposed to Lua so the Lobby roster panel can render
// "N / capacity" without duplicating the number. Change this constant only.
inline constexpr std::size_t kPartyCapacity = 4;

// Single source of truth for the display-name length limit, shared by LocalHostPartyGate
// (host-side enforcement) and PlayerProfileStore (so a name a player saves locally can never be
// silently truncated/rejected only once it reaches the host).
inline constexpr std::size_t kMaxPartyDisplayNameLength = 32;

// Single source of truth for the chat/emote message length limit. Enforced by
// ChatCommandDispatcher before a line is ever relayed to the party.
inline constexpr std::size_t kMaxPartyChatMessageLength = 240;

struct PartyMemberState {
    PlayerId playerId = 0;
    std::string displayName;
    bool isHost = false;
    bool ready = false;
    // Set once this member's PlayLevelScene finishes loading the announced match; see
    // PartyMatchStartAnnouncement/PartyMatchLoadedReady.
    bool loaded = false;
};

struct PartyRosterSnapshot {
    std::vector<PartyMemberState> members;
    std::uint32_t capacity = static_cast<std::uint32_t>(kPartyCapacity);
};

struct PartyJoinRequest {
    std::uint16_t protocolVersion = kPartyProtocolVersion;
    std::string displayName;
    // Stable local-profile identity (see PlayerProfileStore). Never shown in any UI -- used only
    // so the host can recognize a returning player (e.g. after a dropped connection) and hand
    // them back the same PlayerId instead of minting a new one. May be empty for legacy/unknown
    // clients; the host then always mints a fresh PlayerId.
    std::string playerUuid;
};

enum class PartyJoinRejectionReason : std::uint8_t {
    Unspecified,
    ProtocolVersionUnsupported,
    PartyFull,
    DisplayNameInvalid,
    // A member with this playerUuid is already an active party member; a modified client cannot
    // use a duplicated identity to desync the host's reconnect bookkeeping.
    AlreadyConnected,
};

struct PartyJoinAccepted {
    PlayerId playerId = 0;
    PartyRosterSnapshot roster;
};

struct PartyJoinRejected {
    PartyJoinRejectionReason reason = PartyJoinRejectionReason::Unspecified;
};

using PartyJoinResult = std::variant<PartyJoinAccepted, PartyJoinRejected>;

// Client -> host intent only; the host recomputes and re-broadcasts the authoritative roster.
struct PartySetReadyRequest {
    bool ready = false;
};

// Host -> clients broadcast when a member disconnects or leaves voluntarily.
struct PartyLeaveNotice {
    PlayerId playerId = 0;
};

// Client -> host intent. The host must verify the sender is the current party host before
// honoring this -- targetPlayerId alone grants no authority.
struct PartyKickRequest {
    PlayerId targetPlayerId = 0;
};

// Host -> all clients, sent once the host starts a match. Every member (host included) must load
// the named level and reply with PartyMatchLoadedReady before the host ticks match simulation.
struct PartyMatchStartAnnouncement {
    std::string levelName;
    std::string levelScriptPath;
    std::string levelAssetPath;
};

// Client -> host, sent once the client's PlayLevel scene finishes loading the announced level.
struct PartyMatchLoadedReady {
    bool ready = true;
};

// Client -> host intent: raw, untrusted chat input (plain text or a "/command args" line). The
// host is the sole authority on what -- if anything -- gets relayed; see ChatCommandDispatcher.
struct PartyChatSendRequest {
    std::string text;
};

// Host -> all party members (including the sender), the authoritative, already-formatted chat or
// emote line to display. Never mutates gameplay state -- purely cosmetic/social.
struct PartyChatMessage {
    PlayerId playerId = 0;
    std::string displayName;
    std::string text;
    // True for emote-formatted lines (e.g. "PlayerX roars") so clients can style them distinctly
    // without re-parsing slash commands themselves.
    bool isEmote = false;
};

// Host -> the sending client only (never relayed to the party): feedback for chat input that
// could not be turned into a broadcastable message, e.g. an unrecognized slash command.
struct PartyChatCommandError {
    std::string message;
};

} // namespace multiplayer
