local CLICK_SFX = "assets/audio/click.ogg"
local HOVER_SFX = "assets/audio/hover.ogg"
local entered, assetsPreloaded = false, false

local function preloadAssets()
    if assetsPreloaded then return end
    Audio.Preload(CLICK_SFX, "Sfx")
    Audio.Preload(HOVER_SFX, "Sfx")
    assetsPreloaded = true
end

local colors = {
    ink = 0xEDF0E8FF, heading = 0xF3F0DFFF, muted = 0xA8B0AAFF,
    accent = 0x83B9A4FF, gold = 0xE1BD67FF, line = 0x38443EFF,
    button = 0x1D2925FF, hover = 0x2A3B35FF, selected = 0x20382FFF,
    red = 0x9A3F32FF, redHover = 0xB24B3BFF, disabled = 0x202522FF
}
local state = Lobby.State()
local feedback, lastRole = "", state.role
local refreshing, pendingLevel, carouselStart = false, state.selectedLevel, 1
local modal, refresh
local refreshQueued = true
local function requestRefresh()
    refreshQueued = true
end

local function place(widget, parent, left, top, width, height)
    widget:SetSize(math.max(0, width), math.max(0, height))
    widget:SetPoint("TOPLEFT", parent, "TOPLEFT", left, top)
end

local function frame(parent, name, color)
    local control = UI.CreateFrame("Frame", name, parent)
    if color then control:SetBackgroundColor(color) end
    return control
end

local function text(parent, name, value, size, color, serif)
    local label = parent:CreateFontString(name)
    label:SetText(value)
    label:SetFont(serif and "timesbd" or "Inter-Regular", size or 16)
    label:SetColor(color or colors.ink)
    label:SetMouseEnabled(false)
    label:SetWordWrap(true)
    return label
end

local function line(parent, name)
    local control = frame(parent, name, colors.line)
    control:SetMouseEnabled(false)
    return control
end

local function command(action, ...)
    local success, message = action(...)
    feedback = message or (success == false and "Unable to complete action" or "")
    if refresh then requestRefresh() end
    return success
end

local function button(parent, name, caption, action, primary, inModal)
    local control = UI.CreateFrame("Button", name, parent)
    control:SetButtonColors(primary and colors.red or colors.button,
        primary and colors.redHover or colors.hover, colors.selected)
    local label = text(control, name .. "Label", caption, primary and 24 or 16, colors.ink, primary)
    label:SetWordWrap(false)
    label:SetPoint("TOPLEFT", control, "TOPLEFT", 14, 8)
    control:SetScript("OnEnter", function() Audio.Play(HOVER_SFX, "Sfx") end)
    control:SetScript("OnClick", function()
        if modal and modal:IsVisible() and not inModal then return end
        Audio.Play(CLICK_SFX, "Sfx")
        action()
    end)
    return control, label
end

local function edit(parent, name, value)
    local control = UI.CreateFrame("EditBox", name, parent)
    control:SetFont("Inter-Regular", 16)
    control:SetBackgroundColor(0x0C1211FF)
    control:SetText(value)
    return control
end

local root = frame(nil, "LobbyRoot", 0x090C0DFF)
root:SetAllPoints(UI.Root)
local background = root:CreateImage("LobbyBackground")
background:SetAllPoints(root)
background:SetFit("COVER")
background:SetMouseEnabled(false)
background:SetSource("assets/images/splash_screen.png")
local shade = frame(root, "LobbyShade", 0x050908C2)
shade:SetAllPoints(root)
shade:SetMouseEnabled(false)

local header = frame(root, "LobbyHeader", 0x0B0F10E8)
local headerLine = line(header, "HeaderLine")
local back = button(header, "BackButton", "<", function() Scene.GoTo("MainMenu") end)
back:SetTooltip("Back to main menu")
local brand = text(header, "LobbyBrand", "NODE SPIRE TD", 16, colors.accent)
local title = text(header, "LobbyTitle", "Match", 32, colors.heading, true)
local connection = text(header, "LobbyConnection", "Solo", 16, colors.muted)
local capacity = text(header, "LobbyCapacity", "", 16, colors.gold)

