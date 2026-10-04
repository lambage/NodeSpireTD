#include "lambui/scenes/LobbyScene.hpp"

#include "multiplayer/MultiplayerSession.hpp"
#include "multiplayer/PlayerProfileStore.hpp"
#include "scenes/TowerLoadController.hpp"

#include <lua.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace NodeSpireUi {
namespace {
constexpr unsigned short kPartyPort = 47321;
constexpr std::size_t kMaxTowers = 5;
constexpr std::size_t kMaxChatLines = 100;

void field(lua_State* lua, const char* name, const std::string& value) {
    lua_pushlstring(lua, value.data(), value.size());
    lua_setfield(lua, -2, name);
}

void field(lua_State* lua, const char* name, bool value) {
    lua_pushboolean(lua, value);
    lua_setfield(lua, -2, name);
}

void integerField(lua_State* lua, const char* name, lua_Integer value) {
    lua_pushinteger(lua, value);
    lua_setfield(lua, -2, name);
}

std::string argument(lua_State* lua, int index) {
    if (lua_type(lua, index) != LUA_TSTRING) throw std::runtime_error("Expected text");
    std::size_t length = 0;
    const char* text = lua_tolstring(lua, index, &length);
    std::string result(text, length);
    if (result.find('\0') != std::string::npos) throw std::runtime_error("Invalid text");
    return result;
}
} // namespace

LobbyScene::LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore,
                       PlayLevelLaunchConfig& playLevelLaunchConfig, lambui_backend::VulkanUiRenderer& renderer)
    : LuaUiScene("assets/scenes/Lobby.lua", renderer), session_(session), profileStore_(profileStore),
      playLevelLaunchConfig_(playLevelLaunchConfig) {}

