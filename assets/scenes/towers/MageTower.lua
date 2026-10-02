return function(context, parent)
    local tree = context.talentControls(context, parent)
    local title = context.text(parent, "UpgradeTitle", "TALENTS", 16, context.colors.gold)
    local layout = {
        {"resonant_attunement", 2.5, 0},
        {"focused_casting", 2.5, 1},
        {"fire_well", 0.5, 2},
        {"ice_well", 2.5, 2},
        {"arcane_well", 4.5, 2},
        {"ember_cascade", 0, 3},
        {"cinder_wick", 1, 3},
        {"frostbite", 2, 3},
        {"glacial_lance", 3, 3},
        {"volatile_matrix", 4, 3},
        {"overweave", 5, 3},
        {"thermal_shock", 0, 4},
        {"inferno", 1, 4},
        {"wildfire_surge", 2, 4},
        {"absolute_zero", 3, 4},
        {"frozen_precision", 4, 4},
        {"archons_focus", 5, 4},
        {"primordial_convergence", 2.5, 5}
    }
    local links = {
        {"resonant_attunement", "focused_casting"},
        {"focused_casting", "fire_well"},
        {"focused_casting", "ice_well"},
        {"focused_casting", "arcane_well"},
        {"fire_well", "ember_cascade"},
        {"fire_well", "cinder_wick"},
        {"ice_well", "frostbite"},
        {"ice_well", "glacial_lance"},
        {"arcane_well", "volatile_matrix"},
        {"arcane_well", "overweave"},
        {"ember_cascade", "thermal_shock"},
        {"frostbite", "thermal_shock"},
        {"ember_cascade", "wildfire_surge"},
        {"volatile_matrix", "wildfire_surge"},
        {"frostbite", "frozen_precision"},
        {"volatile_matrix", "frozen_precision"},
        {"cinder_wick", "inferno"},
        {"glacial_lance", "absolute_zero"},
        {"overweave", "archons_focus"},
        {"thermal_shock", "primordial_convergence"},
        {"wildfire_surge", "primordial_convergence"},
        {"frozen_precision", "primordial_convergence"}
    }
    return {Update = function(_, selection, availableWidth)
        local width, height = math.max(availableWidth, 504), 460
        local origin = (width - 480) / 2 + 14
        tree:Begin(selection)
        for _, node in ipairs(layout) do
            tree:Node(node[1], origin + node[2] * 80, 12 + node[3] * 76,
                "assets/images/talents/mage/mage_" .. node[1] .. ".png")
        end
        for _, link in ipairs(links) do tree:Link(link[1], link[2]) end
        local purchased = 0
        for _, node in ipairs(selection.upgrades or {}) do purchased = purchased + node.level end
        title:SetText("TALENTS   " .. purchased .. " RANKS")
        context.place(title, parent, 4, 0, width - 8, 24)
        context.place(tree.root, parent, 0, 32, width, height)
        return width, height + 48
    end}
end