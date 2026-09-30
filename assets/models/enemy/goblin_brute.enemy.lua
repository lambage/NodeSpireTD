return {
    id = "goblin_brute",
    displayName = "Goblin Brute",
    description = "A hulking goblin bolted into scavenged plate, built to shrug off hits and grind the frontline forward.",
    model = "assets/models/enemy/goblin_brute.glb",

    stats = {
        health = 70,
        shield = 20,
        armor = 5,
        resistances = {
            poison = 25,
            fire = 10,
            arcane = 0,
        },
        moveSpeed = 1.7,
        rewardMoney = 45,
        baseDamage = 9
    },

    render = {
        renderScale = 1.15,
        facingYawOffsetDegrees = 0.0,
    }
}