local scroll = UI.CreateFrame("ScrollContainer", "LobbyScroll", root)
local content = scroll:GetContent()
local deployment = frame(content, "DeploymentRail")
local briefing = frame(content, "BriefingRail", 0x101716E8)
local loadout = frame(content, "LoadoutRail")
local centerLeft, centerRight = line(briefing, "BriefingLeftLine"), line(briefing, "BriefingRightLine")

local deploymentKicker = text(deployment, "DeploymentKicker", "DEPLOYMENT", 16, colors.accent)
local deploymentTitle = text(deployment, "DeploymentTitle", "Start your adventure", 32, colors.heading, true)
local deploymentCopy = text(deployment, "DeploymentCopy", "Take the first watch alone, or build a party before entering the Spire.", 16, colors.muted)
local start, startLabel = button(deployment, "StartButton", "Start solo", function()
    if state.canStart then command(Lobby.Start) end
end, true)
local startNote = text(start, "StartNote", "Immediate deployment", 16, 0xF3C4B7FF)
local divider = text(deployment, "PartyDivider", "OR PARTY UP", 16, colors.accent)
local setup = frame(deployment, "PartySetup")
local nameCaption = text(setup, "PlayerNameCaption", "CALLSIGN", 16, colors.accent)
local nameInput = edit(setup, "PlayerName", state.displayName)
local host = button(setup, "HostButton", "Create party", function() command(Lobby.Host, nameInput:GetText()) end)
local address = edit(setup, "JoinAddress", "127.0.0.1")
address:SetTooltip("Host address / LAN port 47321")
local function joinParty() command(Lobby.Join, nameInput:GetText(), address:GetText()) end
local join = button(setup, "JoinButton", "Join", joinParty)
address:SetScript("OnEnterPressed", function() if not modal:IsVisible() then joinParty() end end)

local active = frame(deployment, "ActiveParty")
local partyRole = text(active, "PartyRole", "Party leader", 24, colors.heading, true)
local roster = {}
for index = 1, state.capacity or 4 do
    local row = frame(active, "MemberRow" .. index, 0x111917FF)
    local stripe = line(row, "MemberStripe" .. index)
    local name = text(row, "MemberName" .. index, "Open slot", 16, colors.muted)
    local readyState = text(row, "MemberState" .. index, "WAITING", 16, 0xD98774FF)
    local kick = button(row, "Kick" .. index, "x", function()
        local member = state.members[index]
        if member and state.role == "Host" then command(Lobby.Kick, member.id) end
    end)
    kick:SetTooltip("Remove player from party")
    roster[index] = {root = row, stripe = stripe, name = name, state = readyState, kick = kick}
end
local ready = UI.CreateFrame("CheckBox", "ReadyToggle", active)
ready:SetScript("OnValueChanged", function()
    if not refreshing and not modal:IsVisible() then command(Lobby.Ready, ready:IsChecked()) end
end)
local readyLabel = text(active, "ReadyLabel", "Ready", 16, colors.accent)
local leave, leaveLabel = button(active, "LeaveButton", "Leave party", function() command(Lobby.Leave) end)

local briefingKicker = text(briefing, "BriefingKicker", "SELECTED LEVEL", 16, colors.accent)
local levelName = text(briefing, "SelectedLevelName", "", 32, colors.heading, true)
local levelDescription = text(briefing, "SelectedLevelDescription", "", 16, colors.muted)
local statsLine = line(briefing, "ContractStatsLine")
local stats = {}
for index, caption in ipairs({"THREAT", "PLAYERS", "WAVES"}) do
    stats[index] = {value = text(briefing, caption .. "Value", "", 16, colors.gold),
        caption = text(briefing, caption .. "Caption", caption, 16, colors.muted)}
end
local chooseLevel = button(briefing, "ChooseLevelButton", "Choose another level", function()
    if #state.levels == 0 or state.role == "Client" or state.activeMatch then return end
    pendingLevel, carouselStart = state.selectedLevel, math.max(1, state.selectedLevel - 1)
    modal:SetVisible(true)
    refresh()
end)

