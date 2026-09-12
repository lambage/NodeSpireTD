return {
    id = "archer_hut",
    displayName = "Archer Hut",
    bio = "A frontier watchpost crewed by disciplined bowyers. Archer Huts favor lane control, and can be tuned from heavy impact volleys to chain-hopping electric harassment.",
    model = "assets/models/towers/archer_hut/archer_hut_phase1.glb",
    projectileModel = "assets/models/towers/archer_hut/archer_hut_arrow.glb",

    stats = {
        cost = 150,
        damageType = "physical",
        targetMode = "first",
        armorPiercing = 1.0,
        attackDamage = 20,
        attackRange = 5.0,
        attackSpeed = 1.0,
        projectileSpeed = 16.0,
        splashRadius = 0.0,
        projectileCount = 1,
        chainTargetCount = 1,
        chainRange = 3.5,
        ricochetCount = 0,
        ricochetRange = 3.5,
    },

    render = {
        renderScale = 2.0,
        facingYawOffsetDegrees = 0.0,
        projectileFacingYawOffsetDegrees = 180.0,
    },

    upgradeTree = {
        nodes = {
            {
                id = "quickdraw_rig",
                displayName = "Quickdraw Rig",
                description = "A tuned bow rig that increases firing cadence.",
                upgradeLevels = {
                    {
                        cost = 90,
                        effects = {
                            attackSpeedAdd = 0.5,
                        },
                    },
                    {
                        cost = 150,
                        effects = {
                            attackSpeedAdd = 1.5,
                        },
                    },
                },
            },
            {
                id = "hardened_draw",
                displayName = "Hardened Draw",
                description = "Reinforced limbs transfer more force into every shot.",
                requires = { "quickdraw_rig" },
                upgradeLevels = {
                    {
                        cost = 130,
                        effects = {
                            attackDamageAdd = 7.0,
                        },
                    },
                },
            },
            {
                id = "stone_specialization",
                displayName = "Stone Specialization",
                description = "Heavy stone heads trade finesse for crushing impact.",
                minUpgradesRequired = 2,
                model = "assets/models/towers/archer_hut/archer_hut_phase2.glb",
                requires = { "hardened_draw" },
                excludes = { "metal_specialization", "electric_specialization" },
                upgradeLevels = {
                    {
                        cost = 180,
                        effects = {
                            attackDamageMul = 1.20,
                            splashRadiusAdd = 1.1,
                            projectileSpeedMul = 0.92,
                        },
                    },
                },
            },
            {
                id = "metal_specialization",
                displayName = "Metal Specialization",
                description = "Forged heads favor fast puncture and repeated hits.",
                minUpgradesRequired = 2,
                requires = { "hardened_draw" },
                excludes = { "stone_specialization", "electric_specialization" },
                upgradeLevels = {
                    {
                        cost = 180,
                        effects = {
                            attackSpeedMul = 1.14,
                            projectileCountAdd = 1,
                            attackDamageMul = 0.88,
                        },
                    },
                },
            },
            {
                id = "electric_specialization",
                displayName = "Electric Specialization",
                description = "Charged arrows arc between targets in packed lanes.",
                minUpgradesRequired = 2,
                requires = { "hardened_draw" },
                excludes = { "stone_specialization", "metal_specialization" },
                upgradeLevels = {
                    {
                        cost = 180,
                        effects = {
                            projectileSpeedMul = 1.2,
                            attackRangeAdd = 0.8,
                            chainTargetCountAdd = 1,
                            chainRangeAdd = 1.2,
                        },
                    },
                },
            },
            {
                id = "stone_shatter",
                displayName = "Shatter Volley",
                description = "Massive heads crack armor and hit harder.",
                minUpgradesRequired = 3,
                requires = { "stone_specialization" },
                upgradeLevels = {
                    {
                        cost = 135,
                        effects = {
                            attackDamageMul = 1.20,
                            splashRadiusAdd = 1.9,
                        },
                    },
                },
            },
            {
                id = "stone_penetrator",
                displayName = "Penetrator Heads",
                description = "Narrow stone points trade blast for piercing lanes.",
                minUpgradesRequired = 3,
                requires = { "stone_specialization" },
                upgradeLevels = {
                    {
                        cost = 130,
                        effects = {
                            attackDamageAdd = 4.0,
                            projectileSpeedAdd = 2.5,
                            splashRadiusAdd = -0.6,
                        },
                    },
                },
            },
            {
                id = "metal_overdraw",
                displayName = "Overdraw Rig",
                description = "Reinforced bow frame for rapid overdraw.",
                minUpgradesRequired = 3,
                requires = { "metal_specialization" },
                upgradeLevels = {
                    {
                        cost = 140,
                        effects = {
                            attackSpeedMul = 1.25,
                        },
                    },
                },
            },
            {
                id = "metal_serrated",
                displayName = "Serrated Vanes",
                description = "Ragged edges shred through clustered enemies.",
                minUpgradesRequired = 3,
                requires = { "metal_specialization" },
                upgradeLevels = {
                    {
                        cost = 145,
                        effects = {
                            attackDamageMul = 1.12,
                            ricochetCountAdd = 1,
                        },
                    },
                },
            },
            {
                id = "electric_chain",
                displayName = "Chain Arc",
                description = "Charged arrows jump between packed enemies.",
                minUpgradesRequired = 3,
                requires = { "electric_specialization" },
                upgradeLevels = {
                    {
                        cost = 145,
                        effects = {
                            attackRangeMul = 1.18,
                            projectileSpeedMul = 1.2,
                            chainTargetCountAdd = 2,
                            chainRangeAdd = 1.2,
                        },
                    },
                },
            },
            {
                id = "electric_focus",
                displayName = "Current Focusing",
                description = "Condenses charge into tighter arcs with longer reach.",
                minUpgradesRequired = 3,
                requires = { "electric_specialization" },
                upgradeLevels = {
                    {
                        cost = 150,
                        effects = {
                            attackRangeAdd = 1.1,
                            chainRangeMul = 1.15,
                        },
                    },
                },
            },
            {
                id = "metal_triple_shot",
                displayName = "Triple Shot",
                description = "Fires three arrows at once and can split across targets.",
                minUpgradesRequired = 4,
                requires = { "metal_overdraw" },
                upgradeLevels = {
                    {
                        cost = 190,
                        effects = {
                            projectileCountAdd = 2,
                            attackDamageMul = 0.84,
                        },
                    },
                },
            },
            {
                id = "metal_precision",
                displayName = "Sightline Bracing",
                description = "A precision rig tightens release timing for consistent lanes.",
                minUpgradesRequired = 4,
                requires = { "metal_overdraw" },
                upgradeLevels = {
                    {
                        cost = 185,
                        effects = {
                            attackRangeAdd = 1.0,
                            attackSpeedAdd = 0.12,
                            projectileSpeedMul = 1.1,
                        },
                    },
                },
            },
            {
                id = "electric_ricochet",
                displayName = "Arc Ricochet",
                description = "Impacts rebound to nearby enemies repeatedly.",
                minUpgradesRequired = 4,
                requires = { "electric_chain" },
                upgradeLevels = {
                    {
                        cost = 180,
                        effects = {
                            ricochetCountAdd = 2,
                            ricochetRangeAdd = 1.4,
                        },
                    },
                },
            },
            {
                id = "electric_static_field",
                displayName = "Static Field",
                description = "Leaves a lingering static wake that chains farther.",
                minUpgradesRequired = 4,
                requires = { "electric_chain" },
                upgradeLevels = {
                    {
                        cost = 185,
                        effects = {
                            chainTargetCountAdd = 1,
                            chainRangeAdd = 1.0,
                            ricochetRangeAdd = 0.8,
                        },
                    },
                },
            },
            {
                id = "stone_metal_hybrid",
                displayName = "Composite Heads",
                description = "Unlocked only by committing to either stone or metal engineering.",
                minUpgradesRequired = 5,
                requires = { "stone_shatter" },
                excludes = { "electric_specialization", "electric_chain", "electric_ricochet" },
                upgradeLevels = {
                    {
                        cost = 170,
                        effects = {
                            attackDamageAdd = 8.0,
                            attackSpeedAdd = 0.2,
                        },
                    },
                },
            },
            {
                id = "stone_aftershock",
                displayName = "Aftershock Bursts",
                description = "Heavy impacts kick fragments sideways into nearby lanes.",
                minUpgradesRequired = 5,
                requires = { "stone_shatter" },
                excludes = { "electric_specialization", "electric_chain", "electric_ricochet" },
                upgradeLevels = {
                    {
                        cost = 175,
                        effects = {
                            splashRadiusAdd = 1.2,
                            ricochetCountAdd = 1,
                            attackDamageMul = 1.06,
                        },
                    },
                },
            },
        },
    },
}
