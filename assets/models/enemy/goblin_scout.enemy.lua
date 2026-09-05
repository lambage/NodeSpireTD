return {
    id = "goblin_scout",
    displayName = "Goblin Scout",
    description = "A small, quick goblin that darts ahead of the main pack.",
    model = "assets/models/enemy/goblin_scout.glb",

    stats = {
        health = 22,
        shield = 0,
        armor = 1,
        resistances = {
            poison = 50,
            fire = 100,
            arcane = 125,
        },
        moveSpeed = 3.6,
        rewardMoney = 14,
        baseDamage = 3
    },

    render = {
        renderScale = 0.85,
        facingYawOffsetDegrees = 0.0,
    }
}
