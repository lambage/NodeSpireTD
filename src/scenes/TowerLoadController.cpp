#include "TowerLoadController.hpp"

#include "lua.hpp"
#include "utility/WorldAssetLoader.hpp"

#include <spdlog/spdlog.h>
#include <utility>

TowerLoadController::TowerLoadController(lua_State* luaState) : L_(luaState) {}

TowerLoadController::~TowerLoadController() = default;

void TowerLoadController::reset() {
    archetypes_.clear();
    loadoutIds_.clear();
    poolGroupsById_.clear();
    ghostGroupById_.clear();
    templatePrototypeById_.clear();
    projectileTemplatePrototypeById_.clear();
}

std::string TowerLoadController::makeIconTextureId(const std::string& towerId) {
    return "tower_icon:" + towerId;
}

void TowerLoadController::discoverTowerArchetypesInDirectory(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_directory()) {
            spdlog::debug("iterating next directory {}", entry.path().string());
            discoverTowerArchetypesInDirectory(entry.path());
        } else if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() != ".lua") {
            continue;
        }
        spdlog::info("Discovering tower archetype: {}", entry.path().string());
        loadTowerArchetype(entry.path().string());
    }
}

bool TowerLoadController::parseTowerArchetypeScript(const std::string& scriptPath, TowerArchetype& outArchetype) {
    if (!L_) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.c_str()) != LUA_OK) {
        spdlog::error("TowerLoadController: failed to load tower archetype {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("TowerLoadController: tower archetype script error {}: {}", scriptPath, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (!lua_istable(L_, -1)) {
        spdlog::error("TowerLoadController: tower archetype script must return a table: {}", scriptPath);
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
    readStringField("projectileModel", outArchetype.projectileModelPath);
    readStringField("previewImage", outArchetype.previewImagePath);

    lua_getfield(L_, -1, "stats");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "cost");
        if (lua_isinteger(L_, -1)) {
            outArchetype.cost = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "attackRange");
        if (lua_isnumber(L_, -1)) {
            outArchetype.attackRange = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "attackDamage");
        if (lua_isnumber(L_, -1)) {
            outArchetype.attackDamage = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "attackSpeed");
        if (lua_isnumber(L_, -1)) {
            outArchetype.attackSpeed = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "projectileSpeed");
        if (lua_isnumber(L_, -1)) {
            outArchetype.projectileSpeed = static_cast<float>(lua_tonumber(L_, -1));
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

    lua_pop(L_, 1);

    if (outArchetype.id.empty()) {
        outArchetype.id = std::filesystem::path(scriptPath).stem().string();
    }
    if (outArchetype.displayName.empty()) {
        outArchetype.displayName = outArchetype.id;
    }
    if (outArchetype.cost <= 0) {
        outArchetype.cost = 1;
    }
    if (outArchetype.attackRange <= 0.1f) {
        outArchetype.attackRange = 1.0f;
    }
    if (outArchetype.attackDamage <= 0.01f) {
        outArchetype.attackDamage = 1.0f;
    }
    if (outArchetype.attackSpeed <= 0.01f) {
        outArchetype.attackSpeed = 1.0f;
    }
    if (outArchetype.projectileSpeed <= 0.1f) {
        outArchetype.projectileSpeed = 16.0f;
    }
    if (outArchetype.renderScale <= 0.01f) {
        outArchetype.renderScale = 1.0f;
    }

    return true;
}

bool TowerLoadController::loadTowerArchetype(const std::string& scriptPath) {
    TowerArchetype archetype;
    if (!parseTowerArchetypeScript(scriptPath, archetype)) {
        return false;
    }

    if (archetype.previewImagePath.empty() && !archetype.modelPath.empty()) {
        std::filesystem::path candidate = std::filesystem::path(archetype.modelPath).replace_extension(".png");
        if (std::filesystem::exists(candidate)) {
            archetype.previewImagePath = candidate.string();
        }
    }

    archetypes_[archetype.id] = archetype;
    if (loadoutIds_.size() < 5) {
        loadoutIds_.push_back(archetype.id);
    }
    return true;
}

void TowerLoadController::populateWorldAssets(WorldAssetSpec& spec) {
    for (const auto& [towerId, tower] : archetypes_) {
        if (tower.modelPath.empty()) {
            continue;
        }

        if (!tower.previewImagePath.empty()) {
            WorldUiTextureSpec iconTex;
            iconTex.id = makeIconTextureId(towerId);
            iconTex.texturePath = tower.previewImagePath;
            spec.uiTextures.push_back(std::move(iconTex));
        }

        const std::string ghostGroup = "tower_pool_ghost:" + towerId;
        ghostGroupById_[towerId] = ghostGroup;

        auto& poolGroups = poolGroupsById_[towerId];
        poolGroups.reserve(kPoolPlacementsPerType);
        for (int i = 0; i < kPoolPlacementsPerType; ++i) {
            const std::string poolGroup = "tower_pool:" + towerId + ":" + std::to_string(i);
            poolGroups.push_back(poolGroup);
        }

        WorldTemplateModelSpec towerTemplate;
        towerTemplate.id = towerId;
        towerTemplate.modelPath = tower.modelPath;
        templatePrototypeById_[towerId] = static_cast<int>(spec.towerTemplateModels.size());
        spec.towerTemplateModels.push_back(std::move(towerTemplate));

        if (!tower.projectileModelPath.empty()) {
            WorldTemplateModelSpec projectileTemplate;
            projectileTemplate.id = "projectile:" + towerId;
            projectileTemplate.modelPath = tower.projectileModelPath;
            projectileTemplatePrototypeById_[towerId] = static_cast<int>(spec.towerTemplateModels.size());
            spec.towerTemplateModels.push_back(std::move(projectileTemplate));
        }
    }
}

const TowerArchetype* TowerLoadController::findArchetype(const std::string& towerId) const {
    auto it = archetypes_.find(towerId);
    if (it == archetypes_.end()) {
        return nullptr;
    }
    return &it->second;
}

const TowerArchetype* TowerLoadController::archetypeAtLoadoutSlot(int slot) const {
    if (slot < 0 || slot >= static_cast<int>(loadoutIds_.size())) {
        return nullptr;
    }
    return findArchetype(loadoutIds_[static_cast<std::size_t>(slot)]);
}

int TowerLoadController::templatePrototypeIndex(const std::string& towerId) const {
    auto it = templatePrototypeById_.find(towerId);
    return it != templatePrototypeById_.end() ? it->second : -1;
}

int TowerLoadController::projectileTemplatePrototypeIndex(const std::string& towerId) const {
    auto it = projectileTemplatePrototypeById_.find(towerId);
    return it != projectileTemplatePrototypeById_.end() ? it->second : -1;
}
