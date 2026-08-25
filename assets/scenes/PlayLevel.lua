local M = {}

local lastResult = ""
local lastLoadoutResult = ""
local debugPickSpheresVisible = false
local debugPlacementBoundsVisible = false
local debugUiVisible = false
local towerSlotTextures = {}
local lastUpgradeResult = ""
local towerUiArtTextures = {}

local kSlotW = 146
local kSlotPreviewH = 146
local kSlotLabelH = 68
local kSlotH = kSlotPreviewH + kSlotLabelH

local startMatchButton = nil
local playAgainButton = nil
local backToLobbyButton = nil
local slotButtons = {}
local exitToMissionSelectButton = nil
local spend25Button = nil
local damageBase10Button = nil
local startWaveButton = nil
local clearSelectionButton = nil
local compositeToggleButton = nil
local prevClipButton = nil
local nextClipButton = nil
local selectWalkingButton = nil
local resumeGameButton = nil
local quitGameButton = nil
local applyPauseSettingsButton = nil
local coinTexture = nil

local escapeMenuVisible = false
local escapeMenuStatus = ""

local function UiTextWrapped(text)
    ImGui.TextWrapped(tostring(text or ""))
end

local function TextCentered(text)
    local displayW = ImGui.GetWindowWidth()
    local textW, _ = ImGui.CalcTextSize(tostring(text or ""))
    ImGui.SetCursorPosX((displayW - textW) * 0.5)
    ImGui.Text(tostring(text or ""))
end

local function CenterButton(button)
    local availableW, _ = ImGui.GetContentRegionAvail()
    local buttonW, _ = button:getSize()
    ImGui.SetCursorPosX((availableW - buttonW) * 0.5)
end

local function getTowerPreviewTexture(slot)
    if not slot or not slot.available then
        return nil
    end

    local key = tostring(slot.id or slot.displayName or "")
    if key == "" then
        return nil
    end

    if towerSlotTextures[key] ~= nil then
        return towerSlotTextures[key] or nil
    end

    local path = tostring(slot.previewImagePath or "")
    if path == "" or not Texture or not Texture.load then
        towerSlotTextures[key] = false
        return nil
    end

    local tex = Texture.load(VulkanContext, path)
    if tex and tex.isValid and tex:isValid() then
        towerSlotTextures[key] = tex
        return tex
    end

    towerSlotTextures[key] = false
    return nil
end

local function useLiveTowerPreview(slot)
    if not slot or not slot.available then
        return false
    end
    local setTowerPreviewSlots = Gameplay and Gameplay["setTowerPreviewSlots"]
    if not setTowerPreviewSlots then
        return false
    end
    local getCursorScreenPos = ImGui and ImGui["GetCursorScreenPos"]
    if not getCursorScreenPos then
        return false
    end

    local proto = tonumber(slot and slot.previewPrototypeIndex) or -1
    return proto >= 0
end

local function hasActivePlacement()
    if not Gameplay.getTowerPlacementState then
        return false
    end
    local placement = Gameplay.getTowerPlacementState()
    return placement and placement.active
end

local function hasActiveSelection()
    if not Gameplay.getDebugSelection then
        return false
    end
    local selection = Gameplay.getDebugSelection()
    return selection and selection.valid
end

local function drawEscapeMenu(state)
    if not escapeMenuVisible then
        return
    end

    local displayW, displayH = ImGui.GetDisplaySize()
    local panelW, panelH = 560, 520
    ImGui.SetNextWindowPos((displayW - panelW) * 0.5, (displayH - panelH) * 0.35, ImGuiCond.Always)
    ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Always)
    ImGui.SetNextWindowBgAlpha(0.95)

    ImGui.Begin("PauseMenu", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoResize + ImGuiWindowFlags.NoTitleBar)

    if TitleFont then
        ImGui.PushFont(TitleFont)
    end
    TextCentered("Paused")
    if TitleFont then
        ImGui.PopFont()
    end

    ImGui.Spacing()
    UiTextWrapped("Press Esc to resume.")
    ImGui.Separator()

    local settings = state and state.settings or nil
    local audioDirty = false
    if settings and ImGui.SliderFloat then
        ImGui.Text("Quick Audio")
        local changed, value = ImGui.SliderFloat("Master Volume##pause", settings.masterVolume or 0.8, 0.0, 1.0)
        if changed then
            settings.masterVolume = value
            audioDirty = true
        end

        changed, value = ImGui.SliderFloat("Music Volume##pause", settings.musicVolume or 0.7, 0.0, 1.0)
        if changed then
            settings.musicVolume = value
            audioDirty = true
        end

        changed, value = ImGui.SliderFloat("SFX Volume##pause", settings.sfxVolume or 0.8, 0.0, 1.0)
        if changed then
            settings.sfxVolume = value
            audioDirty = true
        end

        if applyPauseSettingsButton ~= nil then
            if applyPauseSettingsButton:render() then
                if Gameplay.requestApplySettings then
                    Gameplay.requestApplySettings()
                    escapeMenuStatus = "Audio settings applied"
                else
                    escapeMenuStatus = "requestApplySettings unavailable"
                end
            elseif audioDirty then
                escapeMenuStatus = "Audio changed. Click Apply Audio to commit."
            end
        end
    end

    ImGui.Spacing()
    ImGui.Separator()
    ImGui.Spacing()

    if resumeGameButton ~= nil then
        if resumeGameButton:render() then
            escapeMenuVisible = false
            escapeMenuStatus = ""
        end
    end

    if quitGameButton ~= nil then
        if quitGameButton:render() then
            Gameplay.requestScene(Gameplay.Scene.Lobby, "Returning to mission select...")
        end
    end

    if escapeMenuStatus ~= "" then
        ImGui.Spacing()
        UiTextWrapped(escapeMenuStatus)
    end

    ImGui.End()
end