void LobbyScene::loadCatalogs(lua_State* lua) {
    levels_.clear();
    towers_.clear();
    chat_.clear();
    status_.clear();
    selectedLevel_ = 0;
    try {
        std::ifstream input("assets/levels/catalog.json");
        if (!input) throw std::runtime_error("Unable to open the level catalog");
        const auto catalog = nlohmann::json::parse(input);
        for (const auto& item : catalog.at("levels")) {
            LevelEntry entry;
            entry.launch.levelId = item.at("id").get<std::string>();
            entry.launch.displayName = item.at("name").get<std::string>();
            entry.launch.mapAssetPath = item.at("mapAsset").get<std::string>();
            entry.launch.startModelPath = item.value("startModel", entry.launch.startModelPath);
            entry.launch.endModelPath = item.value("endModel", entry.launch.endModelPath);
            entry.launch.animatedTemplateModelPaths = item.value("animatedTemplateModels", entry.launch.animatedTemplateModelPaths);
            entry.description = item.value("description", std::string{});
            entry.threat = item.value("threat", std::string{"NORMAL"});
            entry.waves = item.value("waves", std::string{"0"});
            entry.players = item.value("players", std::string{"1-4"});
            const auto thumbnail = item.value("thumbnail", std::string{});
            if (!thumbnail.empty()) entry.thumbnail = thumbnail;
            if (entry.launch.levelId.empty() || !std::filesystem::is_regular_file(entry.launch.mapAssetPath)) continue;
            levels_.push_back(std::move(entry));
        }
        const auto& profile = profileStore_.profile();
        const auto preferred = playLevelLaunchConfig_.towerLoadoutConfigured ? playLevelLaunchConfig_.levelId :
            profile.lastPlayedLevelId.empty() ? catalog.value("defaultLevel", std::string{}) : profile.lastPlayedLevelId;
        for (std::size_t index = 0; index < levels_.size(); ++index) {
            if (levels_[index].launch.levelId == preferred) selectedLevel_ = index;
        }
    } catch (const std::exception& error) {
        levels_.clear();
        status_ = std::string("Unable to load levels: ") + error.what();
    }
    TowerLoadController catalog(lua);
    catalog.discoverTowerArchetypesInDirectory("assets/models/towers");
    for (const auto& [id, tower] : catalog.archetypes())
        towers_.push_back({id, tower.displayName, tower.previewImagePath, tower.bio, tower.cost});
    std::sort(towers_.begin(), towers_.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    if (playLevelLaunchConfig_.towerLoadoutConfigured) {
        catalog.setLoadoutIds(playLevelLaunchConfig_.towerLoadoutIds);
        playLevelLaunchConfig_.towerLoadoutIds = catalog.loadoutIds();
    } else {
        playLevelLaunchConfig_.towerLoadoutIds.clear();
        for (std::size_t index = 0; index < std::min(towers_.size(), kMaxTowers); ++index)
            playLevelLaunchConfig_.towerLoadoutIds.push_back(towers_[index].id);
    }
    if (playLevelLaunchConfig_.towerLoadoutIds.size() > kMaxTowers)
        playLevelLaunchConfig_.towerLoadoutIds.resize(kMaxTowers);
    playLevelLaunchConfig_.towerLoadoutConfigured = true;
    if (!levels_.empty()) configureLaunch(selectedLevel_);
}

void LobbyScene::configureLaunch(std::size_t index) {
    auto loadout = std::move(playLevelLaunchConfig_.towerLoadoutIds);
    playLevelLaunchConfig_ = levels_.at(index).launch;
    playLevelLaunchConfig_.towerLoadoutIds = std::move(loadout);
    playLevelLaunchConfig_.towerLoadoutConfigured = true;
    selectedLevel_ = index;
}

bool LobbyScene::configureActiveMatch() {
    if (!session_.activeMatch()) return false;
    const auto& announcement = *session_.activeMatch();
    for (std::size_t index = 0; index < levels_.size(); ++index) {
        if (levels_[index].launch.levelId == announcement.levelId) {
            configureLaunch(index);
            return true;
        }
    }
    status_ = "The host selected a level that is not in the local catalog.";
    return false;
}

bool LobbyScene::canStart() const {
    if (levels_.empty() || playLevelLaunchConfig_.towerLoadoutIds.empty()) return false;
    if (session_.activeMatch()) {
        return std::any_of(levels_.begin(), levels_.end(), [&](const auto& entry) {
            return entry.launch.levelId == session_.activeMatch()->levelId;
        });
    }
    if (session_.isClient()) return false;
    if (!session_.isInParty()) return true;
    const auto roster = session_.roster();
    return !roster.members.empty() && std::all_of(roster.members.begin(), roster.members.end(),
        [](const auto& member) { return member.ready; });
}

int LobbyScene::pushState(lua_State* lua) const {
    lua_newtable(lua);
    field(lua, "role", std::string(session_.isHost() ? "Host" : session_.isClient() ? "Client" : "Solo"));
    field(lua, "displayName", profileStore_.profile().displayName);
    field(lua, "canStart", canStart());
    field(lua, "activeMatch", session_.activeMatch().has_value());
    integerField(lua, "selectedLevel", static_cast<lua_Integer>(selectedLevel_ + 1));
    integerField(lua, "maxTowers", kMaxTowers);
    const auto roster = session_.roster();
    integerField(lua, "capacity", roster.capacity);
    bool ready = false;
    lua_newtable(lua);
    for (std::size_t index = 0; index < roster.members.size(); ++index) {
        const auto& member = roster.members[index];
        const bool local = member.playerId == session_.localPlayerId();
        if (local) ready = member.ready;
        lua_newtable(lua);
        integerField(lua, "id", member.playerId);
        field(lua, "name", member.displayName);
        field(lua, "host", member.isHost);
        field(lua, "ready", member.ready);
        field(lua, "localPlayer", local);
        lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(lua, -2, "members");
    field(lua, "ready", ready);
    std::string status = status_;
    if (status.empty()) {
        if (levels_.empty()) status = "No playable levels found.";
        else if (playLevelLaunchConfig_.towerLoadoutIds.empty()) status = "Select at least one tower.";
        else if (session_.isClient()) status = roster.members.empty() ? "Connecting to host..." : "Waiting for the party leader.";
        else if (session_.isHost()) status = canStart() ? "Party ready. Port 47321." : "Waiting for all players to ready up. Port 47321.";
    }
    field(lua, "status", status);
    lua_newtable(lua);
    for (std::size_t index = 0; index < levels_.size(); ++index) {
        const auto& entry = levels_[index];
        lua_newtable(lua);
        field(lua, "id", entry.launch.levelId);
        field(lua, "name", entry.launch.displayName);
        field(lua, "description", entry.description);
        field(lua, "threat", entry.threat);
        field(lua, "waves", entry.waves);
        field(lua, "players", entry.players);
        field(lua, "thumbnail", entry.thumbnail);
        lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(lua, -2, "levels");
    lua_newtable(lua);
    const auto& loadout = playLevelLaunchConfig_.towerLoadoutIds;
    for (std::size_t index = 0; index < towers_.size(); ++index) {
        const auto& tower = towers_[index];
        lua_newtable(lua);
        field(lua, "id", tower.id);
        field(lua, "name", tower.name);
        field(lua, "portrait", tower.portrait);
        field(lua, "bio", tower.bio);
        integerField(lua, "cost", tower.cost);
        field(lua, "selected", std::find(loadout.begin(), loadout.end(), tower.id) != loadout.end());
        lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(lua, -2, "towers");
    lua_newtable(lua);
    for (std::size_t index = 0; index < loadout.size(); ++index) {
        lua_pushlstring(lua, loadout[index].data(), loadout[index].size());
        lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(lua, -2, "loadout");
    lua_newtable(lua);
    for (std::size_t index = 0; index < chat_.size(); ++index) {
        lua_pushlstring(lua, chat_[index].data(), chat_[index].size());
        lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(lua, -2, "chat");
    return 1;
}

void LobbyScene::bindSceneApi(lua_State* lua, AudioEngine&) {
    loadCatalogs(lua);
    lua_newtable(lua);
    for (const char* name : {"SelectLevel", "ToggleTower", "Host", "Join", "Leave", "Ready", "Kick", "SendChat", "Start"}) {
        lua_pushlightuserdata(lua, this);
        lua_pushstring(lua, name);
        lua_pushcclosure(lua, dispatch, 2);
        lua_setfield(lua, -2, name);
    }
    lua_setglobal(lua, "Lobby");
}

void LobbyScene::pushOnUpdateState(lua_State* lua) {
    pushState(lua);
}

int LobbyScene::dispatch(lua_State* lua) {
    auto& self = *static_cast<LobbyScene*>(lua_touserdata(lua, lua_upvalueindex(1)));
    const std::string_view operation = lua_tostring(lua, lua_upvalueindex(2));
    bool success = true;
    std::string message;
    try {
        auto& session = self.session_;
        auto& launch = self.playLevelLaunchConfig_;
        self.status_.clear();
        if (operation == "SelectLevel") {
            if (session.isClient() || session.activeMatch()) throw std::runtime_error("Only the leader can select a new mission.");
            const auto index = lua_tointeger(lua, 1);
            if (!lua_isinteger(lua, 1) || index < 1 || static_cast<std::size_t>(index) > self.levels_.size())
                throw std::runtime_error("Invalid level selection.");
            self.configureLaunch(static_cast<std::size_t>(index - 1));
        } else if (operation == "ToggleTower") {
            const auto id = argument(lua, 1);
            if (std::none_of(self.towers_.begin(), self.towers_.end(), [&](const auto& tower) { return tower.id == id; }))
                throw std::runtime_error("Unknown tower.");
            auto& loadout = launch.towerLoadoutIds;
            const auto selected = std::find(loadout.begin(), loadout.end(), id);
            if (selected != loadout.end()) loadout.erase(selected);
            else if (loadout.size() < kMaxTowers) loadout.push_back(id);
            else throw std::runtime_error("Your loadout already has five towers.");
        } else if (operation == "Host" || operation == "Join") {
            if (session.isInParty()) throw std::runtime_error("Leave the current party first.");
            const auto name = argument(lua, 1);
            const auto address = operation == "Join" ? argument(lua, 2) : std::string{};
            if (operation == "Join" && address.find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::runtime_error("Enter a host address.");
            if (!self.profileStore_.setDisplayName(name)) throw std::runtime_error("Unable to save callsign. Use 1-32 characters.");
            const auto& profile = self.profileStore_.profile();
            success = operation == "Host" ? session.hostParty(kPartyPort, profile.displayName, profile.playerUuid) :
                session.joinParty(address, kPartyPort, profile.displayName, profile.playerUuid);
            if (!success) message = operation == "Host" ? "Could not create party. Port 47321 may be in use." : "Could not connect to host on port 47321.";
            self.chat_.clear();
        } else if (operation == "Leave") {
            session.leaveParty();
            self.chat_.clear();
        } else if (operation == "Ready") {
            if (!lua_isboolean(lua, 1) || !session.isInParty()) throw std::runtime_error("Join a party before readying up.");
            success = session.setLocalReady(lua_toboolean(lua, 1) != 0);
        } else if (operation == "Kick") {
            if (!lua_isinteger(lua, 1) || lua_tointeger(lua, 1) <= 0) throw std::runtime_error("Invalid party member.");
            success = session.kickMember(static_cast<multiplayer::PlayerId>(lua_tointeger(lua, 1)));
        } else if (operation == "SendChat") {
            if (!session.isInParty()) throw std::runtime_error("Join a party before sending messages.");
            success = session.sendChatMessage(argument(lua, 1));
            for (const auto& error : session.consumeChatErrors()) message = error;
        } else if (operation == "Start") {
            if (!self.canStart()) throw std::runtime_error("Select a level and towers, and wait for every player to be ready.");
            if (session.activeMatch()) {
                if (!self.configureActiveMatch()) throw std::runtime_error(self.status_);
            } else if (session.isHost() && !session.announceMatchStart(launch.displayName, launch.levelId, launch.mapAssetPath)) {
                throw std::runtime_error("Unable to announce the match.");
            }
            self.profileStore_.setLastPlayedLevelId(launch.levelId);
            self.requestTransition(SceneId::PlayLevel);
        }
        if (!success && message.empty()) message = "Action unavailable.";
    } catch (const std::exception& error) {
        success = false;
        message = error.what();
    }
    lua_pushboolean(lua, success);
    lua_pushlstring(lua, message.data(), message.size());
    return 2;
}

void LobbyScene::onUpdateScene(float) {
    if (const auto notice = session_.consumeConnectionNotice()) {
        status_ = *notice;
        chat_.clear();
    }
    for (const auto& message : session_.consumeChatMessages())
        chat_.push_back(message.isEmote ? message.text : message.displayName + ": " + message.text);
    for (const auto& error : session_.consumeChatErrors()) status_ = error;
    if (chat_.size() > kMaxChatLines) chat_.erase(chat_.begin(), chat_.end() - kMaxChatLines);
    if (session_.isClient() && session_.consumeMatchStartAnnouncement() && configureActiveMatch()) {
        profileStore_.setLastPlayedLevelId(playLevelLaunchConfig_.levelId);
        requestTransition(SceneId::PlayLevel);
    }
}

} // namespace NodeSpireUi
