local CLICK, HOVER = "assets/audio/click.ogg", "assets/audio/hover.ogg"
Audio.Preload(CLICK, "Sfx")
Audio.Preload(HOVER, "Sfx")

local colors = {
    ink = 0xEDF0E8FF, muted = 0xA8B0AAFF, gold = 0xE1BD67FF, accent = 0x83B9A4FF,
    panel = 0x101716F5, line = 0x38443EFF, button = 0x1D2925FF, hover = 0x2A3B35FF,
    selected = 0x305542FF, disabled = 0x202522FF, red = 0x9A3F32FF, redHover = 0xB24B3BFF
}
local state = Play.State()
local refresh, feedback, refreshing = nil, "", false
local chatOpen, previousChat, previousSelection = false, "", ""
local function place(widget, parent, left, top, width, height)
    widget:SetSize(math.max(0, width), math.max(0, height))
    widget:SetPoint("TOPLEFT", parent, "TOPLEFT", left, top)
end
local function frame(parent, name, color)
    local widget = UI.CreateFrame("Frame", name, parent)
    if color then widget:SetBackgroundColor(color) end
    return widget
end
local function text(parent, name, value, size, color, serif)
    local widget = parent:CreateFontString(name)
    widget:SetFont(serif and "timesbd" or "Inter-Regular", size or 16)
    widget:SetText(value)
    widget:SetColor(color or colors.ink)
    widget:SetWordWrap(true)
    widget:SetMouseEnabled(false)
    return widget
end
local function command(action, ...)
    local success, message = action(...)
    feedback = message or (success == false and "Action unavailable." or "")
    if refresh then refresh() end
    return success
end
local function button(parent, name, caption, action, primary)
    local widget = UI.CreateFrame("Button", name, parent)
    widget:SetButtonColors(primary and colors.red or colors.button, primary and colors.redHover or colors.hover, colors.selected)
    local label = text(widget, name .. "Label", caption)
    label:SetPoint("TOPLEFT", widget, "TOPLEFT", 12, 9)
    label:SetPoint("BOTTOMRIGHT", widget, "BOTTOMRIGHT", -8, -6)
    widget:SetScript("OnEnter", function() Audio.Play(HOVER, "Sfx") end)
    widget:SetScript("OnClick", function()
        Audio.Play(CLICK, "Sfx")
        action()
    end)
    return widget, label
end
local function enabled(widget, available, primary)
    widget:SetMouseEnabled(available)
    widget:SetButtonColors(available and (primary and colors.red or colors.button) or colors.disabled,
        available and (primary and colors.redHover or colors.hover) or colors.disabled, colors.selected)