function M.onEnter()
    lastResult = "PlayLevel script loaded"
    lastUpgradeResult = ""
    if Gameplay.getDebugPickSpheresVisible then
        debugPickSpheresVisible = Gameplay.getDebugPickSpheresVisible()
    end
    if Gameplay.setDebugPickSpheresVisible then
        Gameplay.setDebugPickSpheresVisible(debugPickSpheresVisible)
    end
    if Gameplay.getDebugPlacementBoundsVisible then
        debugPlacementBoundsVisible = Gameplay.getDebugPlacementBoundsVisible()
    end
    if Gameplay.setDebugPlacementBoundsVisible then
        Gameplay.setDebugPlacementBoundsVisible(debugPlacementBoundsVisible)
    end

    startMatchButton = GameButton.new("startMatch", "Start Match", 120.0, 42.0)
    playAgainButton = GameButton.new("playAgain", "Play Again", 120.0, 42.0)
    backToLobbyButton = GameButton.new("backToLobby", "Back to Lobby", 120.0, 38.0)

    coinTexture = Texture.load(VulkanContext, "assets/images/coin.png")

    slotButtons = {}
    for i = 1, 5 do
        slotButtons[i] = GameButton.new(string.format("towerSlot_%d", i), string.format("Slot %d\nEmpty", i),
            kSlotW, kSlotLabelH)
    end

    exitToMissionSelectButton = GameButton.new("exitToMissionSelect", "Exit to Mission Select", -1.0, 30.0)
    spend25Button = GameButton.new("spend25", "Spend 25", -1, 0)
    damageBase10Button = GameButton.new("damageBase10", "Damage Base 10", -1, 0)
    startWaveButton = GameButton.new("startWave", "Start Wave", -1, 0)
    clearSelectionButton = GameButton.new("clearSelection", "Clear Selection", 160, 0)
    compositeToggleButton = GameButton.new("compositeToggle", "Composite: OFF", 160, 0)
    prevClipButton = GameButton.new("prevClip", "Prev Clip", 160, 0)
    nextClipButton = GameButton.new("nextClip", "Next Clip", 160, 0)
    selectWalkingButton = GameButton.new("selectWalking", "Select Walking", 160, 0)
    resumeGameButton = GameButton.new("resumeGame", "Resume", -1.0, 42.0)
    quitGameButton = GameButton.new("quitGame", "Quit Game", -1.0, 42.0)
    applyPauseSettingsButton = GameButton.new("applyPauseSettings", "Apply Audio", -1.0, 34.0)

    escapeMenuVisible = false
    escapeMenuStatus = ""
end

local function drawMatchStateOverlay(gs)
    local status = tostring(gs.matchStatus or "")
    if status ~= "WaitingToStart" and status ~= "Victory" and status ~= "Defeat" then
        return
    end

    local title = ""
    local subtitle = ""
    local alpha = 0.5
    if status == "WaitingToStart" then
        title = "Prepare Your Defenses"
        subtitle = "Start when ready."
        alpha = 0.35
    elseif status == "Victory" then
        title = "You won"
        subtitle = "All waves are cleared."
    else
        title = "Defeated"
        subtitle = "Your base has fallen."
    end

    local displayW, displayH = ImGui.GetDisplaySize()
    local panelW, panelH = 520, 340
    ImGui.SetNextWindowPos((displayW - panelW) * 0.5, (displayH - panelH) * 0.35, ImGuiCond.Always)
    ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Always)
    ImGui.SetNextWindowBgAlpha(alpha)
    ImGui.Begin("MatchStateOverlay",
        ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize)

    ImGui.SetCursorPosY(50)
    if TitleFont then
        ImGui.PushFont(TitleFont)
    end
    TextCentered(title)
    if TitleFont then
        ImGui.PopFont()
    end

    ImGui.Spacing()

	if HeadingFont then
		ImGui.PushFont(HeadingFont)
	end
    TextCentered(subtitle)
	if HeadingFont then
		ImGui.PopFont()
	end

    ImGui.Spacing()
    ImGui.Spacing()

    if status == "WaitingToStart" then
        if startMatchButton ~= nil then
            CenterButton(startMatchButton)
            if startMatchButton:render() then
                local r = Gameplay.requestStartWave and Gameplay.requestStartWave() or
                { ok = false, reason = "requestStartWave unavailable" }
                lastResult = string.format("Start match -> ok=%s reason=%s", tostring(r.ok), tostring(r.reason))
            end
        end
    else
        if playAgainButton ~= nil then
            CenterButton(playAgainButton)
            if playAgainButton:render() then
                Gameplay.requestScene(Gameplay.Scene.PlayLevel, "Restarting level...")
            end
        end
        ImGui.Spacing()
        if backToLobbyButton ~= nil then
            CenterButton(backToLobbyButton)
            if backToLobbyButton:render() then
                Gameplay.requestScene(Gameplay.Scene.Lobby, "Returning to mission select...")
            end
        end
    end

    ImGui.End()
end

local function drawWaveCountdownOverlay(gs)
    local status = tostring(gs.matchStatus or "")
    if status ~= "Running" then
        return
    end

    if gs.waveCountdownActive then
        local remaining = gs.waveCountdownRemainingSeconds
        local title = string.format("Next wave in")

        local displayW, displayH = ImGui.GetDisplaySize()
        local panelW, panelH = 520, 160
        ImGui.SetNextWindowPos((displayW - panelW) * 0.5, (displayH - panelH) * 0.05, ImGuiCond.Always)
        ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Always)
        ImGui.SetNextWindowBgAlpha(0.92)
        ImGui.Begin("MatchStateOverlay",
            ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize)

        UiTextWrapped(title)
        ImGui.ProgressBar(remaining / (gs.waveCountdownDurationSeconds or 1.0), -1.0, 18.0, "")
        ImGui.Spacing()
        if TitleFont then
            ImGui.PushFont(TitleFont)
        end
        TextCentered(string.format("%d", math.ceil(remaining or 0.0)))
        if TitleFont then
            ImGui.PopFont()
        end
        ImGui.End()
    elseif gs.waveRoundRemainingSeconds > 0.0 then
        local remaining = gs.waveRoundRemainingSeconds
        local title = string.format("Wave %d in", gs.currentWave + 1)

        local displayW, displayH = ImGui.GetDisplaySize()
        local panelW, panelH = 520, 160
        ImGui.SetNextWindowPos((displayW - panelW) * 0.5, (displayH - panelH) * 0.05, ImGuiCond.Always)
        ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Always)
        ImGui.SetNextWindowBgAlpha(0.92)
        ImGui.Begin("MatchStateOverlay",
            ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize)

        UiTextWrapped(title)
        ImGui.ProgressBar(remaining / (gs.waveRoundDurationSeconds or 1.0), -1.0, 18.0, "")
        ImGui.Spacing()
        if TitleFont then
            ImGui.PushFont(TitleFont)
        end
        TextCentered(string.format("%d", math.ceil(remaining or 0.0)))
        if TitleFont then
            ImGui.PopFont()
        end
        ImGui.End()
    end
end

local function moneyWindow(gs)
    local displayW, displayH = ImGui.GetDisplaySize()
    local panelW, panelH = 160, 80
    ImGui.SetNextWindowPos((displayW - panelW) * 0.75, (displayH - panelH) * 0.99, ImGuiCond.Always)
    ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Always)
    ImGui.SetNextWindowBgAlpha(0.35)
    ImGui.Begin("MoneyWindow",
        ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize)

    ImGui.SetCursorPos(40, 25)
    local moneyText = string.format("%d", gs.playerMoney or 0)
    if coinTexture and coinTexture.isValid and coinTexture:isValid() then
        ImGui.Image(coinTexture, 32, 32)
        ImGui.SameLine()
    end
    if HeadingFont then
        ImGui.PushFont(HeadingFont)
    end
    ImGui.Text(moneyText)
    if HeadingFont then
        ImGui.PopFont()
    end
    ImGui.End()
