local CLICK, HOVER = "assets/audio/click.ogg", "assets/audio/hover.ogg"

-- Scene lifecycle used by LuaUiScene:
-- 1) script load: defines helpers/widgets/functions
-- 2) OnEnter(): preload assets and initialize frame-driven state
-- 3) OnUpdate(dt): periodic refresh for dynamic values/layout
-- 4) OnShortcut(scanCode): optional scene-level keyboard handling
-- 5) OnExit(): cleanup scene-owned transient state

local colors = {
    ink = 0xEDF0E8FF, muted = 0xA8B0AAFF, gold = 0xE1BD67FF, accent = 0x83B9A4FF,
    panel = 0x101716F5, line = 0x38443EFF, button = 0x1D2925FF, hover = 0x2A3B35FF,
    selected = 0x305542FF, disabled = 0x202522FF, red = 0x9A3F32FF, redHover = 0xB24B3BFF
}
local state = Play.State()
local applyState, feedback, refreshing = nil, "", false
local refreshCount = 0
local uiProfileEnv = os.getenv("NODESPIRE_UI_PROFILE") or ""
local uiProfileEnabled = uiProfileEnv ~= "" and uiProfileEnv ~= "0"
local uiProfileWindowStart = os.clock()
local uiProfileUpdateCalls = 0
local uiProfileRefreshCalls = 0
local uiProfileRefreshSeconds = 0
local uiProfileProfileRebuilds = 0
local uiProfileProfileSkips = 0
local chatOpen, previousChat, previousSelection = false, "", ""
local entered, assetsPreloaded = false, false
local profileContentSignature = nil
local sceneDirectory = debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])") or ""
local createBindings = dofile(sceneDirectory .. "UiBindings.lua")
local bindings = createBindings()

local function reportUiProfile()
    if not uiProfileEnabled then return end
    local now = os.clock()
    local elapsed = now - uiProfileWindowStart
    if elapsed < 1 then return end
    local refreshRate = uiProfileRefreshCalls / elapsed
    local refreshMsAvg = uiProfileRefreshCalls > 0 and (uiProfileRefreshSeconds * 1000 / uiProfileRefreshCalls) or 0
    local updateRate = uiProfileUpdateCalls / elapsed
    print(string.format("[ui-prof][lua] updates/s=%.1f refresh/s=%.1f refresh_ms=%.3f profile_rebuilds=%d profile_skips=%d",
        updateRate, refreshRate, refreshMsAvg, uiProfileProfileRebuilds, uiProfileProfileSkips))
    uiProfileWindowStart = now
    uiProfileUpdateCalls = 0
    uiProfileRefreshCalls = 0
    uiProfileRefreshSeconds = 0
    uiProfileProfileRebuilds = 0
    uiProfileProfileSkips = 0
end

local function preloadAssets()
    if assetsPreloaded then return end
    Audio.Preload(CLICK, "Sfx")
    Audio.Preload(HOVER, "Sfx")
    assetsPreloaded = true
end

local function place(widget, parent, left, top, width, height)
    bindings.place(widget, parent, left, top, width, height)
end

local function bindText(widget, value)
    bindings.text(widget, value)
end

local function bindVisible(widget, visible)
    bindings.visible(widget, visible)
end

local function bindColor(widget, color)
    bindings.color(widget, color)
end

local function bindMouseEnabled(widget, enabled)
    bindings.mouseEnabled(widget, enabled)
end

local function bindButtonColors(widget, normal, hover, selected)
    bindings.buttonColors(widget, normal, hover, selected)
end

local function bindValue(widget, value)
    bindings.value(widget, value)
end

local function bindTooltip(widget, value)
    bindings.tooltip(widget, value)
end

local function bindContentSize(widget, width, height)
    bindings.contentSize(widget, width, height)
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
    if applyState then applyState(Play.State()) end
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
    bindMouseEnabled(widget, available)
    bindButtonColors(widget,
        available and (primary and colors.red or colors.button) or colors.disabled,
        available and (primary and colors.redHover or colors.hover) or colors.disabled,
        colors.selected)
