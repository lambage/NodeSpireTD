return {
    id = "mage_tower",
    displayName = "Mage Tower",
    bio = "A crystal-crowned spire that channels raw magic through whichever rite its keeper chooses to bind. Where the Archer Hut commits to one arrow at a time, the Mage Tower rewards a caster who weaves two schools together -- or masters all three at once.",
    model = "assets/models/towers/mage_tower/mage_tower.glb",
    projectileModel = "assets/models/towers/mage_tower/mage_tower_bolt.glb",

    stats = {
        cost = 170,
        damageType = "arcane",
        targetMode = "first",
        armorPiercing = 1.0,
        attackDamage = 14,
        attackRange = 5.5,
        attackSpeed = 0.85,
        projectileSpeed = 14.0,
        splashRadius = 0.0,
        projectileCount = 1,
        chainTargetCount = 1,
        chainRange = 3.5,
        ricochetCount = 0,
        ricochetRange = 3.5,

        burnDamagePerSecond = 0.0,
        burnDuration = 2.0,
        slowAmount = 0.0,
        slowDuration = 1.0,
        freezeChance = 0.0,
        freezeDuration = 0.75,
        critChance = 0.0,
        critDamageMul = 1.5,
    },

    render = {
        renderScale = 2.0,
        facingYawOffsetDegrees = 0.0,
        projectileFacingYawOffsetDegrees = 180.0,
    },

    -- Talent shape, unlike the Archer Hut's silo-and-exclude tree: three
    -- Wells (Fire / Ice / Arcane) can all be opened at once -- nothing
    -- excludes another Well. Each Well has one node that deepens that
    -- school (feeding a single-school capstone) and one "enabler" node
    -- that feeds Convergence nodes shared with the other two schools.
    -- Convergence nodes are the payoff for branching across schools, a
    -- shape the Archer Hut's tree can't express at all. Capstones and the
    -- final Primordial Convergence stay mutually exclusive, so a caster
    -- still has to choose: go all-in on one element, or master the blend.
    upgradeTree = {
        nodes = {
            -- Core (universal) --------------------------------------------------
            {
                id = "resonant_attunement",
                displayName = "Resonant Attunement",
                description = "Tunes the crystal's resonance for faster spellcasting.",
                upgradeLevels = {
                    { cost = 90, effects = { attackSpeedAdd = 0.35 } },
                    { cost = 150, effects = { attackSpeedAdd = 0.85 } },
                },
            },
            {
                id = "focused_casting",
                displayName = "Focused Casting",
                description = "Sharper focus channels more raw power into every bolt.",
                requires = { "resonant_attunement" },
                upgradeLevels = {
                    { cost = 130, effects = { attackDamageAdd = 6.0 } },
                },
            },

            -- Wells (open freely -- no excludes between them) -------------------
            {
                id = "fire_well",
                displayName = "Ember Well",
                description = "Taps a vein of restrained flame within the crystal.",
                minUpgradesRequired = 2,
                requires = { "focused_casting" },
                upgradeLevels = {
                    { cost = 180, effects = { splashRadiusAdd = 0.6, attackDamageMul = 1.08 } },
                },
            },
            {
                id = "ice_well",
                displayName = "Rime Well",
                description = "Opens a current of deep cold through the shaft.",
                minUpgradesRequired = 2,
                requires = { "focused_casting" },
                upgradeLevels = {
                    { cost = 180, effects = { slowAmountAdd = 0.15, slowDurationAdd = 1.0, projectileSpeedMul = 1.05 } },
                },
            },
            {
                id = "arcane_well",
                displayName = "Arc Well",
                description = "Opens a channel for bolts to leap between foes.",
                minUpgradesRequired = 2,
                requires = { "focused_casting" },
                upgradeLevels = {
                    { cost = 180, effects = { chainTargetCountAdd = 1, chainRangeAdd = 1.0 } },
                },
            },

            -- Fire branch --------------------------------------------------------
            {
                id = "ember_cascade",
                displayName = "Ember Cascade",
                description = "Flames catch and spread through packed enemies. (Enables Fire Convergences.)",
                minUpgradesRequired = 3,
                requires = { "fire_well" },
                upgradeLevels = {
                    { cost = 140, effects = { splashRadiusAdd = 0.8, burnDamagePerSecondAdd = 4.0 } },
                },
            },
            {
                id = "cinder_wick",
                displayName = "Cinder Wick",
                description = "A slow-burning wick keeps the flame alive long after impact.",
                minUpgradesRequired = 3,
                requires = { "fire_well" },
                upgradeLevels = {
                    { cost = 140, effects = { burnDamagePerSecondAdd = 6.0, burnDurationAdd = 1.5 } },
                },
            },

            -- Ice branch -----------------------------------------------------------
            {
                id = "frostbite",
                displayName = "Frostbite",
                description = "Lingering cold saps the target's speed. (Enables Ice Convergences.)",
                minUpgradesRequired = 3,
                requires = { "ice_well" },
                upgradeLevels = {
                    { cost = 140, effects = { slowAmountAdd = 0.15, slowDurationAdd = 1.0 } },
                },
            },
            {
                id = "glacial_lance",
                displayName = "Glacial Lance",
                description = "A needle of ice punches through at range.",
                minUpgradesRequired = 3,
                requires = { "ice_well" },
                upgradeLevels = {
                    { cost = 145, effects = { projectileSpeedMul = 1.15, attackRangeAdd = 1.0, attackDamageAdd = 4.0 } },
                },
            },

            -- Arcane branch ----------------------------------------------------------
            {
                id = "volatile_matrix",
                displayName = "Volatile Matrix",
                description = "Unstable weave occasionally surges to devastating effect. (Enables Arcane Convergences.)",
                minUpgradesRequired = 3,
                requires = { "arcane_well" },
                upgradeLevels = {
                    { cost = 150, effects = { critChanceAdd = 0.10 } },
                },
            },
            {
                id = "overweave",
                displayName = "Overweave",
                description = "Extends the arcane current further afield.",
                minUpgradesRequired = 3,
                requires = { "arcane_well" },
                upgradeLevels = {
                    { cost = 145, effects = { chainTargetCountAdd = 1, chainRangeAdd = 1.2, attackRangeAdd = 0.8 } },
                },
            },

            -- Convergences -- require an enabler node from TWO different Wells.
            -- This is the shape the Archer Hut's tree cannot do: its excludes
            -- lists make cross-branch investment impossible.
            {
                id = "thermal_shock",
                displayName = "Thermal Shock",
                description = "Scalded flesh cracks against sudden cold, taking the full brunt of both. (Fire + Ice)",
                minUpgradesRequired = 5,
                requires = { "ember_cascade", "frostbite" },
                upgradeLevels = {
                    { cost = 185, effects = { burnDamagePerSecondAdd = 5.0, slowAmountAdd = 0.10, attackDamageMul = 1.10 } },
                },
            },
            {
                id = "wildfire_surge",
                displayName = "Wildfire Surge",
                description = "A surging crit sets its target alight. (Fire + Arcane)",
                minUpgradesRequired = 5,
                requires = { "ember_cascade", "volatile_matrix" },
                upgradeLevels = {
                    { cost = 185, effects = { critChanceAdd = 0.08, burnDamagePerSecondAdd = 3.0 } },
                },
            },
            {
                id = "frozen_precision",
                displayName = "Frozen Precision",
                description = "Chilled targets are easy prey for a precise strike. (Ice + Arcane)",
                minUpgradesRequired = 5,
                requires = { "frostbite", "volatile_matrix" },
                upgradeLevels = {
                    { cost = 185, effects = { critChanceAdd = 0.08, slowAmountAdd = 0.08 } },
                },
            },

            -- Specialist capstones -- the "go all-in on one element" option,
            -- mutually exclusive with each other and with Primordial Convergence.
            {
                id = "inferno",
                displayName = "Inferno",
                description = "Total immolation. Nothing that burns walks away twice.",
                minUpgradesRequired = 5,
                requires = { "cinder_wick" },
                excludes = { "absolute_zero", "archons_focus", "primordial_convergence" },
                upgradeLevels = {
                    { cost = 200, effects = { attackDamageMul = 1.35, splashRadiusAdd = 1.5, burnDamagePerSecondAdd = 8.0 } },
                },
            },
            {
                id = "absolute_zero",
                displayName = "Absolute Zero",
                description = "The cold given form. What it touches, it holds.",
                minUpgradesRequired = 5,
                requires = { "glacial_lance" },
                excludes = { "inferno", "archons_focus", "primordial_convergence" },
                upgradeLevels = {
                    { cost = 200, effects = { slowAmountAdd = 0.25, freezeChanceAdd = 0.20, freezeDurationAdd = 1.0, attackDamageMul = 1.15 } },
                },
            },
            {
                id = "archons_focus",
                displayName = "Archon's Focus",
                description = "Perfect clarity. Every surge finds its mark.",
                minUpgradesRequired = 5,
                requires = { "overweave" },
                excludes = { "inferno", "absolute_zero", "primordial_convergence" },
                upgradeLevels = {
                    { cost = 200, effects = { critChanceAdd = 0.20, critDamageMulAdd = 0.5, chainTargetCountAdd = 1 } },
                },
            },

            -- Ultimate: requires ALL THREE Convergences. Structurally
            -- impossible on the Archer Hut's tree -- the reward for a caster
            -- who mastered every school instead of specializing in one.
            {
                id = "primordial_convergence",
                displayName = "Primordial Convergence",
                description = "The three currents become one. A mage who has mastered every school channels them as a single, overwhelming force.",
                minUpgradesRequired = 10,
                requires = { "thermal_shock", "wildfire_surge", "frozen_precision" },
                excludes = { "inferno", "absolute_zero", "archons_focus" },
                upgradeLevels = {
                    {
                        cost = 260,
                        effects = {
                            attackDamageMul = 1.20,
                            burnDamagePerSecondAdd = 4.0,
                            slowAmountAdd = 0.10,
                            critChanceAdd = 0.10,
                            chainTargetCountAdd = 1,
                        },
                    },
                },
            },
        },
    },
}
