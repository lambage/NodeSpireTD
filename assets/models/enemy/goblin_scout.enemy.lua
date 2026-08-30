local DamageType = (Gameplay and Gameplay.DamageType) or {
    Physical = "physical",
    Fire = "fire",
    Poison = "poison",
    Arcane = "arcane",
    Electric = "electric",
    Holy = "holy",
    Necrotic = "necrotic",
}

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
            [DamageType.Poison] = 50,
            [DamageType.Fire] = 100,
            [DamageType.Arcane] = 125,
        },
        moveSpeed = 3.6,
        rewardMoney = 14,
        baseDamage = 3
    },

    render = {
        renderScale = 0.85,
        facingYawOffsetDegrees = 180.0,
    }
}
