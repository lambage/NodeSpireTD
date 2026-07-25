#include "EnemyLoadController.hpp"

#include "lua.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>
#include <utility>

EnemyLoadController::EnemyLoadController(lua_State* luaState) : L_(luaState) {}

EnemyLoadController::~EnemyLoadController() = default;

void EnemyLoadController::reset() {
    archetypes_.clear();
    defaultId_.clear();
}

bool EnemyLoadController::parseEnemyArchetypeScript(const std::string& scriptPath, EnemyArchetype& outArchetype) const {
    if (!L_) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("EnemyLoadController: failed to load enemy archetype {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("EnemyLoadController: enemy archetype script error {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (!lua_istable(L_, -1)) {
        spdlog::error("EnemyLoadController: enemy archetype script must return a table: {}", scriptPath);
        lua_pop(L_, 1);
        return false;
    }

    auto readStringField = [&](const char* key, std::string& out) {
        lua_getfield(L_, -1, key);
        if (lua_isstring(L_, -1)) {
            out = lua_tostring(L_, -1);
        }
        lua_pop(L_, 1);
    };

    readStringField("id", outArchetype.id);
    readStringField("displayName", outArchetype.displayName);
    readStringField("model", outArchetype.modelPath);

    lua_getfield(L_, -1, "stats");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "health");
        if (lua_isinteger(L_, -1)) {
            outArchetype.health = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "moveSpeed");
        if (lua_isnumber(L_, -1)) {
            outArchetype.moveSpeed = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "rewardMoney");
        if (lua_isinteger(L_, -1)) {
            outArchetype.rewardMoney = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "baseDamage");
        if (lua_isinteger(L_, -1)) {
            outArchetype.baseDamage = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "render");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "renderScale");
        if (lua_isnumber(L_, -1)) {
            outArchetype.renderScale = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "facingYawOffsetDegrees");
        if (lua_isnumber(L_, -1)) {
            outArchetype.facingYawOffsetDegrees = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_pop(L_, 1); // archetype root table

    if (outArchetype.id.empty()) {
        outArchetype.id = std::filesystem::path(scriptPath).stem().string();
    }

    if (outArchetype.health <= 0) {
        outArchetype.health = 1;
    }
    if (outArchetype.moveSpeed <= 0.0f) {
        outArchetype.moveSpeed = 0.1f;
    }
    if (outArchetype.spawnIntervalSeconds <= 0.05f) {
        outArchetype.spawnIntervalSeconds = 0.05f;
    }
    if (outArchetype.defeatIntervalSeconds <= 0.05f) {
        outArchetype.defeatIntervalSeconds = 0.05f;
    }
    if (outArchetype.baseDamage <= 0) {
        outArchetype.baseDamage = 1;
    }
    if (outArchetype.renderScale <= 0.01f) {
        outArchetype.renderScale = 1.0f;
    }

    return true;
}

bool EnemyLoadController::loadEnemyArchetype(const std::string& scriptPath) {
    EnemyArchetype archetype;
    if (!parseEnemyArchetypeScript(scriptPath, archetype)) {
        return false;
    }

    registerArchetype(std::move(archetype));
    return true;
}

void EnemyLoadController::registerArchetype(EnemyArchetype archetype) {
    if (defaultId_.empty() || archetypes_.empty()) {
        defaultId_ = archetype.id;
    }
    archetypes_[archetype.id] = std::move(archetype);
}

const EnemyArchetype* EnemyLoadController::findArchetype(const std::string& enemyId) const {
    auto it = archetypes_.find(enemyId);
    if (it != archetypes_.end()) {
        return &it->second;
    }

    auto fallbackIt = archetypes_.find(defaultId_);
    if (fallbackIt != archetypes_.end()) {
        return &fallbackIt->second;
    }

    return nullptr;
}
