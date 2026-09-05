#pragma once

#include <string>
#include <vector>

namespace NodeSpireUi {

enum class PlayLevelUiPhase {
    Loading,
    LoadFailed,
    WaitingToStart,
    Running,
    Paused,
    Victory,
    Defeat,
};

enum class PlayLevelSelectionKind {
    None,
    Tower,
    Enemy,
};

enum class PlayLevelTargetingMode {
    First,
    Last,
    Nearest,
    Random,
    HighestHealth,
    LowestHealth,
};

struct PlayLevelCommandResult {
    bool accepted = false;
    std::string message;
};

struct PlayLevelTowerSlotView {
    int slot = 0;
    std::string towerId;
    std::string displayName;
    std::string previewImagePath;
    int cost = 0;
    bool available = false;
    bool selected = false;
    bool enabled = false;
    std::string disabledReason;
};

struct PlayLevelPlacementView {
    bool visible = false;
    bool canPlace = false;
    std::string towerName;
    std::string verdict;
    std::string reason;
};

struct PlayLevelUpgradeView {
    std::string id;
    std::string displayName;
    std::string description;
    std::string iconPath;
    int cost = 0;
    int currentLevel = 0;
    int maxLevel = 0;
    bool enabled = false;
    std::string disabledReason;
};

struct PlayLevelTowerSelectionView {
    std::string displayName;
    std::string bio;
    std::string targetingLabel;
    int sellValue = 0;
    bool canModify = false;
    std::vector<PlayLevelUpgradeView> upgrades;
};

struct PlayLevelEnemySelectionView {
    std::string displayName;
    std::string description;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float shield = 0.0f;
    float maxShield = 0.0f;
    float armor = 0.0f;
    float moveSpeed = 0.0f;
    float baseDamage = 0.0f;
    float bounty = 0.0f;
};

struct PlayLevelUiSnapshot {
    PlayLevelUiPhase phase = PlayLevelUiPhase::Loading;
    std::string levelName;
    std::string headline;
    std::string supportingText;
    std::string loadingActivity;
    float loadingProgress = 0.0f;

    int baseHealth = 0;
    int money = 0;
    int currentWave = 0;
    int waveCount = 0;
    int enemiesAlive = 0;
    int enemiesRemaining = 0;

    bool countdownVisible = false;
    std::string countdownLabel;
    int countdownSeconds = 0;
    float countdownProgress = 0.0f;

    bool startWaveVisible = false;
    bool startWaveEnabled = false;
    std::string startWaveDisabledReason;
    bool loadoutVisible = false;
    std::vector<PlayLevelTowerSlotView> towerSlots;
    PlayLevelPlacementView placement;

    PlayLevelSelectionKind selectionKind = PlayLevelSelectionKind::None;
    PlayLevelTowerSelectionView towerSelection;
    PlayLevelEnemySelectionView enemySelection;
};

struct PlayLevelUiStateInput {
    PlayLevelUiPhase phase = PlayLevelUiPhase::Loading;
    std::string levelName;
    std::string loadingActivity;
    float loadingProgress = 0.0f;
    int baseHealth = 0;
    int money = 0;
    int currentWave = 0;
    int waveCount = 0;
    int enemiesAlive = 0;
    int enemiesToSpawn = 0;
    bool waveInProgress = false;
    bool preWaveCountdownActive = false;
    float preWaveCountdownRemainingSeconds = 0.0f;
    float preWaveCountdownDurationSeconds = 0.0f;
    float roundCountdownRemainingSeconds = 0.0f;
    float roundCountdownDurationSeconds = 0.0f;
    bool worldReady = false;
    bool routeReady = false;
    bool enemyTemplateReady = false;
};

PlayLevelUiSnapshot buildPlayLevelUiSnapshot(const PlayLevelUiStateInput& input);

class IPlayLevelUiApi {
  public:
    virtual ~IPlayLevelUiApi() = default;

    virtual const PlayLevelUiSnapshot& snapshot() const = 0;
    virtual PlayLevelCommandResult requestStartWave() = 0;
    virtual PlayLevelCommandResult selectTowerSlot(int slot) = 0;
    virtual PlayLevelCommandResult cancelTowerPlacement() = 0;
    virtual PlayLevelCommandResult clearSelection() = 0;
    virtual PlayLevelCommandResult unlockSelectedTowerUpgrade(const std::string& upgradeId) = 0;
    virtual PlayLevelCommandResult setSelectedTowerTargeting(PlayLevelTargetingMode mode) = 0;
    virtual PlayLevelCommandResult sellSelectedTower() = 0;
    virtual PlayLevelCommandResult setPaused(bool paused) = 0;
    virtual PlayLevelCommandResult restartLevel() = 0;
    virtual PlayLevelCommandResult returnToLobby() = 0;
};

} // namespace NodeSpireUi