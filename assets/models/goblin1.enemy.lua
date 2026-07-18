return {
    id = "goblin1",
    displayName = "Goblin Grunt",
    model = "assets/models/goblin1.glb",

    stats = {
        health = 35,
        moveSpeed = 2.8,
        rewardMoney = 8,
        baseDamage = 5,
        renderScale = 1.0,
        facingYawOffsetDegrees = 180.0
    },

    wave = {
        spawnIntervalSeconds = 0.9,
        defeatIntervalSeconds = 1.2,
        baseDamage = 5
    },

    behavior = {
        -- Placeholder behavior fields so we can expand to script-driven logic later.
        lanePreference = "default",
        targetPriority = "base"
    }
}
