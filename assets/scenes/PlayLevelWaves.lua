---@diagnostic disable: undefined-global

local M = {}

function M.onLoad()
    if not Entity or not Wave then
        return
    end

    Wave.Reset()

    local goblin1 = Entity.Load("assets/models/enemy/goblin1.enemy.lua")

    -- Example future extension:
    -- local goblin2 = Entity.Load("assets/models/enemy/goblin2.enemy.lua")

    Wave.Register({
        { entity = goblin1, count = 6, spawnIntervalSeconds = 0.9 },
    }, 30)

    Wave.Register({
        { entity = goblin1, count = 9, spawnIntervalSeconds = 0.8 },
    }, 35)

    Wave.Register({
        { entity = goblin1, count = 12, spawnIntervalSeconds = 0.72 },
        -- { entity = goblin2, count = 10, spawnIntervalSeconds = 1.0 },
    }, 40)
end

-- Backward-compatible fallback shape if a scene still reads returned waves.
M.waves = {
    {
        enemyId = "goblin1",
        count = 6,
        spawnIntervalSeconds = 0.9,
        roundDurationSeconds = 30,
    },
}

return M