end

local function drawTowerUpgradeWindow(gs)
    local getSelectedTowerUpgradeState = Gameplay and Gameplay["getSelectedTowerUpgradeState"]
    local getSelectedEnemyInfo = Gameplay and Gameplay["getSelectedEnemyInfo"]
    local requestSelectedTowerUpgrade = Gameplay and Gameplay["requestSelectedTowerUpgrade"]

    local function getTowerUiConfig(state)
        local ui = state and state.ui or nil
        return {
            accent = { 0.62, 0.42, 0.08 },
            unlocked = { 0.15, 0.52, 0.27 },
            locked = { 0.22, 0.23, 0.26 },
            panelTitle = tostring(ui and ui.panelTitle or "Tower Talent Tree"),
            talentTreeArtPath = tostring(ui and ui.artPath or ""),
        }
    end

    local function applyUiColorOverride(rgb, fallback)
        if not rgb then
            return fallback
        end
        local r = tonumber(rgb[1] or fallback[1]) or fallback[1]
        local g = tonumber(rgb[2] or fallback[2]) or fallback[2]
        local b = tonumber(rgb[3] or fallback[3]) or fallback[3]
        return { r, g, b }
    end

    local function getTowerUiArtTexture(path)
        local key = tostring(path or "")
        if key == "" then
            return nil
        end
        if towerUiArtTextures[key] ~= nil then
            return towerUiArtTextures[key] or nil
        end
        if not Texture or not Texture.load then
            towerUiArtTextures[key] = false
            return nil
        end

        local tex = Texture.load(VulkanContext, key)
        if tex and tex.isValid and tex:isValid() then
            towerUiArtTextures[key] = tex
            return tex
        end

        towerUiArtTextures[key] = false
        return nil
    end

    local function getNodeIconTexture(node, defaultPath)
        local nodePath = tostring(node and node.icon or "")
        if nodePath ~= "" then
            local nodeTex = getTowerUiArtTexture(nodePath)
            if nodeTex then
                return nodeTex
            end
        end
        local fallbackPath = tostring(defaultPath or "")
        if fallbackPath ~= "" then
            return getTowerUiArtTexture(fallbackPath)
        end
        return getTowerUiArtTexture("assets/images/question.png")
    end

    local function appendEffectLine(lines, label, value, fmt)
        local numeric = tonumber(value or 0) or 0
        if math.abs(numeric) <= 0.0001 then
            return
        end
        table.insert(lines, string.format("%s: " .. fmt, label, numeric))
    end

    local function buildNodeTooltip(node)
        local lines = {}
        table.insert(lines, tostring(node.displayName or node.id or "Talent"))
        local currentLevel = tonumber(node.currentLevel or 0) or 0
        local maxLevel = tonumber(node.maxLevel or 1) or 1
        table.insert(lines, string.format("Level %d/%d", currentLevel, maxLevel))
        table.insert(lines, string.format("Cost: %d", math.floor(tonumber(node.cost or 0) or 0)))
        if node.parent and node.parent ~= "" then
            table.insert(lines, "Parent: " .. tostring(node.parent))
        end
        if tonumber(node.minUpgradesRequired or 0) > 0 then
            table.insert(lines, string.format("Needs total upgrades: %d", tonumber(node.minUpgradesRequired or 0)))
        end
        if node.description and node.description ~= "" then
            table.insert(lines, tostring(node.description))
        end

        local fx = node.effects or {}
        appendEffectLine(lines, "Damage Add", fx.attackDamageAdd, "%+.2f")
        appendEffectLine(lines, "Damage Mult", fx.attackDamageMul and (fx.attackDamageMul - 1.0), "%+.2f")
        appendEffectLine(lines, "Range Add", fx.attackRangeAdd, "%+.2f")
        appendEffectLine(lines, "Range Mult", fx.attackRangeMul and (fx.attackRangeMul - 1.0), "%+.2f")
        appendEffectLine(lines, "Speed Add", fx.attackSpeedAdd, "%+.2f")
        appendEffectLine(lines, "Speed Mult", fx.attackSpeedMul and (fx.attackSpeedMul - 1.0), "%+.2f")
        appendEffectLine(lines, "Projectile Speed Add", fx.projectileSpeedAdd, "%+.2f")
        appendEffectLine(lines, "Projectile Speed Mult", fx.projectileSpeedMul and (fx.projectileSpeedMul - 1.0), "%+.2f")
        appendEffectLine(lines, "Splash Add", fx.splashRadiusAdd, "%+.2f")
        appendEffectLine(lines, "Splash Mult", fx.splashRadiusMul and (fx.splashRadiusMul - 1.0), "%+.2f")
        appendEffectLine(lines, "Chain Count Add", fx.chainTargetCountAdd, "%+d")
        appendEffectLine(lines, "Chain Range Add", fx.chainRangeAdd, "%+.2f")
        appendEffectLine(lines, "Chain Range Mult", fx.chainRangeMul and (fx.chainRangeMul - 1.0), "%+.2f")
        appendEffectLine(lines, "Ricochet Count Add", fx.ricochetCountAdd, "%+d")
        appendEffectLine(lines, "Ricochet Range Add", fx.ricochetRangeAdd, "%+.2f")
        appendEffectLine(lines, "Ricochet Range Mult", fx.ricochetRangeMul and (fx.ricochetRangeMul - 1.0), "%+.2f")

        if node.reason and node.reason ~= "" then
            table.insert(lines, "")
            table.insert(lines, "Locked: " .. tostring(node.reason))
        end

        return table.concat(lines, "\n")
    end

    if getSelectedTowerUpgradeState then
        local state = getSelectedTowerUpgradeState()
        if state and state.valid then
            local uiConfig = getTowerUiConfig(state)
            uiConfig.accent = applyUiColorOverride(state and state.ui and state.ui.accent, uiConfig.accent)
            uiConfig.unlocked = applyUiColorOverride(state and state.ui and state.ui.unlocked, uiConfig.unlocked)
            uiConfig.locked = applyUiColorOverride(state and state.ui and state.ui.locked, uiConfig.locked)
            local displayW, _ = ImGui.GetDisplaySize()
            local panelW, panelH = 960, 500
            ImGui.SetNextWindowPos(displayW - panelW - 20, 90, ImGuiCond.FirstUseEver)
            ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.FirstUseEver)
            ImGui.SetNextWindowBgAlpha(0.84)
            ImGui.Begin("TowerUpgrades", ImGuiWindowFlags.NoCollapse)

            ImGui.Text(tostring(uiConfig.panelTitle or "Tower Talent Tree"))
            local artPath = tostring(uiConfig.talentTreeArtPath or "")
            if artPath ~= "" then
                local artTex = getTowerUiArtTexture(artPath)
                if artTex then
                    ImGui.Image(artTex, panelW - 36, 90)
                end
            end
            ImGui.Separator()

            ImGui.Text(string.format("%s  #%d", tostring(state.displayName or state.towerId or "Tower"),
                tonumber(state.towerInstanceOrdinal or 1) or 1))
            ImGui.Text(string.format("Type: %s   Money: %d", tostring(state.damageType or "physical"),
                math.floor(tonumber(gs.playerMoney or 0) or 0)))
            ImGui.Separator()
            ImGui.Text("Talent Graph")

            local nodes = state.nodes or {}
            local nodesById = {}
            for i = 1, #nodes do
                local node = nodes[i]
                local nodeId = tostring(node.id or "")
                if nodeId ~= "" then
                    nodesById[nodeId] = node
                end
            end

            local depthById = {}
            local function resolveDepth(nodeId, visiting)
                if depthById[nodeId] ~= nil then
                    return depthById[nodeId]
                end
                local node = nodesById[nodeId]
                if not node then
                    depthById[nodeId] = 0
                    return 0
                end

                if visiting[nodeId] then
                    depthById[nodeId] = 0
                    return 0
                end

                visiting[nodeId] = true
                local parentId = tostring(node.parent or "")
                local depth = 0
                if parentId ~= "" and nodesById[parentId] then
                    depth = resolveDepth(parentId, visiting) + 1
                end
                visiting[nodeId] = nil
                depthById[nodeId] = depth
                return depth
            end

            local rows = {}
            local maxDepth = 0
            for i = 1, #nodes do
                local node = nodes[i]
                local nodeId = tostring(node.id or "")
                local depth = resolveDepth(nodeId, {})
                if not rows[depth] then
                    rows[depth] = {}
                end
                table.insert(rows[depth], node)
                maxDepth = math.max(maxDepth, depth)
            end

            local childrenOrderRankByParent = {}
            for nodeId, node in pairs(nodesById) do
                local order = node.childrenOrder
                if type(order) == "table" then
                    local rankById = {}
                    for orderIdx = 1, #order do
                        local childId = tostring(order[orderIdx] or "")
                        if childId ~= "" and rankById[childId] == nil then
                            rankById[childId] = orderIdx
                        end
                    end
                    childrenOrderRankByParent[nodeId] = rankById
                end
            end

            -- First pass: order each row using parent-flow ranking to minimize crossed connectors.
            local rowSorted = {}
            local orderRankByNodeId = {}
            for depth = 0, maxDepth do
                local row = rows[depth] or {}
                table.sort(row, function(a, b)
                    local aId = tostring(a.id or "")
                    local bId = tostring(b.id or "")
                    local aParent = tostring(a.parent or "")
                    local bParent = tostring(b.parent or "")

                    local aParentRank = orderRankByNodeId[aParent] or 1000000
                    local bParentRank = orderRankByNodeId[bParent] or 1000000
                    if aParentRank ~= bParentRank then
                        return aParentRank < bParentRank
                    end

                    if aParent ~= "" and aParent == bParent then
                        local rankMap = childrenOrderRankByParent[aParent]
                        if rankMap then
                            local ar = rankMap[aId]
                            local br = rankMap[bId]
                            if ar ~= nil and br ~= nil and ar ~= br then
                                return ar < br
                            end
                            if ar ~= nil and br == nil then
                                return true
                            end
                            if ar == nil and br ~= nil then
                                return false
                            end
                        end
                    end

                    local ac = tonumber(a.column or 0) or 0
                    local bc = tonumber(b.column or 0) or 0
                    if ac ~= bc then
                        return ac < bc
                    end
                    return aId < bId
                end)

                rowSorted[depth] = row
                for idx = 1, #row do
                    local id = tostring(row[idx].id or "")
                    if id ~= "" then
                        orderRankByNodeId[id] = idx
                    end
                end
            end

            local defaultNodeIconPath = "assets/images/question.png"
            if state.ui and state.ui.defaultNodeIcon and state.ui.defaultNodeIcon ~= "" then
                defaultNodeIconPath = tostring(state.ui.defaultNodeIcon)
            end

            local nodeIconSize = 32
            local nodeStepXBase = 74
            local nodeStepXMin = 56
            local nodeStepXMax = 112
            local rowGapY = 42
            local nodeCenters = {}

            for depth = 0, maxDepth do
                local row = rowSorted[depth] or {}

                if #row == 0 then
                    ImGui.Dummy(1, rowGapY)
                end

                local availW, _ = ImGui.GetContentRegionAvail()
                local rowStepX = nodeStepXBase
                if #row > 1 then
                    local fitStep = (availW - nodeIconSize) / (#row - 1)
                    rowStepX = math.max(nodeStepXMin, math.min(nodeStepXMax, fitStep))
                end
                local rowWidth = ((#row - 1) * rowStepX) + nodeIconSize
                if #row > 0 then
                    ImGui.SetCursorPosX(math.max(6, (availW - rowWidth) * 0.5))
                end

                for i = 1, #row do
                    local node = row[i]
                    if i > 1 then
                        ImGui.SameLine(0, rowStepX - nodeIconSize)
                    end

                    local unlocked = node.unlocked == true
                    local canUnlock = node.canUnlock == true
                    if unlocked then
                        ImGui.PushStyleColor(ImGuiCol.Button, uiConfig.unlocked[1], uiConfig.unlocked[2], uiConfig.unlocked[3], 0.95)
                        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, uiConfig.unlocked[1] + 0.03, uiConfig.unlocked[2] + 0.08,
                            uiConfig.unlocked[3] + 0.04, 0.98)
                        ImGui.PushStyleColor(ImGuiCol.ButtonActive, uiConfig.unlocked[1] - 0.03, uiConfig.unlocked[2] - 0.07,
                            uiConfig.unlocked[3] - 0.03, 0.98)
                    elseif canUnlock then
                        ImGui.PushStyleColor(ImGuiCol.Button, uiConfig.accent[1], uiConfig.accent[2], uiConfig.accent[3], 0.95)
                        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, uiConfig.accent[1] + 0.10, uiConfig.accent[2] + 0.10,
                            uiConfig.accent[3] + 0.05, 0.98)
                        ImGui.PushStyleColor(ImGuiCol.ButtonActive, uiConfig.accent[1] - 0.08, uiConfig.accent[2] - 0.07,
                            uiConfig.accent[3] - 0.03, 0.98)
                    else
                        ImGui.PushStyleColor(ImGuiCol.Button, uiConfig.locked[1], uiConfig.locked[2], uiConfig.locked[3], 0.88)
                        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, uiConfig.locked[1] + 0.04, uiConfig.locked[2] + 0.04,
                            uiConfig.locked[3] + 0.04, 0.90)
                        ImGui.PushStyleColor(ImGuiCol.ButtonActive, uiConfig.locked[1] - 0.04, uiConfig.locked[2] - 0.03,
                            uiConfig.locked[3] - 0.03, 0.92)
                    end

                    local iconTex = getNodeIconTexture(node, defaultNodeIconPath)
                    local clicked = false
                    local iconX, iconY = ImGui.GetCursorScreenPos()
                    ImGui.PushStyleVar(ImGuiStyleVar.FramePadding, 0.0, 0.0)
                    if iconTex and ImGui.ImageButton then
                        clicked = ImGui.ImageButton(string.format("node_icon_%s", tostring(node.id or "node")),
                            iconTex, nodeIconSize, nodeIconSize)
                    else
                        clicked = ImGui.Button("?##" .. tostring(node.id or "node"), nodeIconSize, nodeIconSize)
                    end
                    ImGui.PopStyleVar()

                    local nodeTopCenterX = iconX + nodeIconSize * 0.5
                    local nodeTopY = iconY
                    local nodeBottomY = iconY + nodeIconSize

                    local nodeId = tostring(node.id or "")
                    if nodeId ~= "" then
                        nodeCenters[nodeId] = { x = nodeTopCenterX, yTop = nodeTopY, yBottom = nodeBottomY }
                    end

                    local parentId = tostring(node.parent or "")
                    if parentId ~= "" and nodeCenters[parentId] and ImGui.DrawLine then
                        local p = nodeCenters[parentId]
                        ImGui.DrawLine(p.x, p.yBottom, nodeTopCenterX,
                            nodeTopY,
                            uiConfig.accent[1], uiConfig.accent[2], uiConfig.accent[3], 0.78, 1.6)
                    end

                    if clicked then
                        if requestSelectedTowerUpgrade then
                            local r = requestSelectedTowerUpgrade(tostring(node.id or ""))
                            lastUpgradeResult = string.format("Upgrade %s -> ok=%s reason=%s", tostring(node.id), tostring(r.ok),
                                tostring(r.reason))
                        end
                    end
                    if ImGui.IsItemHovered and ImGui.SetTooltip and ImGui.IsItemHovered() then
                        ImGui.SetTooltip(buildNodeTooltip(node))
                    end
                    ImGui.PopStyleColor(3)
                end

                if #row > 0 then
                    ImGui.Dummy(1, rowGapY)
                end
            end

            ImGui.Separator()
            ImGui.Text("Tower Stats")
            ImGui.Text(string.format("DMG %.1f -> %.1f   AP %.1f -> %.1f   RNG %.1f -> %.1f",
                tonumber(state.baseAttackDamage or 0), tonumber(state.attackDamage or 0),
                tonumber(state.baseArmorPiercing or 0), tonumber(state.armorPiercing or 0),
                tonumber(state.baseAttackRange or 0), tonumber(state.attackRange or 0)))
            ImGui.Text(string.format("SPD %.2f -> %.2f   PROJ %.1f -> %.1f   SPL %.1f -> %.1f",
                tonumber(state.baseAttackSpeed or 0), tonumber(state.attackSpeed or 0),
                tonumber(state.baseProjectileSpeed or 0), tonumber(state.projectileSpeed or 0),
                tonumber(state.baseSplashRadius or 0), tonumber(state.splashRadius or 0)))
            ImGui.Text(string.format("SHOT %d -> %d   CHAIN %d -> %d @ %.1f -> %.1f   BOUNCE %d -> %d @ %.1f -> %.1f",
                tonumber(state.baseProjectileCount or 1), tonumber(state.projectileCount or 1),
                tonumber(state.baseChainTargetCount or 1), tonumber(state.chainTargetCount or 1),
                tonumber(state.baseChainRange or 0), tonumber(state.chainRange or 0),
                tonumber(state.baseRicochetCount or 0), tonumber(state.ricochetCount or 0),
                tonumber(state.baseRicochetRange or 0), tonumber(state.ricochetRange or 0)))

            if lastUpgradeResult ~= "" then
                UiTextWrapped(lastUpgradeResult)
            end

            ImGui.End()
            return
        end
    end

    if getSelectedEnemyInfo then
        local enemy = getSelectedEnemyInfo()
        if enemy and enemy.valid then
            local displayW, _ = ImGui.GetDisplaySize()
            local panelW, panelH = 460, 280
            ImGui.SetNextWindowPos(displayW - panelW - 20, 90, ImGuiCond.Always)
            ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Always)
            ImGui.SetNextWindowBgAlpha(0.88)
            ImGui.Begin("EnemyDetails", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoResize)

            ImGui.Text(string.format("%s  #%d", tostring(enemy.displayName or enemy.enemyId or "Enemy"),
                tonumber(enemy.instanceOrdinal or 1) or 1))
            ImGui.Separator()
            if enemy.description and enemy.description ~= "" then
                UiTextWrapped(enemy.description)
                ImGui.Separator()
            end

            ImGui.Text(string.format("Health: %.0f / %.0f", tonumber(enemy.health or 0), tonumber(enemy.maxHealth or 0)))
            ImGui.Text(string.format("Shield: %.0f / %.0f", tonumber(enemy.shield or 0), tonumber(enemy.maxShield or 0)))
            ImGui.Text(string.format("Armor: %.1f", tonumber(enemy.armor or 0)))
            ImGui.Text(string.format("Move Speed: %.2f", tonumber(enemy.moveSpeed or 0)))
            ImGui.Text(string.format("Base Damage: %.0f", tonumber(enemy.baseDamage or 0)))
            ImGui.Text(string.format("Bounty: $%.0f", tonumber(enemy.rewardMoney or 0)))

            local resistances = enemy.resistances or {}
            if #resistances > 0 then
                ImGui.Separator()
                ImGui.Text("Resistances")
                table.sort(resistances, function(a, b)
                    return tostring(a.damageType or "") < tostring(b.damageType or "")
                end)
                for i = 1, #resistances do
                    local r = resistances[i]
                    local dtype = tostring(r.damageType or "unknown")
                    local pct = tonumber(r.percent or 0) or 0
                    local tag = ""
                    if pct > 100.0 then
                        tag = " (heals)"
                    elseif pct == 100.0 then
                        tag = " (immune)"
                    end
                    ImGui.Text(string.format("%s: %.0f%%%s", dtype, pct, tag))
                end
            end

            if enemy.selectionDistance then
                ImGui.Text(string.format("Distance: %.2f", tonumber(enemy.selectionDistance or 0)))
            end

            ImGui.End()
        end
    end