local chat = frame(briefing, "PartyChat")
local chatTitle = text(chat, "ChatTitle", "Party channel", 24, colors.heading, true)
local chatLive = text(chat, "ChatLive", "LIVE", 16, colors.gold)
local chatScroll = UI.CreateFrame("ScrollContainer", "ChatScroll", chat)
chatScroll:SetBackgroundColor(0x090E0DDD)
local chatText = text(chatScroll:GetContent(), "ChatMessages", "", 16, colors.ink)
local chatInput = edit(chat, "ChatInput", "")
local function sendChat()
    if command(Lobby.SendChat, chatInput:GetText()) then chatInput:SetText("") end
end
chatInput:SetScript("OnEnterPressed", function() if not modal:IsVisible() then sendChat() end end)
local send = button(chat, "SendButton", "Send", sendChat)

local loadoutKicker = text(loadout, "LoadoutKicker", "TOWER LOADOUT", 16, colors.accent)
local loadoutCount = text(loadout, "LoadoutCount", "", 16, colors.gold)
local loadoutTitle = text(loadout, "LoadoutTitle", "Choose your defenses", 24, colors.heading, true)
local loadoutCopy = text(loadout, "LoadoutCopy", "Select up to five towers to take into the next match.", 16, colors.muted)
local slots, towerCards, towerById = {}, {}, {}
for index = 1, state.maxTowers or 5 do
    local slot = frame(loadout, "LoadoutSlot" .. index, 0x0C1211FF)
    local stripe = line(slot, "LoadoutStripe" .. index)
    local number = text(slot, "SlotNumber" .. index, tostring(index), 16, colors.gold)
    local name = text(slot, "SlotName" .. index, "Empty slot", 16, colors.muted)
    slots[index] = {root = slot, stripe = stripe, number = number, name = name}
end
local inventoryCaption = text(loadout, "InventoryCaption", "INVENTORY", 16, colors.accent)
local inventory = UI.CreateFrame("ScrollContainer", "TowerInventory", loadout)
for index, tower in ipairs(state.towers) do
    towerById[tower.id] = tower
    local control = button(inventory:GetContent(), "Tower" .. index, "", function() command(Lobby.ToggleTower, tower.id) end)
    control:SetTooltip(tower.bio or tower.name)
    local portrait = control:CreateImage("TowerPortrait" .. index)
    portrait:SetFit("CONTAIN")
    portrait:SetMouseEnabled(false)
    local loaded = tower.portrait and tower.portrait ~= "" and portrait:SetSource(tower.portrait)
    local glyph = text(control, "TowerGlyph" .. index, "T", 24, colors.gold, true)
    glyph:SetVisible(not loaded)
    local name = text(control, "TowerName" .. index, tower.name, 16)
    local cost = text(control, "TowerCost" .. index, "$" .. tostring(tower.cost or 0), 16, colors.gold)
    local stripe = line(control, "TowerSelected" .. index)
    towerCards[index] = {root = control, portrait = portrait, glyph = glyph, name = name, cost = cost, stripe = stripe}
end

local footer = frame(root, "LobbyFooter", 0x0B0F10E8)
local footerLine = line(footer, "FooterLine")
local status = text(footer, "LobbyStatus", "", 16, colors.muted)

