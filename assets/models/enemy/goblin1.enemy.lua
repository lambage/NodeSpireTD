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
    id = "goblin1",
    displayName = "Goblin Grunt",
    description = "A reckless frontliner that charges the base in noisy packs.",
    model = "assets/models/enemy/goblin1.glb",

    stats = {
        health = 35,
        shield = 0,
        armor = 2,
        resistances = {
            [DamageType.Poison] = 50,
            [DamageType.Fire] = 100,
            [DamageType.Arcane] = 125,
        },
        moveSpeed = 2.8,
        rewardMoney = 20,
        baseDamage = 5
    },

    render = {
        renderScale = 1.0,
        facingYawOffsetDegrees = 180.0,
    }
}
