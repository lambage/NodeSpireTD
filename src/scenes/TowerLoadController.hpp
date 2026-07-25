#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;
struct WorldAssetSpec;

struct TowerArchetype {
    std::string id = "tower";
    std::string displayName = "Tower";
    std::string modelPath;
    std::string projectileModelPath;
    std::string previewImagePath;
    int cost = 100;
    float attackDamage = 1.0f;
    float attackRange = 5.0f;
    float attackSpeed = 1.0f;
    float projectileSpeed = 16.0f;
    float renderScale = 1.0f;
    float facingYawOffsetDegrees = 0.0f;
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