end

function M.render(state, dt, elapsed)
    local gs = Gameplay.getState()

    if ImGui.IsKeyPressed and ImGuiKey and ImGuiKey.GraveAccent then
        if ImGui.IsKeyPressed(ImGuiKey.GraveAccent, false) then
            debugUiVisible = not debugUiVisible
        end
    end

    if ImGui.IsKeyPressed and ImGuiKey and ImGuiKey.H then
        if ImGui.IsKeyPressed(ImGuiKey.H, false) then
            debugPickSpheresVisible = not debugPickSpheresVisible
            if Gameplay.setDebugPickSpheresVisible then
                Gameplay.setDebugPickSpheresVisible(debugPickSpheresVisible)
            end
            lastResult = string.format("Pick spheres -> %s", debugPickSpheresVisible and "ON" or "OFF")
        end
    end

    if ImGui.IsKeyPressed and ImGuiKey and ImGuiKey.B then
        if ImGui.IsKeyPressed(ImGuiKey.B, false) then
            debugPlacementBoundsVisible = not debugPlacementBoundsVisible
            if Gameplay.setDebugPlacementBoundsVisible then
                Gameplay.setDebugPlacementBoundsVisible(debugPlacementBoundsVisible)
            end
            lastResult = string.format("Placement bounds -> %s", debugPlacementBoundsVisible and "ON" or "OFF")
        end
    end

    local isMouseClicked = ImGui and ImGui["IsMouseClicked"]
    local imguiMouseButton = _G["ImGuiMouseButton"]
    if isMouseClicked and imguiMouseButton and imguiMouseButton.Right then
        if isMouseClicked(imguiMouseButton.Right, false) and hasActiveSelection() then
            Gameplay.clearDebugSelection()
            lastResult = "Tower selection cleared"
        elseif isMouseClicked(imguiMouseButton.Right, false) and hasActivePlacement() then
            Gameplay.cancelTowerPlacement()
            lastResult = "Tower placement cancelled"
        end
        
    end

    if ImGui.IsKeyPressed and ImGuiKey and ImGuiKey.Escape then
        if ImGui.IsKeyPressed(ImGuiKey.Escape, false) then
            if escapeMenuVisible then
                escapeMenuVisible = false
                escapeMenuStatus = ""
            elseif hasActivePlacement() then
                Gameplay.cancelTowerPlacement()
            elseif hasActiveSelection() then
                if Gameplay.clearDebugSelection then
                    Gameplay.clearDebugSelection()
                    lastResult = "Selection cleared"
                end
            else
                escapeMenuVisible = true
                escapeMenuStatus = ""
            end
        end
    end

    if gs.worldLoading then
        local displayW, displayH = ImGui.GetDisplaySize()
        local panelW, panelH = 600, 260

        ImGui.SetNextWindowPos((displayW - panelW) * 0.5, (displayH - panelH) * 0.5, ImGuiCond.Once)
        ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Once)
        ImGui.SetNextWindowBgAlpha(0.88)
        ImGui.Begin("WorldLoading",
            ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoScrollbar)
        ImGui.Text(string.format("Loading  %s", state.activeLevelName or "Level"))
        ImGui.Separator()
        ImGui.Spacing()
        ImGui.Text(tostring(gs.loadActivity or "Starting..."))
        ImGui.Spacing()
        ImGui.ProgressBar(gs.loadProgress or 0.0, -1.0, 18.0, "")
        ImGui.Text(string.format("  %.0f%%", (gs.loadProgress or 0.0) * 100.0))
        ImGui.End()
        return
    end

    drawMatchStateOverlay(gs)
    drawWaveCountdownOverlay(gs)
    moneyWindow(gs)
    drawTowerUpgradeWindow(gs)

    local loadout = nil
    if Gameplay.getTowerLoadout then
        loadout = Gameplay.getTowerLoadout()
    end
    local placement = nil
    if Gameplay.getTowerPlacementState then
        placement = Gameplay.getTowerPlacementState()
    end

    local displayW, displayH = ImGui.GetDisplaySize()
    local hudW, hudH = 460, 320

    local slotW, slotH = kSlotW, kSlotH
    local previewH = kSlotPreviewH
    local gap = 8
    local barW = slotW * 5 + gap * 4 + 28
    local barH = slotH + 62
    ImGui.SetNextWindowPos((displayW - barW) * 0.5, displayH - barH - 12, ImGuiCond.Always)
    ImGui.SetNextWindowSize(barW, barH, ImGuiCond.Always)
    ImGui.SetNextWindowBgAlpha(0.80)
    ImGui.Begin("TowerLoadout", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize)

    local previewSlots = {}
    local setTowerPreviewSlots = Gameplay and Gameplay["setTowerPreviewSlots"]
    local getCursorScreenPos = ImGui and ImGui["GetCursorScreenPos"]

    for i = 1, 5 do
        local slot = loadout and loadout[i] or nil
        local available = slot and slot.available
        local selected = slot and slot.selected

        if i > 1 then
            ImGui.SameLine()
        end

        if selected then
            ImGui.PushStyleColor(ImGuiCol.Button, 0.20, 0.54, 0.28, 0.88)
            ImGui.PushStyleColor(ImGuiCol.ButtonHovered, 0.26, 0.66, 0.34, 0.95)
            ImGui.PushStyleColor(ImGuiCol.ButtonActive, 0.16, 0.48, 0.24, 0.98)
        end

        local isEmpty = not available
        if isEmpty then
            ImGui.PushStyleColor(ImGuiCol.Button, 0.16, 0.17, 0.20, 0.35)
            ImGui.PushStyleColor(ImGuiCol.ButtonHovered, 0.19, 0.20, 0.23, 0.45)
            ImGui.PushStyleColor(ImGuiCol.ButtonActive, 0.14, 0.15, 0.17, 0.55)
            ImGui.PushStyleColor(ImGuiCol.Text, 0.62, 0.64, 0.68, 0.75)
        end

        ImGui.BeginGroup()
        local previewClicked = false
        if available then
            if useLiveTowerPreview(slot) then
                local x, y = getCursorScreenPos()
                local proto = tonumber(slot and slot.previewPrototypeIndex) or -1
                table.insert(previewSlots, {
                    slot = i,
                    prototypeIndex = proto,
                    x = x,
                    y = y,
                    w = slotW,
                    h = previewH,
                })

                ImGui.PushStyleColor(ImGuiCol.Button, 0.0, 0.0, 0.0, 0.0)
                ImGui.PushStyleColor(ImGuiCol.ButtonHovered, 1.0, 1.0, 1.0, 0.08)
                ImGui.PushStyleColor(ImGuiCol.ButtonActive, 1.0, 1.0, 1.0, 0.14)
                previewClicked = ImGui.Button(string.format("##tower_preview_%d", i), slotW, previewH)
                ImGui.PopStyleColor(3)
            else
                local preview = getTowerPreviewTexture(slot)
                if preview then
                    if ImGui.ImageButton then
                        previewClicked = ImGui.ImageButton(string.format("tower_preview_%d", i), preview, slotW, previewH)
                    else
                        ImGui.Image(preview, slotW, previewH)
                    end
                else
                    previewClicked = ImGui.Button("Preview", slotW, previewH)
                end
            end
        else
            previewClicked = ImGui.Button(string.format("##tower_preview_empty_%d", i), slotW, previewH)
        end

        local label = string.format("Slot %d", i)
        if available then
            local displayName = tostring((slot and slot.displayName) or (slot and slot.id) or "Tower")
            local cost = math.floor(tonumber((slot and slot.cost) or 0) or 0)
            label = string.format("Slot %d\n%s\n$%d", i, displayName, cost)
        else
            label = string.format("Slot %d\nEmpty", i)
        end

        local slotButton = slotButtons[i]
        slotButton:setLabel(label)
        local slotButtonClicked = slotButton:render()
        if previewClicked or slotButtonClicked then
            if available and Gameplay.selectTowerSlot then
                local r = Gameplay.selectTowerSlot(i)
                lastLoadoutResult = string.format("Slot %d -> ok=%s reason=%s", i, tostring(r.ok), tostring(r.reason))
            end
        end

        ImGui.EndGroup()

        if isEmpty then
            ImGui.PopStyleColor(4)
        end

        if selected then
            ImGui.PopStyleColor(3)
        end
    end

    if setTowerPreviewSlots then
        setTowerPreviewSlots(previewSlots)
    end

    if placement and placement.active then
        local reason = tostring(placement.reason or "")
        local verdict = placement.canPlace and "VALID" or "INVALID"
        ImGui.Text(string.format("Placement: %s  |  %s", verdict,
            tostring(placement.displayName or placement.towerId or "tower")))
        ImGui.Text(string.format("Range: %.1f  |  Esc: cancel", tonumber(placement.attackRange or 0.0)))
        if reason ~= "" then
            UiTextWrapped(reason)
        end
    end

    if lastLoadoutResult ~= "" then
        UiTextWrapped(lastLoadoutResult)
    end

    ImGui.End()

    drawEscapeMenu(state)

    if not debugUiVisible then
        return
    end

    ImGui.SetNextWindowPos(16, displayH - hudH - 16, ImGuiCond.Once)
    ImGui.SetNextWindowSize(hudW, hudH, ImGuiCond.Once)
    ImGui.SetNextWindowBgAlpha(0.70)
    ImGui.Begin("WorldHUD", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoScrollbar)
    ImGui.Text(state.activeLevelName or "")
    ImGui.Separator()

    if gs.worldLoaded then
        ImGui.Text(string.format("Meshes %d  |  Verts %d  |  Tris %d", gs.meshCount or 0, gs.vertexCount or 0,
            gs.triCount or 0))
        ImGui.Text(string.format("Base HP %d  |  Money %d  |  Wave %d", gs.baseHealth, gs.playerMoney, gs.currentWave))
        ImGui.Text(string.format("Enemy Clip %s", gs.enemyAnimationName or "none"))
        ImGui.Text(string.format("To Spawn %d  |  Alive %d  |  Defeated %d", gs.enemiesToSpawn or 0, gs.enemiesAlive or 0,
            gs.enemiesDefeated or 0))
        ImGui.Text(string.format("Path Points %d  |  Waves %d", gs.routePointCount or 0, gs.waveCount or 0))
        if gs.cameraPosition then
            ImGui.Text(string.format("Pos (%.1f, %.1f, %.1f)", gs.cameraPosition.x or 0.0, gs.cameraPosition.y or 0.0,
                gs.cameraPosition.z or 0.0))
        end
        ImGui.Text("RMB+drag: look   WASD: fly   Space/Q: up/down   Shift: sprint")
    else
        ImGui.Text("Status: LOAD FAILED")
        UiTextWrapped(gs.worldStatus or "Unknown loading failure")
    end

    ImGui.Spacing()
    if exitToMissionSelectButton ~= nil and exitToMissionSelectButton:render() then
        Gameplay.requestScene(Gameplay.Scene.Lobby, "Returning to mission select...")
    end
    ImGui.End()

    ImGui.SetNextWindowPos(20, 20, ImGuiCond.Once)
    ImGui.SetNextWindowSize(460, 600, ImGuiCond.Once)
    ImGui.SetNextWindowBgAlpha(0.76)
    ImGui.Begin("GameplayScriptHarness", ImGuiWindowFlags.NoCollapse)

    ImGui.Text("Lua Gameplay Harness")
    ImGui.Separator()

    ImGui.Text(string.format("HP: %d", gs.baseHealth))
    ImGui.Text(string.format("Money: %d", gs.playerMoney))
    ImGui.Text(string.format("Wave: %d", gs.currentWave))
    ImGui.Text(string.format("Wave Active: %s", tostring(gs.waveInProgress)))
    ImGui.Text(string.format("Pre-Wave Countdown: %s (%.1fs)", tostring(gs.waveCountdownActive),
        gs.waveCountdownRemainingSeconds or 0.0))
    ImGui.Text(string.format("Round Timer: %.1fs / %.1fs", gs.waveRoundRemainingSeconds or 0.0,
        gs.waveRoundDurationSeconds or 0.0))
    ImGui.Text(string.format("To Spawn: %d", gs.enemiesToSpawn or 0))
    ImGui.Text(string.format("Alive: %d", gs.enemiesAlive or 0))
    ImGui.Text(string.format("Defeated: %d", gs.enemiesDefeated or 0))
    ImGui.Text(string.format("Route Points: %d", gs.routePointCount or 0))
    ImGui.Text(string.format("Waves Defined: %d", gs.waveCount or 0))
    ImGui.Text(string.format("Status: %s", gs.matchStatus))

    if spend25Button ~= nil and spend25Button:render() then
        local r = Gameplay.requestSpendMoney(25)
        lastResult = string.format("Spend 25 -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    if damageBase10Button ~= nil and damageBase10Button:render() then
        local r = Gameplay.requestDamageBase(10)
        lastResult = string.format("Damage 10 -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    if startWaveButton ~= nil and startWaveButton:render() then
        local r = Gameplay.requestStartWave()
        lastResult = string.format("Start wave -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    ImGui.Separator()
    ImGui.Text(lastResult)
    ImGui.End()

    ImGui.SetNextWindowPos(2000, 20, ImGuiCond.Once)
    ImGui.SetNextWindowSize(520, 680, ImGuiCond.Once)
    ImGui.SetNextWindowBgAlpha(0.76)
    ImGui.Begin("ModelDebugger", ImGuiWindowFlags.NoCollapse)

    ImGui.Text("Model Debugger (Lua-driven)")
    ImGui.Separator()
    ImGui.Text(string.format("[H] Pick Spheres: %s", debugPickSpheresVisible and "ON" or "OFF"))
    ImGui.Text(string.format("[B] Placement Bounds: %s", debugPlacementBoundsVisible and "ON" or "OFF"))

    if ImGui.SliderFloat and Gameplay.getPathCorridorHalfWidth and Gameplay.setPathCorridorHalfWidth then
        local currentHalfWidth = Gameplay.getPathCorridorHalfWidth()
        local changed, newHalfWidth = ImGui.SliderFloat("Path Corridor Half-Width", currentHalfWidth, 0.25, 10.0)
        if changed then
            Gameplay.setPathCorridorHalfWidth(newHalfWidth)
        end
    end

    if clearSelectionButton ~= nil and clearSelectionButton:render() then
        Gameplay.clearDebugSelection()
    end

    ImGui.Separator()
    local sel = Gameplay.getDebugSelection()
    ImGui.Text(string.format("Status: %s", sel.status or ""))
    ImGui.Text(string.format("Enemy Clip: %s", sel.enemyAnimationName or "none"))

    local od = sel.overlayDebug
    if od then
        ImGui.Separator()
        ImGui.Text("Overlay Debug")
        ImGui.Text(string.format("Spheres total=%d drawn=%d", od.sphereTotal or 0, od.sphereDrawn or 0))
        ImGui.Text(string.format("Reject behind=%d clipW=%d ndcZ=%d radius=%d",
            od.rejectBehindCamera or 0,
            od.rejectClipW or 0,
            od.rejectNdcZ or 0,
            od.rejectRadius or 0))
        ImGui.Text(string.format("Hovered sphere found=%d rejectReason=%d depth=%.2f rPx=%.2f",
            od.hoveredSphereFound or 0,
            od.hoveredRejectReason or 0,
            od.hoveredDepth or 0.0,
            od.hoveredRadiusPixels or 0.0))
        ImGui.Text(string.format("Display %.0fx%.0f  Render %.0fx%.0f",
            od.displayWidth or 0.0,
            od.displayHeight or 0.0,
            od.renderWidth or 0.0,
            od.renderHeight or 0.0))
        ImGui.Text(string.format("Camera yaw=%.3f pitch=%.3f", od.cameraYaw or 0.0, od.cameraPitch or 0.0))
    end

    local clips = Gameplay.getAnimationClips()
    local clipCount = clips.count or 0
    local activeClip = clips.activeIndex or -1
    local compositeMode = Gameplay.getCompositeAnimationMode()
    ImGui.Text(string.format("Clip List: %d total, active=%d", clipCount, activeClip))
    ImGui.Text(string.format("Composite Mode: %s", tostring(compositeMode)))
    if compositeToggleButton ~= nil then
        compositeToggleButton:setLabel(compositeMode and "Composite: ON" or "Composite: OFF")
        if compositeToggleButton:render() then
            local r = Gameplay.setCompositeAnimationMode(not compositeMode)
            lastResult = string.format("Toggle composite -> ok=%s reason=%s", tostring(r.ok), r.reason)
        end
    end
    if clips.names then
        for i = 1, #clips.names do
            local zeroBased = i - 1
            local marker = (zeroBased == activeClip) and "*" or " "
            ImGui.Text(string.format("%s [%d] %s", marker, zeroBased, tostring(clips.names[i])))
        end
    end

    if prevClipButton ~= nil and prevClipButton:render() then
        local r = Gameplay.setAnimationClip(math.max(0, activeClip - 1))
        lastResult = string.format("Prev clip -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end
    if nextClipButton ~= nil and nextClipButton:render() then
        local r = Gameplay.setAnimationClip(math.min(math.max(0, clipCount - 1), activeClip + 1))
        lastResult = string.format("Next clip -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end
    if selectWalkingButton ~= nil and selectWalkingButton:render() then
        local r = Gameplay.setAnimationClip("Walking")
        lastResult = string.format("Select Walking -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    local ad = sel.animationDebug
    if ad and ad.enabled then
        ImGui.Text(string.format("Debug Clip Index: %d / %d", ad.selectedClipIndex or -1, ad.clipCount or 0))
        ImGui.Text(string.format("Composite Applied Clips: %d  Mode: %s", ad.compositeAppliedClips or 0,
            tostring(ad.compositeMode)))
        ImGui.Text(string.format("Anim Time: %.3f / %.3f", ad.timeSeconds or 0.0, ad.durationSeconds or 0.0))
        local norm = 0.0
        if (ad.durationSeconds or 0.0) > 1e-6 then
            norm = (ad.timeSeconds or 0.0) / ad.durationSeconds
        end
        ImGui.Text(string.format("Anim Progress: %.1f%%", norm * 100.0))
        ImGui.Text(string.format("Tracks: %d  Keys: %d", ad.trackCount or 0, ad.keyCount or 0))
        ImGui.Text(string.format("Key Segment: %d -> %d", ad.keyIndex or 0, ad.nextKeyIndex or 0))
        ImGui.Text(string.format("Key Times: %.3f -> %.3f", ad.keyTimeSeconds or 0.0, ad.nextKeyTimeSeconds or 0.0))
        ImGui.Text(string.format("Segment Alpha: %.3f  Step: %s", ad.segmentAlpha or 0.0, tostring(ad.stepInterpolation)))
    else
        ImGui.Text("Animation Debug: unavailable")
    end

    if sel.valid then
        ImGui.Text(string.format("Group: %s", tostring(sel.group)))
        ImGui.Text(string.format("Label: %s", tostring(sel.label)))
        ImGui.Text(string.format("Mesh: %d  Node: %d  Skin: %d", sel.meshIndex or -1, sel.nodeIndex or -1,
            sel.skinIndex or -1))
        ImGui.Text(string.format("Instance: %d", sel.instanceIndex or -1))
        ImGui.Text(string.format("Distance: %.3f", sel.distance or 0.0))
        if sel.hitPosition then
            ImGui.Text(string.format("Hit Pos: (%.2f, %.2f, %.2f)", sel.hitPosition.x or 0.0, sel.hitPosition.y or 0.0,
                sel.hitPosition.z or 0.0))
        end
        if sel.hitNormal then
            ImGui.Text(string.format("Hit Nrm: (%.2f, %.2f, %.2f)", sel.hitNormal.x or 0.0, sel.hitNormal.y or 0.0,
                sel.hitNormal.z or 0.0))
        end
    else
        ImGui.Text("No selection")
        ImGui.Text("Tip: with Auto Pick ON, left-click a model in the world.")
    end

    ImGui.End()
end

function M.onExit()
    towerSlotTextures = {}
    towerUiArtTextures = {}
    lastUpgradeResult = ""

    startMatchButton = nil
    playAgainButton = nil
    backToLobbyButton = nil
    slotButtons = {}
    exitToMissionSelectButton = nil
    spend25Button = nil
    damageBase10Button = nil
    startWaveButton = nil
    clearSelectionButton = nil
    compositeToggleButton = nil
    prevClipButton = nil
    nextClipButton = nil
    selectWalkingButton = nil
    resumeGameButton = nil
    quitGameButton = nil
    applyPauseSettingsButton = nil

    escapeMenuVisible = false
    escapeMenuStatus = ""
end

return M