modal = frame(root, "LevelSelector", 0x030605D9)
modal:SetAllPoints(root)
modal:SetVisible(false)
local modalPanel = frame(modal, "LevelSelectorPanel", 0x0E1513FF)
local modalKicker = text(modalPanel, "SelectorKicker", "MISSION BOARD", 16, colors.accent)
local modalTitle = text(modalPanel, "SelectorTitle", "Select a level", 32, colors.heading, true)
local modalRule = line(modalPanel, "SelectorRule")
local modalCount = text(modalPanel, "SelectorCount", "", 16, colors.muted)
local previous = button(modalPanel, "PreviousLevelButton", "<", function()
    carouselStart = math.max(1, carouselStart - 1)
    refresh()
end, false, true)
previous:SetTooltip("Previous levels")
local nextLevel = button(modalPanel, "NextLevelButton", ">", function()
    carouselStart = math.min(#state.levels, carouselStart + 1)
    refresh()
end, false, true)
nextLevel:SetTooltip("Next levels")
local cancel = button(modalPanel, "CancelLevelButton", "Cancel", function() modal:SetVisible(false) end, false, true)
local confirm = button(modalPanel, "ConfirmLevelButton", "Select level", function()
    if command(Lobby.SelectLevel, pendingLevel) then modal:SetVisible(false) end
end, true, true)
local levelCards = {}
for index, entry in ipairs(state.levels) do
    local card = button(modalPanel, "LevelCard" .. index, "", function()
        pendingLevel = index
        refresh()
    end, false, true)
    local image = card:CreateImage("LevelThumbnail" .. index)
    image:SetFit("COVER")
    image:SetMouseEnabled(false)
    if entry.thumbnail and entry.thumbnail ~= "" then image:SetSource(entry.thumbnail) end
    local name = text(card, "LevelCardName" .. index, entry.name, 24, colors.heading, true)
    local description = text(card, "LevelCardDescription" .. index, entry.description, 16, colors.muted)
    local meta = text(card, "LevelCardMeta" .. index, (entry.threat or "NORMAL") .. " / " .. tostring(entry.waves or "--") .. " WAVES", 16, colors.gold)
    local selection = line(card, "LevelCardSelection" .. index)
    levelCards[index] = {root = card, image = image, name = name, description = description, meta = meta, selection = selection}
end

local lastChat, lastChatWidth = "", 0
refresh = function()
    state = Lobby.State()
    refreshing = true
    if state.role ~= lastRole then feedback, lastRole = "", state.role end
    local _, _, width, height = UI.Root:GetRect()
    local margin = math.max(20, math.floor(width * 0.04))
    local headerHeight, footerHeight = 88, 56
    local mainWidth = width - margin * 2
    local wide = width >= 1000
    local railHeight = math.max(570, height - headerHeight - footerHeight)
    local leftWidth = wide and math.floor(mainWidth * 0.29) or mainWidth
    local centerWidth = wide and math.floor(mainWidth * 0.41) or mainWidth
    local rightWidth = wide and mainWidth - leftWidth - centerWidth or mainWidth
    local leftInner, centerInner, rightInner = leftWidth - 24, centerWidth - 48, rightWidth - 24
    local inParty = state.role ~= "Solo"

    place(header, root, 0, 0, width, headerHeight)
    place(headerLine, header, 0, headerHeight - 1, width, 1)
    place(back, header, margin, 21, 46, 46)
    place(brand, header, margin + 66, 16, 230, 22)
    place(title, header, margin + 66, 38, 230, 42)
    place(connection, header, width - margin - 228, 22, 228, 22)
    place(capacity, header, width - margin - 228, 48, 228, 22)
    connection:SetText(inParty and (state.role == "Host" and "Party leader" or "Party member") or "Solo deployment")
    capacity:SetText(inParty and string.format("%d / %d PLAYERS", #state.members, state.capacity or 4) or "")
    place(scroll, root, margin, headerHeight, mainWidth, height - headerHeight - footerHeight)
    scroll:SetContentSize(mainWidth, wide and railHeight or railHeight * 3)
    place(deployment, content, 0, 0, leftWidth, railHeight)
    place(briefing, content, wide and leftWidth or 0, wide and 0 or railHeight, centerWidth, railHeight)
    place(loadout, content, wide and leftWidth + centerWidth or 0, wide and 0 or railHeight * 2, rightWidth, railHeight)
    place(centerLeft, briefing, 0, 0, 1, railHeight)
    place(centerRight, briefing, centerWidth - 1, 0, 1, railHeight)

    place(deploymentKicker, deployment, 0, 24, leftInner, 22)
    place(deploymentTitle, deployment, 0, 52, leftInner, 72)
    place(deploymentCopy, deployment, 0, 120, leftInner, 52)
    place(start, deployment, 0, 184, leftInner, 72)
    place(startLabel, start, 16, 10, leftInner - 32, 30)
    place(startNote, start, 16, 42, leftInner - 32, 22)
    place(divider, deployment, 0, 272, leftInner, 22)
    startLabel:SetText(state.activeMatch and "Rejoin match" or state.role == "Client" and "Awaiting host" or inParty and "Start match" or "Start solo")
    startNote:SetText(state.activeMatch and "Return to deployment" or inParty and (state.canStart and "Party ready for deployment" or "Waiting for ready players") or "Immediate deployment")
    start:SetButtonColors(state.canStart and (state.activeMatch and colors.selected or colors.red) or colors.disabled,
        state.canStart and colors.redHover or colors.disabled, colors.selected)
    start:SetMouseEnabled(state.canStart)
    divider:SetText(inParty and "ACTIVE PARTY" or "OR PARTY UP")
    place(setup, deployment, 0, 310, leftInner, 180)
    setup:SetVisible(not inParty)
    place(nameCaption, setup, 0, 0, leftInner, 22)
    place(nameInput, setup, 0, 26, leftInner, 38)
    place(host, setup, 0, 76, leftInner, 42)
    place(address, setup, 0, 130, leftInner - 88, 38)
    place(join, setup, leftInner - 80, 130, 80, 38)
    place(active, deployment, 0, 306, leftInner, 264)
    active:SetVisible(inParty)
    partyRole:SetText(state.role == "Host" and "Party leader" or "Party member")
    place(partyRole, active, 0, 0, leftInner, 30)
    for index, row in ipairs(roster) do
        local member = state.members[index]
        place(row.root, active, 0, 36 + (index - 1) * 42, leftInner, 38)
        place(row.stripe, row.root, 0, 0, 3, 38)
        place(row.name, row.root, 10, 2, leftInner - 44, 20)
        place(row.state, row.root, 10, 20, leftInner - 44, 18)
        place(row.kick, row.root, leftInner - 34, 3, 30, 30)
        row.name:SetText(member and (member.name .. (member.localPlayer and " (you)" or "")) or "Open slot")
        row.state:SetText(member and ((member.host and "LEADER / " or "") .. (member.ready and "READY" or "NOT READY")) or "WAITING")
        row.state:SetColor(member and member.ready and 0x7BE0ADFF or 0xD98774FF)
        row.kick:SetVisible(state.role == "Host" and member ~= nil and not member.localPlayer)
        row.stripe:SetBackgroundColor(member and colors.accent or colors.line)
    end
    place(ready, active, 0, 216, 28, 32)
    place(readyLabel, active, 34, 222, 66, 22)
    place(leave, active, leftInner - 132, 212, 132, 38)
    ready:SetChecked(state.ready)
    leaveLabel:SetText(state.role == "Host" and "Disband party" or "Leave party")

    local selected = state.levels[state.selectedLevel]
    place(briefingKicker, briefing, 24, 24, centerInner, 22)
    place(levelName, briefing, 24, 56, centerInner, 44)
    place(levelDescription, briefing, 24, 112, centerInner, 76)
    levelName:SetText(selected and selected.name or "No levels available")
    levelDescription:SetText(selected and selected.description or "")
    place(statsLine, briefing, 24, 206, centerInner, 1)
    local values = {selected and selected.threat or "--", selected and selected.players or "1-4", selected and tostring(selected.waves) or "--"}
    for index, stat in ipairs(stats) do
        local columnWidth = centerInner / 3
        place(stat.value, briefing, 24 + (index - 1) * columnWidth, 222, columnWidth, 24)
        place(stat.caption, briefing, 24 + (index - 1) * columnWidth, 250, columnWidth, 22)
        stat.value:SetText(values[index])
    end
    place(chooseLevel, briefing, 24, 292, centerInner, 42)
    chooseLevel:SetMouseEnabled(#state.levels > 0 and state.role ~= "Client" and not state.activeMatch)
    chooseLevel:SetVisible(state.role ~= "Client" and not state.activeMatch)
    place(chat, briefing, 24, 354, centerInner, railHeight - 374)
    chat:SetVisible(inParty)
    place(chatTitle, chat, 0, 0, centerInner - 60, 30)
    place(chatLive, chat, centerInner - 48, 8, 48, 22)
    local chatHeight = railHeight - 462
    place(chatScroll, chat, 0, 38, centerInner, chatHeight)
    place(chatInput, chat, 0, chatHeight + 48, centerInner - 80, 38)
    place(send, chat, centerInner - 72, chatHeight + 48, 72, 38)
    local chatLines, lineCount = {}, 0
    local chatWidth = math.max(40, centerInner - 28)
    for _, entry in ipairs(state.chat) do
        table.insert(chatLines, entry)
        lineCount = lineCount + math.max(1, math.ceil(#entry / math.max(1, math.floor(chatWidth / 9))))
    end
    local chatValue = table.concat(chatLines, "\n")
    local textHeight = math.max(chatHeight, lineCount * 22 + 16)
    place(chatText, chatScroll:GetContent(), 8, 8, chatWidth, textHeight)
    chatScroll:SetContentSize(centerInner - 16, textHeight)
    if chatValue ~= lastChat or chatWidth ~= lastChatWidth then
        chatText:SetText(chatValue)
        chatScroll:SetScrollOffset(0, math.max(0, textHeight - chatHeight))
        lastChat, lastChatWidth = chatValue, chatWidth
    end

    local rightPad = wide and 24 or 0
    place(loadoutKicker, loadout, rightPad, 24, rightInner - 68, 22)
    place(loadoutCount, loadout, rightPad + rightInner - 64, 24, 64, 22)
    place(loadoutTitle, loadout, rightPad, 58, rightInner, 32)
    place(loadoutCopy, loadout, rightPad, 106, rightInner, 48)
    local selectedIds = state.loadout or {}
    if not state.loadout then
        for _, tower in ipairs(state.towers) do if tower.selected then table.insert(selectedIds, tower.id) end end
    end
    loadoutCount:SetText(string.format("%d / %d", #selectedIds, state.maxTowers or 5))
    for index, slot in ipairs(slots) do
        local tower = towerById[selectedIds[index]]
        place(slot.root, loadout, rightPad, 170 + (index - 1) * 32, rightInner, 28)
        place(slot.stripe, slot.root, 0, 0, 2, 28)
        place(slot.number, slot.root, 10, 4, 22, 22)
        place(slot.name, slot.root, 36, 4, rightInner - 44, 22)
        slot.name:SetText(tower and tower.name or "Empty slot")
        slot.name:SetColor(tower and colors.ink or colors.muted)
        slot.stripe:SetBackgroundColor(tower and colors.accent or colors.line)
    end
    place(inventoryCaption, loadout, rightPad, 350, rightInner, 22)
    place(inventory, loadout, rightPad, 382, rightInner, railHeight - 406)
    inventory:SetContentSize(rightInner - 16, #towerCards * 76)
    for index, card in ipairs(towerCards) do
        local tower = state.towers[index]
        local cardWidth = rightInner - 16
        place(card.root, inventory:GetContent(), 0, (index - 1) * 76, cardWidth, 68)
        place(card.portrait, card.root, 10, 10, 44, 44)
        place(card.glyph, card.root, 20, 20, 28, 30)
        place(card.name, card.root, 66, 10, cardWidth - 78, 26)
        place(card.cost, card.root, 66, 38, cardWidth - 78, 22)
        place(card.stripe, card.root, 0, 0, 3, 68)
        card.root:SetButtonColors(tower.selected and colors.selected or 0x111917FF, colors.hover, colors.selected)
        card.stripe:SetBackgroundColor(tower.selected and 0x69BC95FF or colors.line)
    end

    place(footer, root, 0, height - footerHeight, width, footerHeight)
    place(footerLine, footer, margin, 0, mainWidth, 1)
    place(status, footer, margin, 12, mainWidth, 42)
    status:SetText(feedback ~= "" and feedback or state.status ~= "" and state.status or "Choose solo play or form a party.")
    status:SetColor(feedback ~= "" and 0xF3C4B7FF or colors.muted)

    if modal:IsVisible() then
        local panelWidth = math.min(1480, width - 48)
        local panelHeight = math.min(760, height - 48)
        local visible = math.min(#levelCards, panelWidth >= 1000 and 3 or panelWidth >= 720 and 2 or 1)
        carouselStart = math.max(1, math.min(carouselStart, #levelCards - visible + 1))
        place(modalPanel, modal, (width - panelWidth) / 2, (height - panelHeight) / 2, panelWidth, panelHeight)
        place(modalKicker, modalPanel, 28, 20, panelWidth - 56, 22)
        place(modalTitle, modalPanel, 28, 48, panelWidth - 56, 42)
        place(modalRule, modalPanel, 28, 100, panelWidth - 56, 1)
        place(modalCount, modalPanel, 28, panelHeight - 48, panelWidth - 370, 24)
        modalCount:SetText(string.format("%d DEPLOYMENTS", #levelCards))
        modalCount:SetVisible(panelWidth >= 720)
        place(previous, modalPanel, 8, panelHeight / 2 - 24, 40, 48)
        place(nextLevel, modalPanel, panelWidth - 48, panelHeight / 2 - 24, 40, 48)
        previous:SetMouseEnabled(carouselStart > 1)
        nextLevel:SetMouseEnabled(carouselStart + visible <= #levelCards)
        previous:SetVisible(carouselStart > 1)
        nextLevel:SetVisible(carouselStart + visible <= #levelCards)
        place(cancel, modalPanel, panelWidth - 316, panelHeight - 62, 124, 42)
        place(confirm, modalPanel, panelWidth - 180, panelHeight - 62, 152, 42)
        local cardWidth = (panelWidth - 112 - math.max(0, visible - 1) * 16) / math.max(1, visible)
        local cardHeight = panelHeight - 196
        for index, card in ipairs(levelCards) do
            local shown = index >= carouselStart and index < carouselStart + visible
            card.root:SetVisible(shown)
            if shown then
                place(card.root, modalPanel, 56 + (index - carouselStart) * (cardWidth + 16), 116, cardWidth, cardHeight)
                local imageHeight = math.max(72, math.floor(cardHeight * 0.44))
                place(card.image, card.root, 0, 0, cardWidth, imageHeight)
                place(card.name, card.root, 14, imageHeight + 12, cardWidth - 28, 32)
                place(card.description, card.root, 14, imageHeight + 54, cardWidth - 28, cardHeight - imageHeight - 98)
                card.description:SetVisible(cardHeight >= 300)
                place(card.meta, card.root, 14, cardHeight - 34, cardWidth - 28, 24)
                place(card.selection, card.root, 0, cardHeight - 3, cardWidth, 3)
                card.selection:SetBackgroundColor(index == pendingLevel and 0x69BC95FF or colors.line)
                card.root:SetButtonColors(index == pendingLevel and colors.selected or 0x121A17FF, colors.hover, colors.selected)
            end
        end
    end
    refreshQueued = false
    refreshing = false
end

local previousWidth, previousHeight = 0, 0
function OnEnter()
    preloadAssets()
    entered = true
    previousWidth, previousHeight = 0, 0
    refreshQueued = true
    refresh()
end

function OnExit()
    entered = false
end

function OnShortcut(scanCode)
    if scanCode == keys.ESCAPE then
        Audio.Play(CLICK_SFX, "Sfx")
        Scene.GoTo("MainMenu")
        return true
    end
    return false
end

function OnUpdate(dt)
    if not entered then return end
    local _, _, width, height = UI.Root:GetRect()
    if not refreshQueued and width == previousWidth and height == previousHeight then return end
    if refreshQueued or width ~= previousWidth or height ~= previousHeight then
        previousWidth, previousHeight = width, height
        refresh()
    end
end