end
local function linesHeight(value, width)
    local count = 0
    for line in (value .. "\n"):gmatch("(.-)\n") do
        count = count + math.max(1, math.ceil(#line / math.max(1, math.floor(width / 9))))
    end
    return count * 22
end

local root = frame(nil, "PlayRoot")
root:SetAllPoints(UI.Root)
root:SetMouseEnabled(false)
local stats = frame(root, "BattleStats", colors.panel)
local statLabels, statValues = {}, {}
for index, caption in ipairs({"BASE", "WAVE", "ENEMIES"}) do
    statLabels[index] = text(stats, caption .. "Caption", caption, 16, colors.muted)
    statValues[index] = text(stats, caption .. "Value", "0", 24, index == 1 and colors.accent or colors.ink, true)
end
local level = text(root, "LevelName", state.level or "", 16, colors.ink)
local gold = frame(root, "GoldPlaque", colors.panel)
local goldCaption = text(gold, "GoldCaption", "GOLD", 16, colors.gold)
local goldValue = text(gold, "GoldValue", "$0", 24, colors.gold, true)
local menu = button(root, "MatchMenu", "II", function() command(Play.Pause, true) end)
menu:SetTooltip("Match menu")
local chatToggle = button(root, "ChatToggle", "Chat", function() chatOpen = not chatOpen; refresh() end)

local countdown = frame(root, "WaveCountdown", colors.panel)
local countdownLabel = text(countdown, "CountdownLabel", "Next wave in", 16, colors.accent)
local countdownValue = text(countdown, "CountdownValue", "5", 32, colors.ink, true)
local countdownBar = UI.CreateFrame("ProgressBar", "CountdownProgress", countdown)
countdownBar:SetMinMaxValues(0, 1)

local loadout = frame(root, "TowerLoadout")
local slots = {}
for index = 1, 5 do
    local control = button(loadout, "TowerSlot" .. index, "", function()
        if not state.paused then command(Play.SelectSlot, index) end
    end)
    local stripe = frame(control, "SlotStripe" .. index, colors.line)
    stripe:SetMouseEnabled(false)
    local name = text(control, "SlotName" .. index, "Empty", 16)
    local price = text(control, "SlotPrice" .. index, "$0", 16, colors.gold)
    local number = text(control, "SlotNumber" .. index, tostring(index), 16, colors.muted)
    slots[index] = {root = control, stripe = stripe, name = name, price = price, number = number}
end
local placement = frame(root, "PlacementStatus", colors.panel)
local placementText = text(placement, "PlacementText", "", 16, colors.accent)
local cancelPlacement = button(placement, "CancelPlacement", "x", function() command(Play.CancelPlacement) end)
cancelPlacement:SetTooltip("Cancel placement")

local profile = frame(root, "SelectionProfile", colors.panel)
local profileTitle = text(profile, "ProfileTitle", "", 24, colors.ink, true)
local profileClose = button(profile, "CloseProfile", "x", function() command(Play.ClearSelection) end)
profileClose:SetTooltip("Close profile")
local profileScroll = UI.CreateFrame("ScrollContainer", "ProfileScroll", profile)
local profileContent = profileScroll:GetContent()
local profileBio = text(profileContent, "ProfileBio", "", 16, colors.muted)
local profileStats = text(profileContent, "ProfileStats", "", 16, colors.ink)
local profileEffects = text(profileContent, "ProfileEffects", "", 16, colors.accent)
local upgradeTitle = text(profileContent, "UpgradeTitle", "TALENTS", 16, colors.gold)
local talentTree = frame(profileContent, "TalentTree", 0x0B1012FF)
local talentLinks = frame(talentTree, "TalentLinks")
talentLinks:SetAllPoints(talentTree)
talentLinks:SetMouseEnabled(false)
local upgrades = {}
local connections = {}
local sell, sellLabel = button(profile, "SellTower", "Sell", function()
    if state.selection and state.selection.owned and not state.paused then command(Play.Sell) end
end, true)

local function layoutTalents(nodes, availableWidth, archetype)
    local artwork = ({archer_hut = "archer", mage_tower = "mage"})[archetype]
    local byId, layers, visiting = {}, {}, {}
    local deepest, widest, purchased = 0, 1, 0
    for index, node in ipairs(nodes) do
        byId[node.id] = {node = node, index = index}
        purchased = purchased + node.level
    end
    local function depth(entry)
        if entry.depth then return entry.depth end
        if visiting[entry.node.id] then return 0 end
        visiting[entry.node.id] = true
        local result = 0
        for _, required in ipairs(entry.node.requires or {}) do
            if byId[required] then result = math.max(result, depth(byId[required]) + 1) end
        end
        visiting[entry.node.id] = nil
        entry.depth = result
        return result
    end
    for _, node in ipairs(nodes) do
        local entry = byId[node.id]
        local rank = depth(entry)
        layers[rank] = layers[rank] or {}
        table.insert(layers[rank], entry)
        deepest = math.max(deepest, rank)
        widest = math.max(widest, #layers[rank])
    end
    local span = math.max(availableWidth, widest * 80 + 24)
    local treeHeight = #nodes > 0 and (deepest * 76 + 80) or 0
    for rank = 0, deepest do
        local layer = layers[rank] or {}
        for _, entry in ipairs(layer) do
            local total, count = 0, 0
            for _, required in ipairs(entry.node.requires or {}) do
                local parent = byId[required]
                if parent and parent.x then total, count = total + parent.x, count + 1 end
            end
            entry.order = count > 0 and total / count or entry.index
        end
        table.sort(layer, function(left, right)
            if left.order == right.order then return left.index < right.index end
            return left.order < right.order
        end)
        for column, entry in ipairs(layer) do
            entry.x = (span - #layer * 80) / 2 + (column - 1) * 80 + 14
            entry.y = 12 + rank * 76
        end
    end
    local linkCount = 0
    local function segment(left, top, width, height, color)
        linkCount = linkCount + 1
        if not connections[linkCount] then
            connections[linkCount] = frame(talentLinks, "TalentLink" .. linkCount)
            connections[linkCount]:SetMouseEnabled(false)
        end
        local control = connections[linkCount]
        control:SetVisible(true)
        control:SetBackgroundColor(color)
        place(control, talentLinks, left, top, math.max(2, width), math.max(2, height))
    end
    for index, node in ipairs(nodes) do
        local entry = byId[node.id]
        for _, required in ipairs(node.requires or {}) do
            local parent = byId[required]
            if parent then
                local fromX, fromY = parent.x + 26, parent.y + 52
                local toX, toY = entry.x + 26, entry.y
                local middle = (fromY + toY) / 2
                local color = parent.node.level > 0 and (node.level > 0 and colors.gold or 0x8A794AFF) or 0x394247FF
                segment(fromX - 1, fromY, 2, middle - fromY, color)
                segment(math.min(fromX, toX) - 1, middle, math.abs(toX - fromX) + 2, 2, color)
                segment(toX - 1, middle, 2, toY - middle, color)
            end
        end
        if not upgrades[index] then
            local control = button(talentTree, "Upgrade" .. index, "", function()
                local current = state.selection and state.selection.upgrades and state.selection.upgrades[index]
                if current and current.enabled and state.selection.owned and not state.paused then
                    command(Play.Upgrade, current.id)
                end
            end)
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
            local levelLabel = text(badge, "TalentLevel" .. index, "", 16, colors.gold)
            levelLabel:SetWordWrap(false)
            upgrades[index] = {root = control, inset = inset, icon = icon, symbol = symbol,
                badge = badge, level = levelLabel}
        end
        local item = upgrades[index]
        local learned = node.level > 0
        local border = learned and colors.gold or (node.enabled and colors.accent or 0x515A5FFF)
        item.root:SetVisible(true)
        item.root:SetButtonColors(border, node.enabled and 0xF4D88BFF or 0x899397FF, colors.gold)
        place(item.root, talentTree, entry.x, entry.y, 52, 52)
        place(item.inset, item.root, 3, 3, 46, 46)
        place(item.symbol, item.inset, 6, 10, 40, 26)
        place(item.badge, item.root, 19, 37, 39, 22)
        place(item.level, item.badge, 4, 0, 35, 22)
        local source = node.icon or (artwork and ("assets/images/talents/" .. artwork .. "/" .. artwork .. "_" .. node.id .. ".png")) or ""
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
    for index = linkCount + 1, #connections do connections[index]:SetVisible(false) end
    for index = #nodes + 1, #upgrades do
        upgrades[index].root:SetVisible(false)
    end
    upgradeTitle:SetText("TALENTS   " .. purchased .. " RANKS")
    return span, treeHeight
end

local chat = frame(root, "MatchChat", colors.panel)
local chatTitle = text(chat, "ChatTitle", "PARTY CHANNEL", 16, colors.accent)
local chatScroll = UI.CreateFrame("ScrollContainer", "ChatScroll", chat)
local chatText = text(chatScroll:GetContent(), "ChatMessages", "", 16)
local chatInput = UI.CreateFrame("EditBox", "ChatInput", chat)
chatInput:SetFont("Inter-Regular", 16)
chatInput:SetBackgroundColor(0x090E0DFF)
local function sendChat()
    if command(Play.SendChat, chatInput:GetText()) then chatInput:SetText("") end
end
chatInput:SetScript("OnEnterPressed", sendChat)
local send = button(chat, "SendChat", "Send", sendChat)

local status = frame(root, "MatchStatus", 0x04080788)
status:SetAllPoints(root)
local statusPanel = frame(status, "StatusPanel", colors.panel)
local statusKicker = text(statusPanel, "StatusKicker", "FIELD STATUS", 16, colors.accent)
local statusTitle = text(statusPanel, "StatusTitle", "Loading battlefield", 32, colors.ink, true)
local statusCopy = text(statusPanel, "StatusCopy", "", 16, colors.muted)
local statusProgress = UI.CreateFrame("ProgressBar", "LoadingProgress", statusPanel)
statusProgress:SetMinMaxValues(0, 1)
local statusReason = text(statusPanel, "StatusReason", "", 16, colors.gold)
local start = button(statusPanel, "StartMatch", "Start match", function()
    if state.canStart and not state.paused then command(Play.Start) end
end, true)
local retry = button(statusPanel, "RetryLoad", "Retry", function() command(Play.Retry) end, true)
local replay = button(statusPanel, "ReplayMatch", "Play again", function()
    if not state.client then command(Play.Restart) end
end, true)
local returnLobby = button(statusPanel, "ReturnLobby", "Return to lobby", function() command(Play.Lobby) end)

local pause = frame(root, "PauseOverlay", 0x030605CE)
pause:SetAllPoints(root)
local pausePanel = frame(pause, "PausePanel", colors.panel)
local pauseKicker = text(pausePanel, "PauseKicker", "MATCH MENU", 16, colors.accent)
local pauseTitle = text(pausePanel, "PauseTitle", "Paused", 32, colors.ink, true)
local volumeControls = {}
for index, entry in ipairs({{"masterVolume", "Master volume"}, {"musicVolume", "Music volume"}, {"sfxVolume", "SFX volume"}}) do
    local label = text(pausePanel, entry[1] .. "Label", entry[2])
    local value = text(pausePanel, entry[1] .. "Value", "100%", 16, colors.gold)
    local slider = UI.CreateFrame("StatusBar", entry[1] .. "Slider", pausePanel)
    slider:SetMinMaxValues(0, 1)
    slider:SetScript("OnValueChanged", function()
        if not refreshing then command(Play.SetVolume, entry[1], slider:GetValue()) end
    end)
    volumeControls[index] = {name = entry[1], label = label, value = value, slider = slider}
end
local pauseFeedback = text(pausePanel, "PauseFeedback", "", 16, colors.gold)
local resume = button(pausePanel, "ResumeMatch", "Resume", function() command(Play.Pause, false) end, true)
local pauseLobby = button(pausePanel, "PauseLobby", "Back to lobby", function() command(Play.Lobby) end)

function PlayPointerOverHud(mouseX, mouseY)
    if state.paused or state.phase ~= "running" then return true end
    for _, widget in ipairs({stats, gold, menu, chatToggle, countdown, loadout, placement, profile, chat}) do
        if widget:IsVisible() then
            local left, top, width, height = widget:GetRect()
            if mouseX >= left and mouseX < left + width and mouseY >= top and mouseY < top + height then return true end
        end
    end
    return false
end

refresh = function()
    refreshing = true
    state = Play.State()
    local _, _, width, height = UI.Root:GetRect()
    if width <= 0 or height <= 0 then refreshing = false; return end
    local compact = width < 1000
    local inspectingTower = state.selection and state.selection.kind == "tower"
    local slotWidth = math.min(148, (width - 48) / 5)
    local slotHeight = height < 700 and 136 or 172
    local loadoutWidth = slotWidth * 5 + 32
    local loadoutTop = height - slotHeight - 12
    place(stats, root, 12, 12, 270, 64)
    for index = 1, 3 do
        place(statValues[index], stats, 12 + (index - 1) * 88, 4, 84, 30)
        place(statLabels[index], stats, 12 + (index - 1) * 88, 36, 84, 22)
    end
    statValues[1]:SetText(tostring(math.ceil(state.health or 0)))
    statValues[2]:SetText(string.format("%d/%d", math.min(state.wave or 1, state.waveCount or 1), state.waveCount or 0))
    statValues[3]:SetText(tostring(state.enemies or 0))
    place(level, root, 20, 84, 260, 24)
    level:SetText(state.level or "")
    level:SetVisible(not compact)
    place(gold, root, width - 192, 12, 124, 64)
    place(goldCaption, gold, 12, 5, 100, 22)
    place(goldValue, gold, 12, 28, 108, 30)
    goldValue:SetText("$" .. math.floor(state.money or 0))
    place(menu, root, width - 56, 12, 44, 40)
    place(chatToggle, root, width - 88, 84, 76, 38)
    chatToggle:SetVisible(state.online and compact and not state.paused and state.phase == "running")

    place(countdown, root, (width - 220) / 2, compact and 84 or 12, 220, 76)
    place(countdownLabel, countdown, 12, 8, 148, 24)
    place(countdownValue, countdown, 170, 8, 44, 44)
    place(countdownBar, countdown, 12, 60, 196, 4)
    countdown:SetVisible(state.countdownVisible and not state.paused)
    countdownLabel:SetText(state.countdownLabel or "")
    countdownValue:SetText(tostring(state.countdown or 0))
    countdownBar:SetValue(state.countdownProgress or 0)

    place(loadout, root, (width - loadoutWidth) / 2, loadoutTop, loadoutWidth, slotHeight)
    loadout:SetVisible(state.loadoutVisible and not state.paused and not inspectingTower)
    for index, slot in ipairs(slots) do
        local tower = (state.slots or {})[index] or {name = "Empty", cost = 0}
        local selected = state.selectedSlot == index
        place(slot.root, loadout, (index - 1) * (slotWidth + 8), 0, slotWidth, slotHeight)
        place(slot.stripe, slot.root, 0, slotHeight - 3, slotWidth, 3)
        place(slot.name, slot.root, 10, 8, slotWidth - 20, 40)
        place(slot.price, slot.root, 10, slotHeight - 28, slotWidth - 40, 24)
        place(slot.number, slot.root, slotWidth - 24, slotHeight - 28, 20, 24)
        slot.name:SetText(tower.name)
        slot.price:SetText(tower.available and ("$" .. tower.cost) or "")
        slot.price:SetColor((state.money or 0) >= tower.cost and colors.gold or 0xEF9A86FF)
        slot.root:SetButtonColors(selected and colors.selected or colors.panel, colors.hover, colors.selected)
        slot.stripe:SetBackgroundColor(selected and colors.accent or colors.line)
        slot.root:SetTooltip(tower.bio or tower.name)
        slot.root:SetMouseEnabled(tower.available and state.phase == "running" and not state.paused)
        local left, top = slot.root:GetRect()
        Play.Preview(index, math.max(0, left + 8), math.max(0, top + 44),
            inspectingTower and 0 or slotWidth - 16, inspectingTower and 0 or slotHeight - 78)
    end
    place(placement, root, (width - math.min(460, width - 32)) / 2, loadoutTop - 46, math.min(460, width - 32), 38)
    place(placementText, placement, 12, 8, math.min(460, width - 32) - 60, 24)
    place(cancelPlacement, placement, math.min(460, width - 32) - 38, 2, 34, 34)
    placement:SetVisible((state.selectedSlot or 0) > 0 and not state.paused and not inspectingTower and state.phase == "running")
    local selectedSlot = (state.slots or {})[state.selectedSlot or 0]
    placementText:SetText(feedback ~= "" and feedback or (state.placement ~= "" and state.placement) or (selectedSlot and selectedSlot.name or ""))
    placementText:SetColor(state.canPlace and colors.accent or colors.gold)

    local selection = state.selection
    profile:SetVisible(selection ~= nil and not state.paused and state.phase == "running")
    local profileWidth = math.min(560, width - 24)
    local profileHeight = math.max(120, inspectingTower and height - 96 or loadoutTop - 104)
    place(profile, root, width - profileWidth - 12, 84, profileWidth, profileHeight)
    place(profileTitle, profile, 16, 12, profileWidth - 76, 36)
    place(profileClose, profile, profileWidth - 48, 8, 36, 36)
    local tower = selection and selection.kind == "tower"
    place(profileScroll, profile, 12, 56, profileWidth - 24, profileHeight - (tower and 124 or 68))
    if selection then
        local selectionKey = selection.kind .. tostring(selection.id)
        if selectionKey ~= previousSelection then profileScroll:SetScrollOffset(0, 0) end
        previousSelection = selectionKey
        profileTitle:SetText(selection.name)
        local inner = profileWidth - 48
        local treeWidth, treeHeight = layoutTalents(selection.upgrades or {}, inner + 8, selection.archetype)
        talentTree:SetVisible(tower)
        upgradeTitle:SetVisible(tower)
        place(upgradeTitle, profileContent, 4, 0, inner, 24)
        place(talentTree, profileContent, 0, 32, treeWidth, treeHeight)
        local infoTop = tower and treeHeight + 48 or 0
        local bioHeight = linesHeight(selection.bio or "", inner)
        place(profileBio, profileContent, 4, infoTop, inner, bioHeight)
        profileBio:SetText(selection.bio or "")
        local detail, effects
        if selection.kind == "tower" then
            detail = string.format("DAMAGE %.1f     RANGE %.1f\nRATE %.2f/s     SPENT $%d\n%s   AP %.1f",
                selection.damage, selection.range, selection.rate, selection.spent, selection.damageType, selection.armorPiercing)
            effects = selection.effects or ""
        else
            detail = string.format("HEALTH %.0f / %.0f\nSHIELD %.0f / %.0f\nARMOR %.1f     SPEED %.1f\n$%.0f reward     %.0f base damage",
                selection.health, selection.maxHealth, selection.shield, selection.maxShield, selection.armor, selection.speed,
                selection.reward, selection.baseDamage)
            effects = "RESISTANCES\n" .. (selection.resistances or "No resistances")
        end
        local detailHeight = linesHeight(detail, inner)
        place(profileStats, profileContent, 4, infoTop + bioHeight + 12, inner, detailHeight)
        profileStats:SetText(detail)
        local effectsTop = infoTop + bioHeight + detailHeight + 28
        local effectsHeight = effects == "" and 0 or linesHeight(effects, inner)
        place(profileEffects, profileContent, 4, effectsTop, inner, effectsHeight)
        profileEffects:SetText(effects)
        local nextTop = effectsTop + effectsHeight + 12
        sell:SetVisible(tower)
        if tower then
            place(sell, profile, 12, profileHeight - 54, profileWidth - 24, 42)
            enabled(sell, selection.owned, true)
            sellLabel:SetText(selection.owned and ("Sell for $" .. selection.sell) or "Owned by another player")
        end
        profileScroll:SetContentSize(tower and treeWidth or inner + 8, nextTop)
    end

    local chatWidth, chatHeight = math.min(340, width - 24), math.min(200, loadoutTop - 100)
    place(chat, root, 12, loadoutTop - chatHeight - 12, chatWidth, chatHeight)
    chat:SetVisible(state.online and state.phase == "running" and not state.paused and (not compact or (chatOpen and not selection)))
    place(chatTitle, chat, 12, 8, chatWidth - 24, 24)
    place(chatScroll, chat, 8, 36, chatWidth - 16, chatHeight - 88)
    place(chatInput, chat, 8, chatHeight - 44, chatWidth - 88, 36)
    place(send, chat, chatWidth - 72, chatHeight - 44, 64, 36)
    local chatValue = table.concat(state.chat or {}, "\n")
    local chatTextHeight = linesHeight(chatValue, chatWidth - 40)
    place(chatText, chatScroll:GetContent(), 4, 0, chatWidth - 40, chatTextHeight)
    chatScroll:SetContentSize(chatWidth - 32, chatTextHeight)
    if chatValue ~= previousChat then
        chatText:SetText(chatValue)
        chatScroll:SetScrollOffset(0, math.max(0, chatTextHeight - chatHeight + 88))
        previousChat = chatValue
    end

    local terminal = state.phase == "victory" or state.phase == "defeat"
    local ready, loading, failed = state.phase == "ready", state.phase == "loading", state.phase == "failed"
    status:SetVisible((ready or loading or failed or terminal) and not state.paused)
    local panelWidth, panelHeight = math.min(520, width - 32), math.min(286, height - 32)
    place(statusPanel, status, (width - panelWidth) / 2, math.max(12, (height - panelHeight) / 2 - (ready and 48 or 0)), panelWidth, panelHeight)
    place(statusKicker, statusPanel, 24, 20, panelWidth - 48, 24)
    place(statusTitle, statusPanel, 24, 52, panelWidth - 48, 44)
    place(statusCopy, statusPanel, 24, 108, panelWidth - 48, 62)
    place(statusProgress, statusPanel, 24, 182, panelWidth - 48, 6)
    place(statusReason, statusPanel, 24, 174, panelWidth - 48, 42)
    statusKicker:SetText(ready and "BATTLEFIELD READY" or terminal and "DEPLOYMENT COMPLETE" or "FIELD STATUS")
    statusTitle:SetText(state.headline or "")
    statusCopy:SetText(state.description or "")
    statusProgress:SetVisible(loading)
    statusProgress:SetValue(state.loadingProgress or 0)
    statusReason:SetText(feedback ~= "" and feedback or (ready and state.startReason or (state.client and terminal and "Waiting for the host to replay." or "")))
    start:SetVisible(ready and not state.client)
    enabled(start, state.canStart, true)
    retry:SetVisible(failed)
    replay:SetVisible(terminal and not state.client)
    local actionWidth = (panelWidth - 60) / 2
    for _, widget in ipairs({start, retry, replay}) do place(widget, statusPanel, 24, panelHeight - 62, actionWidth, 42) end
    place(returnLobby, statusPanel, panelWidth - actionWidth - 24, panelHeight - 62, actionWidth, 42)

    pause:SetVisible(state.paused)
    local pauseWidth, pauseHeight = math.min(480, width - 32), math.min(412, height - 32)
    place(pausePanel, pause, (width - pauseWidth) / 2, (height - pauseHeight) / 2, pauseWidth, pauseHeight)
    place(pauseKicker, pausePanel, 24, 20, pauseWidth - 48, 24)
    place(pauseTitle, pausePanel, 24, 52, pauseWidth - 48, 44)
    pauseTitle:SetText(state.online and "Match menu" or "Paused")
    for index, control in ipairs(volumeControls) do
        local top = 112 + (index - 1) * 68
        place(control.label, pausePanel, 24, top, pauseWidth - 120, 24)
        place(control.value, pausePanel, pauseWidth - 88, top, 64, 24)
        place(control.slider, pausePanel, 24, top + 30, pauseWidth - 48, 22)
        control.slider:SetValue(state[control.name] or 0)
        control.value:SetText(math.floor((state[control.name] or 0) * 100 + 0.5) .. "%")
    end
    place(pauseFeedback, pausePanel, 24, 320, pauseWidth - 48, 24)
    pauseFeedback:SetText(feedback)
    place(resume, pausePanel, 24, pauseHeight - 62, (pauseWidth - 60) / 2, 42)
    place(pauseLobby, pausePanel, pauseWidth / 2 + 6, pauseHeight - 62, (pauseWidth - 60) / 2, 42)
    refreshing = false
end

local elapsed, previousWidth, previousHeight = 1, 0, 0
function OnUpdate(dt)
    elapsed = elapsed + (dt or 0)
    local _, _, width, height = UI.Root:GetRect()
    if elapsed < 0.05 and width == previousWidth and height == previousHeight then return end
    elapsed, previousWidth, previousHeight = 0, width, height
    refresh()
end
OnUpdate(0)