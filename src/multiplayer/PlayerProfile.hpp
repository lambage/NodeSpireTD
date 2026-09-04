#pragma once

#include <string>

namespace multiplayer {

// Local player identity, persisted across app restarts (see PlayerProfileStore). displayName is
// the only part ever shown in the party roster/chat UI; playerUuid is a stable identifier used
// solely on the wire (PartyJoinRequest) so a host can recognize a returning player -- e.g. after
// a dropped connection -- and is never rendered anywhere.
struct PlayerProfile {
    std::string playerUuid;
    std::string displayName = "Player";
};

} // namespace multiplayer