end
local function linesHeight(value, width)
    local count = 0
    for line in (value .. "\n"):gmatch("(.-)\n") do
        count = count + math.max(1, math.ceil(#line / math.max(1, math.floor(width / 9))))
    end
    return count * 22
end

local function selectionSignature(selection)
    if not selection then return "none" end
    local parts = {
        selection.kind or "",
        tostring(selection.id or ""),
        selection.archetype or "",
        selection.name or "",
        selection.bio or "",
        tostring(selection.owned or false),
        tostring(selection.sell or 0),
        tostring(selection.damage or 0),
        tostring(selection.range or 0),
        tostring(selection.rate or 0),
        tostring(selection.spent or 0),
        selection.damageType or "",
        tostring(selection.armorPiercing or 0),
        selection.effects or "",
        tostring(selection.health or 0),
        tostring(selection.maxHealth or 0),
        tostring(selection.shield or 0),
        tostring(selection.maxShield or 0),
        tostring(selection.armor or 0),
        tostring(selection.speed or 0),
        tostring(selection.reward or 0),
        tostring(selection.baseDamage or 0),
        selection.resistances or ""
    }
    for _, node in ipairs(selection.upgrades or {}) do
        parts[#parts + 1] = table.concat({
            node.id or "",
            tostring(node.level or 0),
            tostring(node.maxLevel or 0),
            tostring(node.cost or 0),
            tostring(node.enabled or false),
            tostring(node.minUpgradesRequired or 0),
            node.reason or "",
            node.name or "",
            node.description or "",
            node.icon or ""
        }, "|")
        if node.requires then
            parts[#parts + 1] = table.concat(node.requires, ",")
        end
    end
    return table.concat(parts, "#")
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
local chatToggle = button(root, "ChatToggle", "Chat", function() chatOpen = not chatOpen; if applyState then applyState(Play.State()) end end)

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
    local preview = UI.CreateFrame("Canvas", "TowerPreview" .. index, control)
    preview:SetMouseEnabled(false)
    preview:SetRenderCallback(function(left, top, width, height)
        Play.Preview(index, left, top, width, height)
    end)
    local stripe = frame(control, "SlotStripe" .. index, colors.line)
    stripe:SetMouseEnabled(false)
    local name = text(control, "SlotName" .. index, "Empty", 16)
    local price = text(control, "SlotPrice" .. index, "$0", 16, colors.gold)
    local number = text(control, "SlotNumber" .. index, tostring(index), 16, colors.muted)
    slots[index] = {root = control, preview = preview, stripe = stripe, name = name, price = price, number = number}
end
local placement = frame(root, "PlacementStatus", colors.panel)
local placementText = text(placement, "PlacementText", "", 16, colors.accent)
local cancelPlacement = button(placement, "CancelPlacement", "x", function() command(Play.CancelPlacement) end)
cancelPlacement:SetTooltip("Cancel placement")

local profile = UI.CreateFrame("Window", "SelectionProfile", root)
profile:SetTitle("")
profile:SetMovable(true)
profile:SetResizable(false)
profile:SetButtonMode("Minimize", "Hidden")
profile:SetButtonMode("Maximize", "Hidden")
profile:SetButtonMode("Close", "Hidden")
local profileViewportWidth, profileViewportHeight, profileLayoutHeight
local profileTitle = text(profile, "ProfileTitle", "", 24, colors.ink, true)
local profileClose = button(profile, "CloseProfile", "x", function() command(Play.ClearSelection) end)
profileClose:SetTooltip("Close profile")
local profileScroll = UI.CreateFrame("ScrollContainer", "ProfileScroll", profile)
local profileContent = profileScroll:GetContent()
local profileBio = text(profileContent, "ProfileBio", "", 16, colors.muted)
local profileStats = text(profileContent, "ProfileStats", "", 16, colors.ink)
local profileEffects = text(profileContent, "ProfileEffects", "", 16, colors.accent)
local towerViews = {
    archer_hut = dofile(sceneDirectory .. "towers/ArcherHut.lua"),
    mage_tower = dofile(sceneDirectory .. "towers/MageTower.lua")
}
local talentControls = dofile(sceneDirectory .. "towers/TalentControls.lua")
local towerInstances = {}
local sell, sellLabel = button(profile, "SellTower", "Sell", function()
    if state.selection and state.selection.owned and not state.paused then command(Play.Sell) end
end, true)

local function updateTowerView(selection, width)
    local archetype = selection and selection.kind == "tower" and (selection.archetype or "unknown") or nil
    for id, instance in pairs(towerInstances) do bindVisible(instance.root, id == archetype) end
    if not archetype then return width, 0 end
    local instance = towerInstances[archetype]
    if not instance then
        local content = frame(profileContent, archetype .. "_TowerContent")
        local context = {
            colors = colors, place = place, talentControls = talentControls,
            frame = function(parent, name, color) return frame(parent, archetype .. "_" .. name, color) end,
            text = function(parent, name, ...) return text(parent, archetype .. "_" .. name, ...) end,
            button = function(parent, name, ...) return button(parent, archetype .. "_" .. name, ...) end,
            purchase = function(id)
                local current = state.selection
                if not current or current.kind ~= "tower" or current.archetype ~= archetype
                    or not current.owned or state.paused then return end
                for _, node in ipairs(current.upgrades or {}) do
                    if node.id == id and node.enabled then command(Play.Upgrade, id); return end
                end
            end
        }
        local create = towerViews[archetype]
        local view
        if create then
            view = create(context, content)
        else
            local notice = text(content, "TowerUiUnavailable", "Tower UI not configured.", 16, colors.muted)
            view = {Update = function(_, _, availableWidth)
                place(notice, content, 4, 0, availableWidth - 8, 44)
                return availableWidth, 56
            end}
        end
        instance = {root = content, view = view}
        towerInstances[archetype] = instance
    end
    local contentWidth, contentHeight = instance.view:Update(selection, width)
    place(instance.root, profileContent, 0, 0, contentWidth, contentHeight)
    return contentWidth, contentHeight
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

bindings.defineState("hud.health", function(context)
    return tostring(math.ceil(context.state.health or 0))
end, function(value)
    bindText(statValues[1], value)
end)
bindings.defineState("hud.wave", function(context)
    return string.format("%d/%d", math.min(context.state.wave or 1, context.state.waveCount or 1), context.state.waveCount or 0)
end, function(value)
    bindText(statValues[2], value)
end)
bindings.defineState("hud.enemies", function(context)
    return tostring(context.state.enemies or 0)
end, function(value)
    bindText(statValues[3], value)
end)
bindings.defineState("hud.level", function(context)
    return context.state.level or ""
end, function(value)
    bindText(level, value)
end)
bindings.defineState("hud.levelVisible", function(context)
    return not context.compact
end, function(value)
    bindVisible(level, value)
end)
bindings.defineState("hud.gold", function(context)
    return "$" .. math.floor(context.state.money or 0)
end, function(value)
    bindText(goldValue, value)
end)
bindings.defineState("hud.chatToggle", function(context)
    return context.state.online and context.compact and not context.state.paused and context.state.phase == "running"
end, function(value)
    bindVisible(chatToggle, value)
end)
bindings.defineState("hud.countdownVisible", function(context)
    return context.state.countdownVisible and not context.state.paused
end, function(value)
    bindVisible(countdown, value)
end)
bindings.defineState("hud.countdownLabel", function(context)
    return context.state.countdownLabel or ""
end, function(value)
    bindText(countdownLabel, value)
end)
bindings.defineState("hud.countdownValue", function(context)
    return tostring(context.state.countdown or 0)
end, function(value)
    bindText(countdownValue, value)
end)
bindings.defineState("hud.countdownProgress", function(context)
    return context.state.countdownProgress or 0
end, function(value)
    bindValue(countdownBar, value)
end)
bindings.defineState("hud.loadoutVisible", function(context)
    return context.state.loadoutVisible and not context.state.paused
end, function(value)
    bindVisible(loadout, value)
end)
bindings.defineState("hud.placementVisible", function(context)
    return (context.state.selectedSlot or 0) > 0 and not context.state.paused and context.state.phase == "running"
end, function(value)
    bindVisible(placement, value)
end)
bindings.defineState("hud.placementText", function(context)
    return context.feedback ~= "" and context.feedback or
        (context.state.placement ~= "" and context.state.placement) or
        (context.selectedSlot and context.selectedSlot.name or "")
end, function(value)
    bindText(placementText, value)
end)
bindings.defineState("hud.placementColor", function(context)
    return context.state.canPlace and colors.accent or colors.gold
end, function(value)
    bindColor(placementText, value)
end)
bindings.defineState("status.visible", function(context)
    return (context.ready or context.loading or context.failed or context.terminal) and not context.state.paused
end, function(value)
    bindVisible(status, value)
end)
bindings.defineState("status.kicker", function(context)
    return context.ready and "BATTLEFIELD READY" or context.terminal and "DEPLOYMENT COMPLETE" or "FIELD STATUS"
end, function(value)
    bindText(statusKicker, value)
end)
bindings.defineState("status.title", function(context)
    return context.state.headline or ""
end, function(value)
    bindText(statusTitle, value)
end)
bindings.defineState("status.copy", function(context)
    return context.state.description or ""
end, function(value)
    bindText(statusCopy, value)
end)
bindings.defineState("status.progressVisible", function(context)
    return context.loading
end, function(value)
    bindVisible(statusProgress, value)
end)
bindings.defineState("status.progressValue", function(context)
    return context.state.loadingProgress or 0
end, function(value)
    bindValue(statusProgress, value)
end)
bindings.defineState("status.reason", function(context)
    return context.feedback ~= "" and context.feedback or
        (context.ready and context.state.startReason or
            (context.state.client and context.terminal and "Waiting for the host to replay." or ""))
end, function(value)
    bindText(statusReason, value)
end)
bindings.defineState("status.startVisible", function(context)
    return context.ready and not context.state.client
end, function(value)
    bindVisible(start, value)
end)
bindings.defineState("status.retryVisible", function(context)
    return context.failed
end, function(value)
    bindVisible(retry, value)
end)
bindings.defineState("status.replayVisible", function(context)
    return context.terminal and not context.state.client
end, function(value)
    bindVisible(replay, value)
end)
bindings.defineState("pause.visible", function(context)
    return context.state.paused
end, function(value)
    bindVisible(pause, value)
end)
bindings.defineState("pause.title", function(context)
    return context.state.online and "Match menu" or "Paused"
end, function(value)
    bindText(pauseTitle, value)
end)
bindings.defineState("pause.feedback", function(context)
    return context.feedback
end, function(value)
    bindText(pauseFeedback, value)
end)

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

applyState = function(nextState)
    local refreshStarted = uiProfileEnabled and os.clock() or nil
    refreshCount = refreshCount + 1
    if uiProfileEnabled then uiProfileRefreshCalls = uiProfileRefreshCalls + 1 end
    refreshing = true
    state = nextState or Play.State()
    local _, _, width, height = UI.Root:GetRect()
    if width <= 0 or height <= 0 then refreshing = false; return end
    local compact = width < 1000
    local slotWidth = math.min(148, (width - 48) / 5)
    local slotHeight = height < 700 and 136 or 172
    local loadoutWidth = slotWidth * 5 + 32
    local loadoutTop = height - slotHeight - 12
    place(stats, root, 12, 12, 270, 64)
    for index = 1, 3 do
        place(statValues[index], stats, 12 + (index - 1) * 88, 4, 84, 30)
        place(statLabels[index], stats, 12 + (index - 1) * 88, 36, 84, 22)
    end
    place(level, root, 20, 84, 260, 24)
    place(gold, root, width - 192, 12, 124, 64)
    place(goldCaption, gold, 12, 5, 100, 22)
    place(goldValue, gold, 12, 28, 108, 30)
    place(menu, root, width - 56, 12, 44, 40)
    place(chatToggle, root, width - 88, 84, 76, 38)

    place(countdown, root, (width - 220) / 2, compact and 84 or 12, 220, 76)
    place(countdownLabel, countdown, 12, 8, 148, 24)
    place(countdownValue, countdown, 170, 8, 44, 44)
    place(countdownBar, countdown, 12, 60, 196, 4)

    place(loadout, root, (width - loadoutWidth) / 2, loadoutTop, loadoutWidth, slotHeight)
    for index, slot in ipairs(slots) do
        local tower = (state.slots or {})[index] or {name = "Empty", cost = 0}
        local selected = state.selectedSlot == index
        place(slot.root, loadout, (index - 1) * (slotWidth + 8), 0, slotWidth, slotHeight)
        place(slot.stripe, slot.root, 0, slotHeight - 3, slotWidth, 3)
        place(slot.name, slot.root, 10, 8, slotWidth - 20, 40)
        place(slot.price, slot.root, 10, slotHeight - 28, slotWidth - 40, 24)
        place(slot.number, slot.root, slotWidth - 24, slotHeight - 28, 20, 24)
        bindText(slot.name, tower.name)
        bindText(slot.price, tower.available and ("$" .. tower.cost) or "")
        bindColor(slot.price, (state.money or 0) >= tower.cost and colors.gold or 0xEF9A86FF)
        bindButtonColors(slot.root, selected and colors.selected or colors.panel, colors.hover, colors.selected)
        slot.stripe:SetBackgroundColor(selected and colors.accent or colors.line)
        bindTooltip(slot.root, tower.bio or tower.name)
        bindMouseEnabled(slot.root, tower.available and state.phase == "running" and not state.paused)
        place(slot.preview, slot.root, 8, 44, slotWidth - 16, slotHeight - 78)
    end
    place(placement, root, (width - math.min(460, width - 32)) / 2, loadoutTop - 46, math.min(460, width - 32), 38)
    place(placementText, placement, 12, 8, math.min(460, width - 32) - 60, 24)
    place(cancelPlacement, placement, math.min(460, width - 32) - 38, 2, 34, 34)
    local selectedSlot = (state.slots or {})[state.selectedSlot or 0]

    local selection = state.selection
    bindVisible(profile, selection ~= nil and not state.paused and state.phase == "running")
    local profileWidth = math.min(560, width - 24)
    local profileHeight = math.max(120, loadoutTop - 96)
    local profileLayoutChanged = false
    if width ~= profileViewportWidth or height ~= profileViewportHeight or profileHeight ~= profileLayoutHeight then
        local left, top = profile:GetRect()
        if not profileViewportWidth then left, top = width - profileWidth - 12, 84 end
        profile:SetBounds(math.max(0, math.min(left, width - profileWidth)),
            math.max(0, math.min(top, height - profileHeight)), profileWidth, profileHeight)
        profileViewportWidth, profileViewportHeight, profileLayoutHeight = width, height, profileHeight
        profileLayoutChanged = true
    end
    place(profileTitle, profile, 12, 2, profileWidth - 60, 28)
    place(profileClose, profile, profileWidth - 36, 2, 28, 28)
    local tower = selection and selection.kind == "tower"
    place(profileScroll, profile, 12, 40, profileWidth - 24, profileHeight - (tower and 108 or 52))
    if selection then
        local selectionKey = selection.kind .. tostring(selection.id) .. (selection.archetype or "")
        local contentSignature = selectionSignature(selection) .. "|" .. profileWidth .. "|" .. profileHeight
        local profileDirty = profileLayoutChanged or contentSignature ~= profileContentSignature
        if selectionKey ~= previousSelection then
            profileScroll:SetScrollOffset(0, 0)
            profileDirty = true
        end
        previousSelection = selectionKey
        if profileDirty then
            if uiProfileEnabled then uiProfileProfileRebuilds = uiProfileProfileRebuilds + 1 end
            local treeWidth, infoTop = updateTowerView(selection, profileWidth - 40)
            bindText(profileTitle, selection.name)
            local inner = profileWidth - 48
            local bioHeight = linesHeight(selection.bio or "", inner)
            place(profileBio, profileContent, 4, infoTop, inner, bioHeight)
            bindText(profileBio, selection.bio or "")
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
            bindText(profileStats, detail)
            local effectsTop = infoTop + bioHeight + detailHeight + 28
            local effectsHeight = effects == "" and 0 or linesHeight(effects, inner)
            place(profileEffects, profileContent, 4, effectsTop, inner, effectsHeight)
            bindText(profileEffects, effects)
            local nextTop = effectsTop + effectsHeight + 12
            bindVisible(sell, tower)
            if tower then
                place(sell, profile, 12, profileHeight - 54, profileWidth - 24, 42)
                enabled(sell, selection.owned, true)
                bindText(sellLabel, selection.owned and ("Sell for $" .. selection.sell) or "Owned by another player")
            end
            bindContentSize(profileScroll, tower and treeWidth or inner + 8, nextTop)
            profileContentSignature = contentSignature
        elseif uiProfileEnabled then
            uiProfileProfileSkips = uiProfileProfileSkips + 1
        end
    else
        if previousSelection ~= "" then
            updateTowerView(nil, profileWidth - 40)
        end
        previousSelection = ""
        profileContentSignature = nil
    end

    local chatWidth, chatHeight = math.min(340, width - 24), math.min(200, loadoutTop - 100)
    place(chat, root, 12, loadoutTop - chatHeight - 12, chatWidth, chatHeight)
    bindVisible(chat, state.online and state.phase == "running" and not state.paused and (not compact or (chatOpen and not selection)))
    place(chatTitle, chat, 12, 8, chatWidth - 24, 24)
    place(chatScroll, chat, 8, 36, chatWidth - 16, chatHeight - 88)
    place(chatInput, chat, 8, chatHeight - 44, chatWidth - 88, 36)
    place(send, chat, chatWidth - 72, chatHeight - 44, 64, 36)
    local chatValue = table.concat(state.chat or {}, "\n")
    local chatTextHeight = linesHeight(chatValue, chatWidth - 40)
    place(chatText, chatScroll:GetContent(), 4, 0, chatWidth - 40, chatTextHeight)
    bindContentSize(chatScroll, chatWidth - 32, chatTextHeight)
    if chatValue ~= previousChat then
        bindText(chatText, chatValue)
        chatScroll:SetScrollOffset(0, math.max(0, chatTextHeight - chatHeight + 88))
        previousChat = chatValue
    end

    local terminal = state.phase == "victory" or state.phase == "defeat"
    local ready, loading, failed = state.phase == "ready", state.phase == "loading", state.phase == "failed"
    bindings.applyState({
        state = state,
        compact = compact,
        terminal = terminal,
        ready = ready,
        loading = loading,
        failed = failed,
        selectedSlot = selectedSlot,
        feedback = feedback
    })
    local panelWidth, panelHeight = math.min(520, width - 32), math.min(286, height - 32)
    place(statusPanel, status, (width - panelWidth) / 2, math.max(12, (height - panelHeight) / 2 - (ready and 48 or 0)), panelWidth, panelHeight)
    place(statusKicker, statusPanel, 24, 20, panelWidth - 48, 24)
    place(statusTitle, statusPanel, 24, 52, panelWidth - 48, 44)
    place(statusCopy, statusPanel, 24, 108, panelWidth - 48, 62)
    place(statusProgress, statusPanel, 24, 182, panelWidth - 48, 6)
    place(statusReason, statusPanel, 24, 174, panelWidth - 48, 42)
    enabled(start, state.canStart, true)
    local actionWidth = (panelWidth - 60) / 2
    for _, widget in ipairs({start, retry, replay}) do place(widget, statusPanel, 24, panelHeight - 62, actionWidth, 42) end
    place(returnLobby, statusPanel, panelWidth - actionWidth - 24, panelHeight - 62, actionWidth, 42)

    local pauseWidth, pauseHeight = math.min(480, width - 32), math.min(412, height - 32)
    place(pausePanel, pause, (width - pauseWidth) / 2, (height - pauseHeight) / 2, pauseWidth, pauseHeight)
    place(pauseKicker, pausePanel, 24, 20, pauseWidth - 48, 24)
    place(pauseTitle, pausePanel, 24, 52, pauseWidth - 48, 44)
    for index, control in ipairs(volumeControls) do
        local top = 112 + (index - 1) * 68
        place(control.label, pausePanel, 24, top, pauseWidth - 120, 24)
        place(control.value, pausePanel, pauseWidth - 88, top, 64, 24)
        place(control.slider, pausePanel, 24, top + 30, pauseWidth - 48, 22)
        bindValue(control.slider, state[control.name] or 0)
        bindText(control.value, math.floor((state[control.name] or 0) * 100 + 0.5) .. "%")
    end
    place(pauseFeedback, pausePanel, 24, 320, pauseWidth - 48, 24)
    place(resume, pausePanel, 24, pauseHeight - 62, (pauseWidth - 60) / 2, 42)
    place(pauseLobby, pausePanel, pauseWidth / 2 + 6, pauseHeight - 62, (pauseWidth - 60) / 2, 42)
    refreshing = false
    if refreshStarted then
        uiProfileRefreshSeconds = uiProfileRefreshSeconds + (os.clock() - refreshStarted)
        reportUiProfile()
    end
end

function OnEnter()
    preloadAssets()
    entered = true
    applyState(Play.State())
end

function OnExit()
    entered = false
end

function OnShortcut(scanCode)
    -- Return true only when this script explicitly consumes a key.
    -- Returning false keeps C++ fallback shortcuts active.
    return false
end

function OnStateChanged(nextState)
    if not entered then return end
    applyState(nextState)
end

function OnUpdate(dt)
    if uiProfileEnabled then uiProfileUpdateCalls = uiProfileUpdateCalls + 1 end
    reportUiProfile()
end