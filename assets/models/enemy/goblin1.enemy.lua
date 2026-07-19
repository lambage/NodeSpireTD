return {
    id = "goblin1",
    displayName = "Goblin Grunt",
    model = "assets/models/enemy/goblin1.glb",

    stats = {
        health = 35,
        moveSpeed = 2.8,
        rewardMoney = 20,
        baseDamage = 5
    },

    render = {
        renderScale = 1.0,
        facingYawOffsetDegrees = 180.0,
    },

    behavior = {
        -- Placeholder behavior fields so we can expand to script-driven logic later.
        lanePreference = "default",
        targetPriority = "base"
    }
}
