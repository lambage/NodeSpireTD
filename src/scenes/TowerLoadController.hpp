#pragma once

#include "scenes/DamageTypes.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;
struct WorldAssetSpec;

struct TowerArchetype {
  struct UpgradeUiStyle {
    std::string panelTitle = "Tower Talent Tree";
    std::string artPath;
    std::string defaultNodeIconPath = "assets/images/question.png";
    float accentR = 0.62f;
    float accentG = 0.42f;
    float accentB = 0.08f;
    float unlockedR = 0.15f;
    float unlockedG = 0.52f;
    float unlockedB = 0.27f;
    float lockedR = 0.22f;
    float lockedG = 0.23f;
    float lockedB = 0.26f;
  };

  struct UpgradeEffects {
    float attackDamageAdd = 0.0f;
    float attackDamageMul = 1.0f;
    float attackRangeAdd = 0.0f;
    float attackRangeMul = 1.0f;
    float attackSpeedAdd = 0.0f;
    float attackSpeedMul = 1.0f;
    float projectileSpeedAdd = 0.0f;
    float projectileSpeedMul = 1.0f;
    float splashRadiusAdd = 0.0f;
    float splashRadiusMul = 1.0f;
    float chainRangeAdd = 0.0f;
    float chainRangeMul = 1.0f;
    float ricochetRangeAdd = 0.0f;
    float ricochetRangeMul = 1.0f;
    int projectileCountAdd = 0;
    int chainTargetCountAdd = 0;
    int ricochetCountAdd = 0;
  };

  struct UpgradeNode {
    std::string id;
    std::string displayName;
    std::string description;
    std::string iconPath;
    std::string parentId;
    std::vector<std::string> childrenOrder;
    std::string towerModelPathOverride;
    std::string projectileModelPathOverride;
    std::string branch;
    int cost = 0;
    int tier = 0;
    int column = 0;
    int maxLevel = 1;
    int minUpgradesRequired = 0;
    int towerPrototypeOverrideIndex = -1;
    int projectilePrototypeOverrideIndex = -1;
    std::vector<std::string> requiredNodeIds;
    std::vector<std::string> excludes;
    UpgradeEffects effects{};
  };

    std::string id = "tower";
    std::string displayName = "Tower";
    std::string modelPath;
    std::string projectileModelPath;
    std::string previewImagePath;
    int cost = 100;
    playlevel::DamageType damageType = playlevel::DamageType::Physical;
    float attackDamage = 1.0f;
    float armorPiercing = 0.0f;
    float attackRange = 5.0f;
    float attackSpeed = 1.0f;
    float projectileSpeed = 16.0f;
    float splashRadius = 0.0f;
    float chainRange = 3.5f;
    float ricochetRange = 3.5f;
    int projectileCount = 1;
    int chainTargetCount = 1;
    int ricochetCount = 0;
    float renderScale = 1.0f;
    float facingYawOffsetDegrees = 0.0f;
    UpgradeUiStyle upgradeUi;
    std::vector<UpgradeNode> upgradeNodes;
};

// Owns discovery/parsing/storage of tower archetypes loaded from Lua scripts, along with the
// per-tower bookkeeping (loadout slots, instance pool debug-group names, template prototype
// indices) needed to render placed/pooled/ghost tower instances.
class TowerLoadController {
  public:
    static constexpr int kPoolPlacementsPerType = 32;

    explicit TowerLoadController(lua_State* luaState);
    ~TowerLoadController();

    void reset();

    // Recursively scans a directory for *.lua tower archetype scripts and loads each one.
    void discoverTowerArchetypesInDirectory(const std::filesystem::path& dir);

    // Parses a single tower archetype script and stores/loadout-registers the result.
    bool loadTowerArchetype(const std::string& scriptPath);

    // Registers icon textures, template models, and per-tower pool/ghost debug-group names into
    // the provided world asset spec. Must be called after all archetypes are loaded.
    void populateWorldAssets(WorldAssetSpec& spec);

    const TowerArchetype* findArchetype(const std::string& towerId) const;
    const TowerArchetype* archetypeAtLoadoutSlot(int slot) const;

    const std::vector<std::string>& loadoutIds() const { return loadoutIds_; }
    const std::unordered_map<std::string, TowerArchetype>& archetypes() const { return archetypes_; }
    const std::unordered_map<std::string, std::vector<std::string>>& poolGroupsById() const {
        return poolGroupsById_;
    }
    const std::unordered_map<std::string, std::string>& ghostGroupById() const { return ghostGroupById_; }

    // Returns -1 if the tower has no registered template prototype.
    int templatePrototypeIndex(const std::string& towerId) const;
    int projectileTemplatePrototypeIndex(const std::string& towerId) const;

    static std::string makeIconTextureId(const std::string& towerId);

  private:
    bool parseTowerArchetypeScript(const std::string& scriptPath, TowerArchetype& outArchetype);

    lua_State* L_;
    std::unordered_map<std::string, TowerArchetype> archetypes_;
    std::vector<std::string> loadoutIds_;
    std::unordered_map<std::string, std::vector<std::string>> poolGroupsById_;
    std::unordered_map<std::string, std::string> ghostGroupById_;
    std::unordered_map<std::string, int> templatePrototypeById_;
    std::unordered_map<std::string, int> projectileTemplatePrototypeById_;
};
