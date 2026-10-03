return function(context, parent)
    local frame, text, button, place = context.frame, context.text, context.button, context.place
    local colors = context.colors
    local root = frame(parent, "TalentTree", 0x0B1012FF)
    local links = frame(root, "TalentLinks")
    links:SetAllPoints(root)
    links:SetMouseEnabled(false)
    local controls, connections, nodes, positions = {}, {}, {}, {}
    local linkCount = 0
    local view = {root = root}

    function view:Begin(selection)
        nodes, positions, linkCount = {}, {}, 0
        for index, node in ipairs(selection.upgrades or {}) do
            nodes[node.id] = {value = node, index = index}
        end
        for _, item in pairs(controls) do item.root:SetVisible(false) end
        for _, control in ipairs(connections) do control:SetVisible(false) end
    end

    function view:Node(id, left, top, source)
        local entry = nodes[id]
        if not entry then return end
        local node, index = entry.value, entry.index
        positions[id] = {x = left, y = top}
        if not controls[index] then
            local control = button(root, "Upgrade" .. index, "", function() context.purchase(controls[index].id) end)
            local inset = frame(control, "TalentInset" .. index, 0x151C20FF)
            inset:SetMouseEnabled(false)
            local icon = inset:CreateImage("TalentIcon" .. index)
            icon:SetFit("COVER")
            icon:SetMouseEnabled(false)
            icon:SetAllPoints(inset)
            local symbol = text(inset, "TalentSymbol" .. index, "", 20, colors.ink, true)
            symbol:SetWordWrap(false)
            local badge = frame(control, "TalentRank" .. index, 0x080C0FFF)
            badge:SetMouseEnabled(false)
            local level = text(badge, "TalentLevel" .. index, "", 16, colors.gold)
            level:SetWordWrap(false)
            controls[index] = {root = control, inset = inset, icon = icon, symbol = symbol, badge = badge, level = level}
        end
        local item = controls[index]
        item.id = id
        local learned = node.level > 0
        local border = learned and colors.gold or (node.enabled and colors.accent or 0x515A5FFF)
        item.root:SetVisible(true)
        item.root:SetButtonColors(border, node.enabled and 0xF4D88BFF or 0x899397FF, colors.gold)
        place(item.root, root, left, top, 52, 52)
        place(item.inset, item.root, 3, 3, 46, 46)
        place(item.symbol, item.inset, 6, 10, 40, 26)
        place(item.badge, item.root, 19, 37, 39, 22)
        place(item.level, item.badge, 4, 0, 35, 22)
        source = node.icon or source
        if item.icon:GetSource() ~= source then item.icon:SetSource(source) end
        item.icon:SetTint(learned and 0xFFFFFFFF or (node.enabled and 0xB9CED6FF or 0x50585FFF))
        local initials = ""
        for word in node.name:gmatch("%S+") do initials = initials .. word:sub(1, 1) end
        item.symbol:SetText(initials:sub(1, 2):upper())
        item.symbol:SetVisible(not item.icon:IsLoaded())
        item.symbol:SetColor(learned and colors.gold or (node.enabled and colors.ink or colors.muted))
        item.level:SetText(node.level .. "/" .. node.maxLevel)
        item.level:SetColor(learned and colors.gold or colors.muted)
        local tooltip = node.name .. "  " .. node.level .. "/" .. node.maxLevel .. "\n" .. (node.description or "")
        if node.level < node.maxLevel then tooltip = tooltip .. "\nNext rank: $" .. node.cost end
        if (node.minUpgradesRequired or 0) > 0 then
            tooltip = tooltip .. "\nRequires " .. node.minUpgradesRequired .. " total talent levels"
        end
        if not node.enabled and (node.reason or "") ~= "" then tooltip = tooltip .. "\n" .. node.reason end
        item.root:SetTooltip(tooltip)
    end

    local function segment(left, top, width, height, color)
        linkCount = linkCount + 1
        if not connections[linkCount] then
            connections[linkCount] = frame(links, "TalentLink" .. linkCount)
            connections[linkCount]:SetMouseEnabled(false)
        end
        local control = connections[linkCount]
        control:SetVisible(true)
        control:SetBackgroundColor(color)
        place(control, links, left, top, math.max(2, width), math.max(2, height))
    end

    function view:Link(parentId, childId)
        local parentPosition, childPosition = positions[parentId], positions[childId]
        if not parentPosition or not childPosition then return end
        local parentNode, childNode = nodes[parentId].value, nodes[childId].value
        local fromX, fromY = parentPosition.x + 26, parentPosition.y + 52
        local toX, toY = childPosition.x + 26, childPosition.y
        local middle = (fromY + toY) / 2
        local color = parentNode.level > 0 and (childNode.level > 0 and colors.gold or 0x8A794AFF) or 0x394247FF
        segment(fromX - 1, fromY, 2, middle - fromY, color)
        segment(math.min(fromX, toX) - 1, middle, math.abs(toX - fromX) + 2, 2, color)
        segment(toX - 1, middle, 2, toY - middle, color)
    end

    return view
end