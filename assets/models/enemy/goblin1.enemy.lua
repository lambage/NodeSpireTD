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
            poison = 50,
            fire = 100,
            arcane = 125,
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
