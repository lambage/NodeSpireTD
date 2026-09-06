#include "TowerLoadController.hpp"

#include "lua.hpp"
#include "utility/WorldAssetLoader.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_set>
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
    readStringField("bio", outArchetype.bio);

    lua_getfield(L_, -1, "stats");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "cost");
        if (lua_isinteger(L_, -1)) {
            outArchetype.cost = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "damageType");
        if (lua_isstring(L_, -1)) {
            const std::string damageTypeRaw = lua_tostring(L_, -1);
            playlevel::DamageType parsedType{};
            if (playlevel::tryParseDamageType(damageTypeRaw, parsedType)) {
                outArchetype.damageType = parsedType;
            } else {
                spdlog::warn("TowerLoadController: invalid damageType '{}' in {}. Falling back to 'physical'.", damageTypeRaw,
                             scriptPath);
                outArchetype.damageType = playlevel::DamageType::Physical;
            }
        }
        lua_pop(L_, 1);

        auto readTargetModeField = [&](const char* key) {
            lua_getfield(L_, -1, key);
            if (lua_isstring(L_, -1)) {
                playlevel::TowerTargetingMode parsedMode{};
                if (playlevel::tryParseTowerTargetingMode(lua_tostring(L_, -1), parsedMode)) {
                    outArchetype.defaultTargetingMode = parsedMode;
                } else {
                    spdlog::warn("TowerLoadController: invalid targeting mode '{}' in {}. Falling back to 'nearest'.",
                                 lua_tostring(L_, -1), scriptPath);
                }
            }
            lua_pop(L_, 1);
        };
        readTargetModeField("targetMode");
        readTargetModeField("targetingMode");

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

        lua_getfield(L_, -1, "armorPiercing");
        if (lua_isnumber(L_, -1)) {
            outArchetype.armorPiercing = static_cast<float>(lua_tonumber(L_, -1));
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

        lua_getfield(L_, -1, "splashRadius");
        if (lua_isnumber(L_, -1)) {
            outArchetype.splashRadius = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "chainRange");
        if (lua_isnumber(L_, -1)) {
            outArchetype.chainRange = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "ricochetRange");
        if (lua_isnumber(L_, -1)) {
            outArchetype.ricochetRange = static_cast<float>(lua_tonumber(L_, -1));
        }
        lua_pop(L_, 1);

        auto readEffectStat = [&](const char* key, float& outValue) {
            lua_getfield(L_, -1, key);
            if (lua_isnumber(L_, -1)) {
                outValue = static_cast<float>(lua_tonumber(L_, -1));
            }
            lua_pop(L_, 1);
        };
        readEffectStat("burnDamagePerSecond", outArchetype.burnDamagePerSecond);
        readEffectStat("burnDuration", outArchetype.burnDuration);
        readEffectStat("slowAmount", outArchetype.slowAmount);
        readEffectStat("slowDuration", outArchetype.slowDuration);
        readEffectStat("freezeChance", outArchetype.freezeChance);
        readEffectStat("freezeDuration", outArchetype.freezeDuration);
        readEffectStat("critChance", outArchetype.critChance);
        readEffectStat("critDamageMul", outArchetype.critDamageMul);

        lua_getfield(L_, -1, "projectileCount");
        if (lua_isinteger(L_, -1)) {
            outArchetype.projectileCount = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "chainTargetCount");
        if (lua_isinteger(L_, -1)) {
            outArchetype.chainTargetCount = static_cast<int>(lua_tointeger(L_, -1));
        }
        lua_pop(L_, 1);

        lua_getfield(L_, -1, "ricochetCount");
        if (lua_isinteger(L_, -1)) {
            outArchetype.ricochetCount = static_cast<int>(lua_tointeger(L_, -1));
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

    lua_getfield(L_, -1, "upgradeTree");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "nodes");
        if (lua_istable(L_, -1)) {
            const int nodeCount = static_cast<int>(lua_rawlen(L_, -1));
            outArchetype.upgradeNodes.clear();
            outArchetype.upgradeNodes.reserve(static_cast<std::size_t>(nodeCount));

            for (int i = 1; i <= nodeCount; ++i) {
                lua_geti(L_, -1, i);
                if (!lua_istable(L_, -1)) {
                    lua_pop(L_, 1);
                    continue;
                }

                TowerArchetype::UpgradeNode node;

                lua_getfield(L_, -1, "id");
                if (lua_isstring(L_, -1)) {
                    node.id = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "displayName");
                if (lua_isstring(L_, -1)) {
                    node.displayName = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "description");
                if (lua_isstring(L_, -1)) {
                    node.description = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "towerModel");
                if (lua_isstring(L_, -1)) {
                    node.towerModelPathOverride = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "projectileModel");
                if (lua_isstring(L_, -1)) {
                    node.projectileModelPathOverride = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "minUpgradesRequired");
                if (lua_isinteger(L_, -1)) {
                    node.minUpgradesRequired = static_cast<int>(lua_tointeger(L_, -1));
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "requires");
                if (lua_istable(L_, -1)) {
                    const int count = static_cast<int>(lua_rawlen(L_, -1));
                    node.requiredNodeIds.reserve(static_cast<std::size_t>(count));
                    for (int idx = 1; idx <= count; ++idx) {
                        lua_geti(L_, -1, idx);
                        if (lua_isstring(L_, -1)) {
                            node.requiredNodeIds.emplace_back(lua_tostring(L_, -1));
                        }
                        lua_pop(L_, 1);
                    }
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "excludes");
                if (lua_istable(L_, -1)) {
                    const int count = static_cast<int>(lua_rawlen(L_, -1));
                    node.excludes.reserve(static_cast<std::size_t>(count));
                    for (int idx = 1; idx <= count; ++idx) {
                        lua_geti(L_, -1, idx);
                        if (lua_isstring(L_, -1)) {
                            node.excludes.emplace_back(lua_tostring(L_, -1));
                        }
                        lua_pop(L_, 1);
                    }
                }
                lua_pop(L_, 1);

                auto readUpgradeEffects = [&](TowerArchetype::UpgradeEffects& outEffects) {
                    auto readEffect = [&](const char* key, float& outValue) {
                        lua_getfield(L_, -1, key);
                        if (lua_isnumber(L_, -1)) {
                            outValue = static_cast<float>(lua_tonumber(L_, -1));
                        }
                        lua_pop(L_, 1);
                    };

                    readEffect("attackDamageAdd", outEffects.attackDamageAdd);
                    readEffect("attackDamageMul", outEffects.attackDamageMul);
                    readEffect("attackRangeAdd", outEffects.attackRangeAdd);
                    readEffect("attackRangeMul", outEffects.attackRangeMul);
                    readEffect("attackSpeedAdd", outEffects.attackSpeedAdd);
                    readEffect("attackSpeedMul", outEffects.attackSpeedMul);
                    readEffect("projectileSpeedAdd", outEffects.projectileSpeedAdd);
                    readEffect("projectileSpeedMul", outEffects.projectileSpeedMul);
                    readEffect("splashRadiusAdd", outEffects.splashRadiusAdd);
                    readEffect("splashRadiusMul", outEffects.splashRadiusMul);
                    readEffect("chainRangeAdd", outEffects.chainRangeAdd);
                    readEffect("chainRangeMul", outEffects.chainRangeMul);
                    readEffect("ricochetRangeAdd", outEffects.ricochetRangeAdd);
                    readEffect("ricochetRangeMul", outEffects.ricochetRangeMul);
                    readEffect("burnDamagePerSecondAdd", outEffects.burnDamagePerSecondAdd);
                    readEffect("burnDurationAdd", outEffects.burnDurationAdd);
                    readEffect("slowAmountAdd", outEffects.slowAmountAdd);
                    readEffect("slowDurationAdd", outEffects.slowDurationAdd);
                    readEffect("freezeChanceAdd", outEffects.freezeChanceAdd);
                    readEffect("freezeDurationAdd", outEffects.freezeDurationAdd);
                    readEffect("critChanceAdd", outEffects.critChanceAdd);
                    readEffect("critDamageMulAdd", outEffects.critDamageMulAdd);

                    lua_getfield(L_, -1, "projectileCountAdd");
                    if (lua_isinteger(L_, -1)) {
                        outEffects.projectileCountAdd = static_cast<int>(lua_tointeger(L_, -1));
                    }
                    lua_pop(L_, 1);

                    lua_getfield(L_, -1, "chainTargetCountAdd");
                    if (lua_isinteger(L_, -1)) {
                        outEffects.chainTargetCountAdd = static_cast<int>(lua_tointeger(L_, -1));
                    }
                    lua_pop(L_, 1);

                    lua_getfield(L_, -1, "ricochetCountAdd");
                    if (lua_isinteger(L_, -1)) {
                        outEffects.ricochetCountAdd = static_cast<int>(lua_tointeger(L_, -1));
                    }
                    lua_pop(L_, 1);
                };

                auto sanitizeUpgradeEffects = [](TowerArchetype::UpgradeEffects& effects) {
                    if (effects.attackDamageMul <= 0.0f) {
                        effects.attackDamageMul = 1.0f;
                    }
                    if (effects.attackRangeMul <= 0.0f) {
                        effects.attackRangeMul = 1.0f;
                    }
                    if (effects.attackSpeedMul <= 0.0f) {
                        effects.attackSpeedMul = 1.0f;
                    }
                    if (effects.projectileSpeedMul <= 0.0f) {
                        effects.projectileSpeedMul = 1.0f;
                    }
                    if (effects.splashRadiusMul <= 0.0f) {
                        effects.splashRadiusMul = 1.0f;
                    }
                    if (effects.chainRangeMul <= 0.0f) {
                        effects.chainRangeMul = 1.0f;
                    }
                    if (effects.ricochetRangeMul <= 0.0f) {
                        effects.ricochetRangeMul = 1.0f;
                    }
                };

                lua_getfield(L_, -1, "upgradeLevels");
                if (!lua_istable(L_, -1)) {
                    lua_pop(L_, 1);
                    lua_getfield(L_, -1, "levels");
                }
                if (lua_istable(L_, -1)) {
                    const int levelCount = static_cast<int>(lua_rawlen(L_, -1));
                    node.upgradeLevels.reserve(static_cast<std::size_t>(levelCount));
                    for (int levelIdx = 1; levelIdx <= levelCount; ++levelIdx) {
                        lua_geti(L_, -1, levelIdx);
                        if (!lua_istable(L_, -1)) {
                            lua_pop(L_, 1);
                            continue;
                        }

                        TowerArchetype::UpgradeNode::UpgradeLevel level;

                        lua_getfield(L_, -1, "cost");
                        if (lua_isinteger(L_, -1)) {
                            level.cost = static_cast<int>(lua_tointeger(L_, -1));
                        }
                        lua_pop(L_, 1);

                        lua_getfield(L_, -1, "effects");
                        const bool hasNestedEffects = lua_istable(L_, -1);
                        if (hasNestedEffects) {
                            readUpgradeEffects(level.effects);
                        }
                        lua_pop(L_, 1);
                        if (!hasNestedEffects) {
                            readUpgradeEffects(level.effects);
                        }

                        if (level.cost < 0) {
                            level.cost = 0;
                        }
                        sanitizeUpgradeEffects(level.effects);
                        node.upgradeLevels.push_back(std::move(level));
                        lua_pop(L_, 1);
                    }
                }
                lua_pop(L_, 1);

                if (node.upgradeLevels.empty()) {
                    TowerArchetype::UpgradeNode::UpgradeLevel legacyLevel;

                    lua_getfield(L_, -1, "cost");
                    if (lua_isinteger(L_, -1)) {
                        legacyLevel.cost = static_cast<int>(lua_tointeger(L_, -1));
                    }
                    lua_pop(L_, 1);

                    lua_getfield(L_, -1, "effects");
                    if (lua_istable(L_, -1)) {
                        readUpgradeEffects(legacyLevel.effects);
                    }
                    lua_pop(L_, 1);

                    if (legacyLevel.cost < 0) {
                        legacyLevel.cost = 0;
                    }
                    sanitizeUpgradeEffects(legacyLevel.effects);
                    node.upgradeLevels.push_back(std::move(legacyLevel));
                }

                if (node.id.empty()) {
                    lua_pop(L_, 1);
                    continue;
                }
                if (node.displayName.empty()) {
                    node.displayName = node.id;
                }
                if (node.minUpgradesRequired < 0) {
                    node.minUpgradesRequired = 0;
                }
                if (node.upgradeLevels.empty()) {
                    node.upgradeLevels.emplace_back();
                }

                outArchetype.upgradeNodes.push_back(std::move(node));
                lua_pop(L_, 1);
            }
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
    if (outArchetype.armorPiercing < 0.0f) {
        outArchetype.armorPiercing = 0.0f;
    }
    if (outArchetype.attackSpeed <= 0.01f) {
        outArchetype.attackSpeed = 1.0f;
    }
    if (outArchetype.projectileSpeed <= 0.1f) {
        outArchetype.projectileSpeed = 16.0f;
    }
    if (outArchetype.splashRadius < 0.0f) {
        outArchetype.splashRadius = 0.0f;
    }
    if (outArchetype.chainRange <= 0.1f) {
        outArchetype.chainRange = 3.5f;
    }
    if (outArchetype.ricochetRange <= 0.1f) {
        outArchetype.ricochetRange = 3.5f;
    }
    outArchetype.burnDamagePerSecond = std::max(0.0f, outArchetype.burnDamagePerSecond);
    outArchetype.burnDuration = std::max(0.0f, outArchetype.burnDuration);
    outArchetype.slowAmount = std::clamp(outArchetype.slowAmount, 0.0f, 1.0f);
    outArchetype.slowDuration = std::max(0.0f, outArchetype.slowDuration);
    outArchetype.freezeChance = std::clamp(outArchetype.freezeChance, 0.0f, 1.0f);
    outArchetype.freezeDuration = std::max(0.0f, outArchetype.freezeDuration);
    outArchetype.critChance = std::clamp(outArchetype.critChance, 0.0f, 1.0f);
    outArchetype.critDamageMul = std::max(1.0f, outArchetype.critDamageMul);
    if (outArchetype.projectileCount < 1) {
        outArchetype.projectileCount = 1;
    }
    if (outArchetype.chainTargetCount < 1) {
        outArchetype.chainTargetCount = 1;
    }
    if (outArchetype.ricochetCount < 0) {
        outArchetype.ricochetCount = 0;
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

void TowerLoadController::setLoadoutIds(const std::vector<std::string>& towerIds) {
    std::vector<std::string> selected;
    std::unordered_set<std::string> seen;
    selected.reserve(std::min<std::size_t>(towerIds.size(), 5));
    for (const std::string& towerId : towerIds) {
        if (selected.size() == 5) {
            break;
        }
        if (archetypes_.contains(towerId) && seen.insert(towerId).second) {
            selected.push_back(towerId);
        }
    }
    loadoutIds_ = std::move(selected);
}

void TowerLoadController::populateWorldAssets(WorldAssetSpec& spec) {
    std::unordered_map<std::string, int> towerTemplateByPath;
    std::unordered_map<std::string, int> projectileTemplateByPath;

    auto ensureTowerTemplate = [&](const std::string& templateId, const std::string& modelPath) {
        if (modelPath.empty()) {
            return -1;
        }
        auto it = towerTemplateByPath.find(modelPath);
        if (it != towerTemplateByPath.end()) {
            return it->second;
        }

        WorldTemplateModelSpec towerTemplate;
        towerTemplate.id = templateId;
        towerTemplate.modelPath = modelPath;
        const int idx = static_cast<int>(spec.towerTemplateModels.size());
        spec.towerTemplateModels.push_back(std::move(towerTemplate));
        towerTemplateByPath[modelPath] = idx;
        return idx;
    };

    auto ensureProjectileTemplate = [&](const std::string& templateId, const std::string& modelPath) {
        if (modelPath.empty()) {
            return -1;
        }
        auto it = projectileTemplateByPath.find(modelPath);
        if (it != projectileTemplateByPath.end()) {
            return it->second;
        }

        WorldTemplateModelSpec projectileTemplate;
        projectileTemplate.id = templateId;
        projectileTemplate.modelPath = modelPath;
        const int idx = static_cast<int>(spec.towerTemplateModels.size());
        spec.towerTemplateModels.push_back(std::move(projectileTemplate));
        projectileTemplateByPath[modelPath] = idx;
        return idx;
    };

    for (auto& [towerId, tower] : archetypes_) {
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

        templatePrototypeById_[towerId] = ensureTowerTemplate(towerId, tower.modelPath);

        if (!tower.projectileModelPath.empty()) {
            projectileTemplatePrototypeById_[towerId] =
                ensureProjectileTemplate("projectile:" + towerId, tower.projectileModelPath);
        }

        for (auto& node : tower.upgradeNodes) {
            node.towerPrototypeOverrideIndex =
                ensureTowerTemplate("upgrade_tower:" + towerId + ":" + node.id, node.towerModelPathOverride);
            node.projectilePrototypeOverrideIndex = ensureProjectileTemplate(
                "upgrade_projectile:" + towerId + ":" + node.id, node.projectileModelPathOverride);
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
