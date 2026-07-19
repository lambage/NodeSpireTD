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

    scriptRef_ = loadLuaScript(state, "assets/scenes/Lobby.lua");
    registerLuaGameplayApi();
    luaOnEnter(scriptRef_);
}

void LobbyScene::onExit(SceneSharedState& state) {
    luaOnExit(state, scriptRef_);
}

void LobbyScene::render(SceneSharedState& state, float dt) {
    if (!availableLevels_.empty()) {
        selectedLevelIndex_ = std::clamp(selectedLevelIndex_, 0, static_cast<int>(availableLevels_.size() - 1));
        state.activeLevelName = availableLevels_[selectedLevelIndex_].name;
        state.activeLevelAssetPath = availableLevels_[selectedLevelIndex_].assetPath.string();
        state.activeLevelScriptPath = availableLevels_[selectedLevelIndex_].scriptPath.string();
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

    lua_setglobal(L_, "Gameplay");
}
