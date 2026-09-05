local M = {
    waves = {
        {
            roundDurationSeconds = 30,
            spawns = {
                { enemyId = "goblin1", count = 6, spawnIntervalSeconds = 0.9 },
            },
        },
        {
            roundDurationSeconds = 35,
            spawns = {
                { enemyId = "goblin1", count = 9, spawnIntervalSeconds = 0.8 },
            },
        },
        {
            roundDurationSeconds = 40,
            spawns = {
                { enemyId = "goblin_scout", count = 4, spawnIntervalSeconds = 0.55 },
                { enemyId = "goblin1", count = 8, spawnIntervalSeconds = 0.72 },
            },
        },
        {
            roundDurationSeconds = 40,
            spawns = {
                { enemyId = "goblin_scout", count = 10, spawnIntervalSeconds = 0.55 },
                { enemyId = "goblin1", count = 20, spawnIntervalSeconds = 0.72 },
                { enemyId = "goblin_scout", count = 10, spawnIntervalSeconds = 0.55 },
            },
        },
        {
            roundDurationSeconds = 40,
            spawns = {
                { enemyId = "goblin_scout", count = 10, spawnIntervalSeconds = 0.35 },
                { enemyId = "goblin1", count = 10, spawnIntervalSeconds = 0.5 },
                { enemyId = "goblin_scout", count = 10, spawnIntervalSeconds = 0.35 },
                { enemyId = "goblin1", count = 10, spawnIntervalSeconds = 0.5 },
                { enemyId = "goblin_scout", count = 10, spawnIntervalSeconds = 0.35 },
                { enemyId = "goblin1", count = 10, spawnIntervalSeconds = 0.5 },
                { enemyId = "goblin_scout", count = 10, spawnIntervalSeconds = 0.35 },
                { enemyId = "goblin1", count = 10, spawnIntervalSeconds = 0.5 },
            },
        },
    },
}

return M
