#include "scenes/LobbyScene.hpp"

#include "scenes/SceneSharedState.hpp"

#include <algorithm>
#include <cctype>
#include <lua.hpp>

namespace {

constexpr const char* kLevelDefinitionFilename = "level.lua";

std::string prettifyDirName(const std::string& dirName) {
    std::string result = dirName;
    bool capitalizeNext = true;
    for (char& c : result) {
        if (c == '_' || c == '-') {
            c = ' ';
            capitalizeNext = true;
        } else if (capitalizeNext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalizeNext = false;
        }
    }
    return result;
}

bool loadLevelEntryFromScript(const std::filesystem::path& levelDir,
                              const std::filesystem::path& scriptPath,
                              std::string& outDisplayName,
                              std::filesystem::path& outMapAssetPath) {
    lua_State* L = luaL_newstate();
    if (!L) {
        return false;
    }

    const std::string scriptPathString = scriptPath.string();
    if (luaL_loadfile(L, scriptPathString.c_str()) != LUA_OK) {
        lua_close(L);
        return false;
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_close(L);
        return false;
    }

    if (!lua_istable(L, -1)) {
        lua_close(L);
        return false;
    }

    std::filesystem::path mapAssetPath;
    std::string displayName;

    lua_getfield(L, -1, "mapAssetPath");
    if (lua_isstring(L, -1)) {
        mapAssetPath = std::filesystem::path(lua_tostring(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "displayName");
    if (lua_isstring(L, -1)) {
        displayName = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_close(L);

    if (mapAssetPath.empty()) {
        return false;
    }

    if (!std::filesystem::is_regular_file(mapAssetPath)) {
        return false;
    }

    outDisplayName = displayName.empty() ? prettifyDirName(levelDir.filename().string()) : displayName;
    outMapAssetPath = mapAssetPath;
    return true;
}

LobbyScene* luaSceneSelf(lua_State* L) {
    return static_cast<LobbyScene*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int pushCommandResult(lua_State* L, bool ok, const char* reason) {
    lua_newtable(L);
    lua_pushboolean(L, ok);
    lua_setfield(L, -2, "ok");
    lua_pushstring(L, reason);
    lua_setfield(L, -2, "reason");
    return 1;
}

} // namespace

LobbyScene::~LobbyScene() = default;

void LobbyScene::onEnter(SceneSharedState& state) {
    availableLevels_.clear();
    const std::filesystem::path levelsDir = "assets/levels";
    if (std::filesystem::is_directory(levelsDir)) {
        std::vector<std::filesystem::path> sortedDirs;
        for (const auto& entry : std::filesystem::directory_iterator(levelsDir)) {
            if (entry.is_directory()) {
                sortedDirs.push_back(entry.path());
            }
        }
        std::sort(sortedDirs.begin(), sortedDirs.end());
        for (const auto& dir : sortedDirs) {
            const std::filesystem::path scriptPath = dir / kLevelDefinitionFilename;
            if (!std::filesystem::is_regular_file(scriptPath)) {
                continue;
            }

            std::string displayName;
            std::filesystem::path mapAssetPath;
            if (loadLevelEntryFromScript(dir, scriptPath, displayName, mapAssetPath)) {
                availableLevels_.push_back({displayName, mapAssetPath, scriptPath});
            }
        }
    }
    if (!availableLevels_.empty()) {
        selectedLevelIndex_ = std::clamp(selectedLevelIndex_, 0, static_cast<int>(availableLevels_.size() - 1));
    } else {
        selectedLevelIndex_ = 0;
    }

    session_ = state.multiplayerSession;
    profileStore_ = state.playerProfileStore;

    scriptRef_ = loadLuaScript(state, "assets/scenes/Lobby.lua");
    registerLuaGameplayApi();
    luaOnEnter(scriptRef_);
}

void LobbyScene::onExit(SceneSharedState& state) {
    // Deliberately does not tear down the multiplayer session: it is persistent and owned by the
    // app runtime (see MultiplayerSession.hpp), not this scene. If a party is active, it carries
    // straight into PlayLevelScene rather than being disconnected/rebuilt at the scene boundary.
    luaOnExit(state, scriptRef_);
}

void LobbyScene::render(SceneSharedState& state, float dt) {
    session_ = state.multiplayerSession;
    profileStore_ = state.playerProfileStore;

    if (!availableLevels_.empty()) {
        selectedLevelIndex_ = std::clamp(selectedLevelIndex_, 0, static_cast<int>(availableLevels_.size() - 1));
        state.activeLevelName = availableLevels_[selectedLevelIndex_].name;
        state.activeLevelAssetPath = availableLevels_[selectedLevelIndex_].assetPath.string();
        state.activeLevelScriptPath = availableLevels_[selectedLevelIndex_].scriptPath.string();
    }

    // A client's own level list/selection is irrelevant once the host announces a match start:
    // override with the host's chosen level so PlayLevelScene::onEnter() loads the same one.
    matchStartJustAnnounced_ = false;
    if (session_ && !session_->isHost()) {
        if (const auto announcement = session_->consumeMatchStartAnnouncement()) {
            state.activeLevelName = announcement->levelName;
            state.activeLevelAssetPath = announcement->levelAssetPath;
            state.activeLevelScriptPath = announcement->levelScriptPath;
            matchStartJustAnnounced_ = true;
        }
    }

    luaOnRender(state, scriptRef_, dt);
}

void LobbyScene::registerLuaGameplayApi() {
    if (!L_) {
        return;
    }

    lua_getglobal(L_, "Gameplay");
    if (!lua_istable(L_, -1)) {
        lua_pop(L_, 1);
        lua_newtable(L_);
    }
    const int gameplayTable = lua_gettop(L_);

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);

            for (std::size_t i = 0; i < self->availableLevels_.size(); ++i) {
                const auto& level = self->availableLevels_[i];
                lua_newtable(L);

                lua_pushstring(L, level.name.c_str());
                lua_setfield(L, -2, "name");
                lua_pushstring(L, level.assetPath.string().c_str());
                lua_setfield(L, -2, "assetPath");
                lua_pushstring(L, level.scriptPath.string().c_str());
                lua_setfield(L, -2, "scriptPath");
                lua_pushboolean(L, static_cast<int>(i) == self->selectedLevelIndex_);
                lua_setfield(L, -2, "selected");

                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getLobbyLevels");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const int requested = static_cast<int>(luaL_checkinteger(L, 1));
            const int zeroBased = requested - 1;

            if (zeroBased < 0 || zeroBased >= static_cast<int>(self->availableLevels_.size())) {
                return pushCommandResult(L, false, "index out of range");
            }

            self->selectedLevelIndex_ = zeroBased;
            return pushCommandResult(L, true, "selected");
        },
        1);
    lua_setfield(L_, gameplayTable, "selectLobbyLevel");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushinteger(L, self->selectedLevelIndex_ + 1);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getSelectedLobbyLevel");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);
            lua_pushboolean(L, self->session_ && self->session_->isHost());
            lua_setfield(L, -2, "hosting");
            lua_pushboolean(L, self->session_ && self->session_->isClient());
            lua_setfield(L, -2, "joining");
            lua_pushinteger(L, self->lastHostPort_);
            lua_setfield(L, -2, "port");
            lua_pushstring(L, self->lastJoinAddress_.c_str());
            lua_setfield(L, -2, "joinAddress");
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getMultiplayerState");

    // setMultiplayerMode(hosting, port, joinAddress) -- hosting=true starts listening for co-op
    // peers right away; a non-empty joinAddress connects as a client right away; otherwise (both
    // false/empty) leaves the current party and returns to solo play. Unlike before, this is a
    // real connection attempt, not just an intent flag consumed later by PlayLevelScene.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const bool hosting = lua_toboolean(L, 1) != 0;
            const auto port = static_cast<unsigned short>(luaL_checkinteger(L, 2));
            const std::string joinAddress = luaL_optstring(L, 3, "");
            self->lastHostPort_ = port;
            self->lastJoinAddress_ = joinAddress;

            if (!self->session_) {
                return pushCommandResult(L, false, "no session");
            }

            bool ok = true;
            const std::string profileName = self->profileStore_ ? self->profileStore_->profile().displayName : "Player";
            const std::string profileUuid = self->profileStore_ ? self->profileStore_->profile().playerUuid : "";
            if (hosting) {
                ok = self->session_->hostParty(port, profileName, profileUuid);
            } else if (!joinAddress.empty()) {
                ok = self->session_->joinParty(joinAddress, port, profileName, profileUuid);
            } else {
                self->session_->leaveParty();
            }
            return pushCommandResult(L, ok, ok ? "updated" : "connection failed");
        },
        1);
    lua_setfield(L_, gameplayTable, "setMultiplayerMode");

