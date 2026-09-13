-- Enemy definition template
--
-- Copy this file to assets/models/enemy/<enemy_id>.enemy.lua and fill in
-- every TODO. See docs/ENEMY_AUTHORING_GUIDE.md for the full workflow and
-- for what the Blender source file should look like before you get here.
--
-- Reference examples in this folder:
--   goblin_scout.enemy.lua  - fast, fragile skirmisher
--   goblin1.enemy.lua       - slow, tanky frontliner

return {
    id = "TODO_enemy_id",                    -- unique, snake_case, matches the .glb filename (no extension)
    displayName = "TODO Display Name",       -- shown to the player
    description = "TODO one-sentence flavor description.",
    model = "assets/models/enemy/TODO_enemy_id.glb",

    stats = {
        health = 0,             -- hit points. Reference: Scout 22 (fragile) - Grunt 35 (tanky)
        shield = 0,             -- flat damage absorbed before health; 0 if this enemy has none
        armor = 0,              -- flat damage reduction per hit. Reference: Scout 1 - Grunt 2
        resistances = {
            poison = 0,          -- % resistance, 0-100. Existing goblins: 25
            fire = 0,            -- Existing goblins: 10
            arcane = 0,          -- Existing goblins: 0
        },
        moveSpeed = 0.0,        -- units/second. Reference: Scout 3.6 (fast) - Grunt 2.8 (slow)
        rewardMoney = 0,        -- gold granted on kill. Reference: Scout 14 - Grunt 20
        baseDamage = 0          -- damage dealt to the player's base. Reference: Scout 3 - Grunt 5
    },

    render = {
        renderScale = 1.0,                 -- uniform scale applied in-engine. Model to the shared ~1m rig height
                                            -- and use this to size the enemy up/down rather than remodeling it.
        facingYawOffsetDegrees = 0.0,       -- rotates the model to face its travel direction; tune if the rig's
                                            -- forward axis doesn't line up (Grunt needs 180 here, Scout needs 0).
    }
}
