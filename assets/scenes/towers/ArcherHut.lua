return function(context, parent)
    local tree = context.talentControls(context, parent)
    local title = context.text(parent, "UpgradeTitle", "TALENTS", 16, context.colors.gold)
    local layout = {
        {"quickdraw_rig", 2.5, 0},
        {"hardened_draw", 2.5, 1},
        {"stone_specialization", 0.5, 2},
        {"metal_specialization", 2.5, 2},
        {"electric_specialization", 4.5, 2},
        {"stone_shatter", 0, 3},
        {"stone_penetrator", 1, 3},
        {"metal_overdraw", 2, 3},
        {"metal_serrated", 3, 3},
        {"electric_chain", 4, 3},
        {"electric_focus", 5, 3},
        {"stone_metal_hybrid", 0, 4},
        {"stone_aftershock", 1, 4},
        {"metal_triple_shot", 2, 4},
        {"metal_precision", 3, 4},
        {"electric_ricochet", 4, 4},
        {"electric_static_field", 5, 4}
    }
    local links = {
        {"quickdraw_rig", "hardened_draw"},
        {"hardened_draw", "stone_specialization"},
        {"hardened_draw", "metal_specialization"},
        {"hardened_draw", "electric_specialization"},
        {"stone_specialization", "stone_shatter"},
        {"stone_specialization", "stone_penetrator"},
        {"metal_specialization", "metal_overdraw"},
        {"metal_specialization", "metal_serrated"},
        {"electric_specialization", "electric_chain"},
        {"electric_specialization", "electric_focus"},
        {"stone_shatter", "stone_metal_hybrid"},
        {"stone_shatter", "stone_aftershock"},
        {"metal_overdraw", "metal_triple_shot"},
        {"metal_overdraw", "metal_precision"},
        {"electric_chain", "electric_ricochet"},
        {"electric_chain", "electric_static_field"}
    }
    return {Update = function(_, selection, availableWidth)
        local width, height = math.max(availableWidth, 504), 384
        local origin = (width - 480) / 2 + 14
        tree:Begin(selection)
        for _, node in ipairs(layout) do
            tree:Node(node[1], origin + node[2] * 80, 12 + node[3] * 76,
                "assets/images/talents/archer/archer_" .. node[1] .. ".png")
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