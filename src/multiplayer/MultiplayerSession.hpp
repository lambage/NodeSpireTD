#pragma once

#include "multiplayer/ChatCommandDispatcher.hpp"
#include "multiplayer/LanMatchClient.hpp"
#include "multiplayer/LanMatchTransport.hpp"
#include "multiplayer/LocalHostPartyGate.hpp"
#include "multiplayer/PartyProtocol.hpp"

#include <boost/asio/io_context.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace multiplayer {

enum class MultiplayerRole { Solo, Host, Client };

// Persistent, scene-independent LAN session. Owned by the top-level app runtime (see
// SceneDirector in AppController.cpp) alongside other cross-scene state, not by any single scene:
// LobbyScene and PlayLevelScene both operate on the same instance via SceneSharedState so the TCP
// connection established while forming a party carries straight into match play instead of being
// torn down and rebuilt at the scene boundary. update() must be called exactly once per frame by
// the owner regardless of which scene is active, so party/chat traffic keeps flowing even while a
// level is loading.
class MultiplayerSession {
  public:
    MultiplayerSession() = default;

    void update();

    bool hostParty(unsigned short port, std::string displayName, std::string playerUuid);
    bool joinParty(const std::string& address, unsigned short port, std::string displayName, std::string playerUuid);
    // Tears down the connection (if any) and returns to solo play with only the local player in
    // the roster.
    void leaveParty();

    MultiplayerRole role() const { return role_; }
    bool isHost() const { return role_ == MultiplayerRole::Host; }
    bool isClient() const { return role_ == MultiplayerRole::Client; }
    bool isInParty() const { return role_ != MultiplayerRole::Solo; }

    // Host/solo: the authoritative gate's roster. Client: the last snapshot pushed by the host.
    PartyRosterSnapshot roster() const;
    PlayerId localPlayerId() const { return localPlayerId_; }

    bool setLocalReady(bool ready);
    // Host-only; re-validated against the gate (a non-host caller cannot spoof authorization by
    // simply calling this).
    bool kickMember(PlayerId targetPlayerId);

    // Host-only: clears loaded flags, broadcasts the announcement, and marks the barrier pending.
    // Returns false if not currently hosting.
    bool announceMatchStart(std::string levelName, std::string levelScriptPath, std::string levelAssetPath);
    // Non-null exactly once per announcement the client hasn't yet consumed.
    std::optional<PartyMatchStartAnnouncement> consumeMatchStartAnnouncement();

    // Called by PlayLevelScene once its own load finishes (host and client alike).
    void signalLocalLoadedReady();
    bool allMembersLoadedReady() const;

    // Chat/emote intent from the local player. Solo/host: dispatched immediately (and relayed to
    // the party if hosting). Client: sent to the host, which is the sole authority on what (if
    // anything) gets relayed back. Never mutates gameplay state.
    bool sendChatMessage(const std::string& rawText);
    // New chat lines since the last call, oldest first (host/solo and relayed-from-host alike).
    std::vector<PartyChatMessage> consumeChatMessages();
    // Sender-only feedback (e.g. unrecognized slash command) since the last call.
    std::vector<std::string> consumeChatErrors();

    // Raw transport/client access for match-level (post-party) join handshakes and gameplay
    // command/snapshot traffic over the already-established connection.
    LanMatchTransport& hostTransport() { return hostTransport_; }
    LanMatchClient& client() { return client_; }
    std::optional<PlayerId> playerIdForPeer(TransportPeerId peerId) const;

  private:
    void pumpHostSide();
    void pumpClientSide();
    void broadcastRoster();
    void resetToSolo();
    // Pushes a display-only system line (playerId=0, displayName="System") into the local chat
    // log and, if hosting, relays it to every connected member -- used for join/leave/kick/host
    // lifecycle notices so they read like ordinary chat lines instead of a separate UI element.
    void broadcastSystemMessage(std::string text);
    void sendSystemMessageToPeer(TransportPeerId peerId, std::string text);
    // Host-only: best-effort targeted notice sent to a peer immediately before disconnecting them
    // for a kick, so their own client can distinguish "kicked" from an ordinary connection drop.
    void notifyPeerKicked(TransportPeerId peerId);

    boost::asio::io_context ioContext_;
    LanMatchTransport hostTransport_{ioContext_};
    LanMatchClient client_{ioContext_};

    MultiplayerRole role_ = MultiplayerRole::Solo;
    LocalHostPartyGate partyGate_;
    PartyRosterSnapshot remoteRoster_;
    PlayerId localPlayerId_ = 0;
    std::string localDisplayName_ = "Player";
    std::string localPlayerUuid_;
    std::unordered_map<TransportPeerId, PlayerId> peerToPlayerId_;
    bool clientJoinPending_ = false;
    std::optional<PartyMatchStartAnnouncement> pendingMatchStartAnnouncement_;
    ChatCommandDispatcher chatDispatcher_;
    std::vector<PartyChatMessage> pendingChatMessages_;
    std::vector<std::string> pendingChatErrors_;
};

} // namespace multiplayer
