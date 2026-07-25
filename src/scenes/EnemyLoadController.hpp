#pragma once

#include <string>
#include <unordered_map>

struct lua_State;

struct EnemyArchetype {
    std::string id = "goblin1";
    std::string displayName = "Goblin";
    std::string modelPath = "assets/models/enemy/goblin1.glb";
    float health = 35.0f;
    float moveSpeed = 2.8f;
    float rewardMoney = 8.0f;
    float spawnIntervalSeconds = 0.9f;
    float defeatIntervalSeconds = 1.2f;
    float baseDamage = 5.0f;
    float renderScale = 1.0f;
    float facingYawOffsetDegrees = 0.0f;
};

// Owns discovery/parsing/storage of enemy archetypes loaded from Lua scripts, plus the notion of
// a "default" enemy id used as a fallback when a requested archetype cannot be found.
class EnemyLoadController {
  public:
    explicit EnemyLoadController(lua_State* luaState);
    ~EnemyLoadController();

    void reset();

    // Parses an enemy archetype script and stores/registers the result.
    bool loadEnemyArchetype(const std::string& scriptPath);

    // Parses an enemy archetype script without storing it. Used when a caller (e.g. the Lua
    // Entity.Load API) needs to inspect/register the archetype itself.
    bool parseEnemyArchetypeScript(const std::string& scriptPath, EnemyArchetype& outArchetype) const;

    // Stores (or overwrites) an archetype. If this is the first archetype registered, it becomes
    // the default enemy id.
    void registerArchetype(EnemyArchetype archetype);

    bool empty() const { return archetypes_.empty(); }
    const std::unordered_map<std::string, EnemyArchetype>& archetypes() const { return archetypes_; }
    const std::string& defaultId() const { return defaultId_; }

    // Looks up an archetype by id, falling back to the default enemy id if not found.
    const EnemyArchetype* findArchetype(const std::string& enemyId) const;

  private:
    lua_State* L_;
    std::unordered_map<std::string, EnemyArchetype> archetypes_;
    std::string defaultId_;
};