    // getPartyRoster() -> { capacity = N, members = { {id, name, isHost, ready, loaded, isLocal}, ... } }
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const auto roster = self->session_ ? self->session_->roster() : multiplayer::PartyRosterSnapshot{};
            const multiplayer::PlayerId localPlayerId = self->session_ ? self->session_->localPlayerId() : 0;

            lua_newtable(L);
            lua_pushinteger(L, static_cast<lua_Integer>(roster.capacity));
            lua_setfield(L, -2, "capacity");

            lua_newtable(L);
            for (std::size_t i = 0; i < roster.members.size(); ++i) {
                const auto& member = roster.members[i];
                lua_newtable(L);
                lua_pushinteger(L, static_cast<lua_Integer>(member.playerId));
                lua_setfield(L, -2, "id");
                lua_pushstring(L, member.displayName.c_str());
                lua_setfield(L, -2, "name");
                lua_pushboolean(L, member.isHost);
                lua_setfield(L, -2, "isHost");
                lua_pushboolean(L, member.ready);
                lua_setfield(L, -2, "ready");
                lua_pushboolean(L, member.loaded);
                lua_setfield(L, -2, "loaded");
                lua_pushboolean(L, member.playerId == localPlayerId);
                lua_setfield(L, -2, "isLocal");
                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(L, -2, "members");
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getPartyRoster");

    // setPartyReady(ready) -- toggles the local player's ready flag. For a client this sends a
    // PartySetReadyRequest and waits for the host's next PartyRosterSnapshot to reflect it.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const bool ready = lua_toboolean(L, 1) != 0;
            const bool ok = self->session_ && self->session_->setLocalReady(ready);
            return pushCommandResult(L, ok, ok ? "updated" : "unavailable");
        },
        1);
    lua_setfield(L_, gameplayTable, "setPartyReady");

    // kickPartyMember(playerId) -- host-only; the session re-validates the requester is the host
    // rather than trusting the UI to only show the button to a host.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const auto targetId = static_cast<multiplayer::PlayerId>(luaL_checkinteger(L, 1));
            const bool ok = self->session_ && self->session_->kickMember(targetId);
            return pushCommandResult(L, ok, ok ? "kicked" : "not authorized");
        },
        1);
    lua_setfield(L_, gameplayTable, "kickPartyMember");

    // announceMatchStart() -- host-only. Broadcasts the currently-selected level to every party
    // member and resets the loaded-ready barrier; call immediately before requestScene(PlayLevel).
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            if (!self->session_ || !self->session_->isHost() || self->selectedLevelIndex_ < 0 ||
                self->selectedLevelIndex_ >= static_cast<int>(self->availableLevels_.size())) {
                return pushCommandResult(L, false, "not hosting or no level selected");
            }
            const auto& level = self->availableLevels_[self->selectedLevelIndex_];
            const bool ok = self->session_->announceMatchStart(level.name, level.scriptPath.string(),
                                                               level.assetPath.string());
            return pushCommandResult(L, ok, ok ? "announced" : "failed");
        },
        1);
    lua_setfield(L_, gameplayTable, "announceMatchStart");

    // checkPartyMatchStart() -- client-only poll; true on the single frame the host's match-start
    // announcement was just consumed (state.activeLevel* has already been overridden that frame).
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_pushboolean(L, self->matchStartJustAnnounced_);
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "checkPartyMatchStart");

    // getLocalProfile() -> { name = displayName }. The profile's UUID is intentionally never
    // exposed to Lua/UI -- it only ever travels host-ward inside setMultiplayerMode()'s join/host
    // calls, so it stays invisible even to the scripting layer.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            lua_newtable(L);
            lua_pushstring(L, self->profileStore_ ? self->profileStore_->profile().displayName.c_str() : "Player");
            lua_setfield(L, -2, "name");
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "getLocalProfile");

    // setLocalProfileName(name) -- persists immediately; does not affect a party already joined
    // (rename takes effect the next time you host/join).
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const std::string name = luaL_checkstring(L, 1);
            const bool ok = self->profileStore_ && self->profileStore_->setDisplayName(name);
            return pushCommandResult(L, ok, ok ? "saved" : "invalid name");
        },
        1);
    lua_setfield(L_, gameplayTable, "setLocalProfileName");

    // sendPartyChat(text) -- accepts plain text or a "/command args" line; the host is the sole
    // authority on what (if anything) gets relayed back (see ChatCommandDispatcher).
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const std::string text = luaL_checkstring(L, 1);
            const bool ok = self->session_ && self->session_->sendChatMessage(text);
            return pushCommandResult(L, ok, ok ? "sent" : "not sent");
        },
        1);
    lua_setfield(L_, gameplayTable, "sendPartyChat");

    // consumePartyChatMessages() -> { {id, name, text, isEmote}, ... }; new lines since the last
    // call, oldest first. Safe to poll every frame even when idle (returns an empty table).
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const auto messages = self->session_ ? self->session_->consumeChatMessages()
                                                  : std::vector<multiplayer::PartyChatMessage>{};
            lua_newtable(L);
            for (std::size_t i = 0; i < messages.size(); ++i) {
                const auto& message = messages[i];
                lua_newtable(L);
                lua_pushinteger(L, static_cast<lua_Integer>(message.playerId));
                lua_setfield(L, -2, "id");
                lua_pushstring(L, message.displayName.c_str());
                lua_setfield(L, -2, "name");
                lua_pushstring(L, message.text.c_str());
                lua_setfield(L, -2, "text");
                lua_pushboolean(L, message.isEmote);
                lua_setfield(L, -2, "isEmote");
                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "consumePartyChatMessages");

    // consumePartyChatErrors() -> { "Unknown command: /foo", ... }; sender-only feedback (never
    // relayed to the rest of the party) since the last call.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(
        L_,
        [](lua_State* L) -> int {
            auto* self = luaSceneSelf(L);
            const auto errors = self->session_ ? self->session_->consumeChatErrors() : std::vector<std::string>{};
            lua_newtable(L);
            for (std::size_t i = 0; i < errors.size(); ++i) {
                lua_pushstring(L, errors[i].c_str());
                lua_seti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        },
        1);
    lua_setfield(L_, gameplayTable, "consumePartyChatErrors");

    lua_setglobal(L_, "Gameplay");
}
