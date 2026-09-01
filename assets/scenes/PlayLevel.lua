local M = {}

local lastResult = ""
local lastLoadoutResult = ""
local debugPickSpheresVisible = false
local debugPlacementBoundsVisible = false
local debugUiVisible = false
local towerSlotTextures = {}
local towerUiArtTextures = {}
local towerBioVisibleByKey = {}

local perfFpsInstant = 0.0
local perfFpsSmoothed = 0.0
local perfFrameMsSmoothed = 0.0
local perfSampleFrames = 0

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

local function targetingModeDisplayName(mode)
    local raw = tostring(mode or "nearest")
    if raw == "first" then
        return "First"
    elseif raw == "last" then
        return "Last"
    elseif raw == "nearest" then
        return "Nearest"
    elseif raw == "random" then
        return "Random"
    elseif raw == "highest_hp" then
        return "Highest HP"
    elseif raw == "lowest_hp" then
        return "Lowest HP"
    end
    return "Nearest"
end

local function isShiftHeld()
    if not ImGui or not ImGui.IsKeyDown or not ImGuiKey then
        return false
    end

    local leftShift = ImGuiKey.LeftShift or ImGuiKey.ModShift
    local rightShift = ImGuiKey.RightShift
    local modShift = ImGuiKey.ModShift

    local leftDown = leftShift and ImGui.IsKeyDown(leftShift)
    local rightDown = rightShift and ImGui.IsKeyDown(rightShift)
    local modDown = modShift and ImGui.IsKeyDown(modShift)
    return leftDown or rightDown or modDown
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
    local requestSelectedTowerTargetingMode = Gameplay and Gameplay["requestSelectedTowerTargetingMode"]
    local requestSellSelectedTower = Gameplay and Gameplay["requestSellSelectedTower"]

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

    local function buildEnabledEffectsSummary(node, purchasedLevels)
        local summary = {
            attackDamageAdd = 0,
            attackDamageMul = 1,
            attackRangeAdd = 0,
            attackRangeMul = 1,
            attackSpeedAdd = 0,
            attackSpeedMul = 1,
            projectileSpeedAdd = 0,
            projectileSpeedMul = 1,
            splashRadiusAdd = 0,
            splashRadiusMul = 1,
            chainRangeAdd = 0,
            chainRangeMul = 1,
            ricochetRangeAdd = 0,
            ricochetRangeMul = 1,
            projectileCountAdd = 0,
            chainTargetCountAdd = 0,
            ricochetCountAdd = 0,
        }

        local levels = node and node.upgradeLevels or nil
        if type(levels) ~= "table" then
            return summary
        end

        local count = math.max(0, math.floor(tonumber(purchasedLevels or 0) or 0))
        for i = 1, count do
            local level = levels[i]
            local fx = level and level.effects or nil
            if type(fx) == "table" then
                summary.attackDamageAdd = summary.attackDamageAdd + (tonumber(fx.attackDamageAdd or 0) or 0)
                summary.attackDamageMul = summary.attackDamageMul * (tonumber(fx.attackDamageMul or 1) or 1)
                summary.attackRangeAdd = summary.attackRangeAdd + (tonumber(fx.attackRangeAdd or 0) or 0)
                summary.attackRangeMul = summary.attackRangeMul * (tonumber(fx.attackRangeMul or 1) or 1)
                summary.attackSpeedAdd = summary.attackSpeedAdd + (tonumber(fx.attackSpeedAdd or 0) or 0)
                summary.attackSpeedMul = summary.attackSpeedMul * (tonumber(fx.attackSpeedMul or 1) or 1)
                summary.projectileSpeedAdd = summary.projectileSpeedAdd + (tonumber(fx.projectileSpeedAdd or 0) or 0)
                summary.projectileSpeedMul = summary.projectileSpeedMul * (tonumber(fx.projectileSpeedMul or 1) or 1)
                summary.splashRadiusAdd = summary.splashRadiusAdd + (tonumber(fx.splashRadiusAdd or 0) or 0)
                summary.splashRadiusMul = summary.splashRadiusMul * (tonumber(fx.splashRadiusMul or 1) or 1)
                summary.chainRangeAdd = summary.chainRangeAdd + (tonumber(fx.chainRangeAdd or 0) or 0)
                summary.chainRangeMul = summary.chainRangeMul * (tonumber(fx.chainRangeMul or 1) or 1)
                summary.ricochetRangeAdd = summary.ricochetRangeAdd + (tonumber(fx.ricochetRangeAdd or 0) or 0)
                summary.ricochetRangeMul = summary.ricochetRangeMul * (tonumber(fx.ricochetRangeMul or 1) or 1)
                summary.projectileCountAdd = summary.projectileCountAdd + (tonumber(fx.projectileCountAdd or 0) or 0)
                summary.chainTargetCountAdd = summary.chainTargetCountAdd + (tonumber(fx.chainTargetCountAdd or 0) or 0)
                summary.ricochetCountAdd = summary.ricochetCountAdd + (tonumber(fx.ricochetCountAdd or 0) or 0)
            end
        end

        return summary
    end

    local function buildNodeTooltip(node)
        local lines = {}
        table.insert(lines, tostring(node.displayName or node.id or "Talent"))
        local currentLevel = tonumber(node.currentLevel or 0) or 0
        local maxLevel = tonumber(node.maxLevel or 1) or 1
        table.insert(lines, string.format("Level %d/%d", currentLevel, maxLevel))
        if currentLevel < maxLevel then
            table.insert(lines, string.format("Cost: %d", math.floor(tonumber(node.cost or 0) or 0)))
        end
        if tonumber(node.minUpgradesRequired or 0) > 0 then
            table.insert(lines, string.format("Needs total upgrades: %d", tonumber(node.minUpgradesRequired or 0)))
        end
        if node.description and node.description ~= "" then
            table.insert(lines, tostring(node.description))
        end

        local fx = node.effects or {}
        if currentLevel >= maxLevel then
            fx = buildEnabledEffectsSummary(node, currentLevel)
        end
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
            local panelW, panelH = 960, 680
            ImGui.SetNextWindowPos(displayW - panelW - 20, 90, ImGuiCond.FirstUseEver)
            ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.FirstUseEver)
            ImGui.SetNextWindowBgAlpha(0.84)
            local towerWindowTitle = string.format("%s###TowerUpgrades", tostring(state.displayName or state.towerId or "Tower"))
            ImGui.Begin(towerWindowTitle, ImGuiWindowFlags.NoCollapse)

            local function clampColorComponent(value)
                return math.max(0.0, math.min(1.0, value))
            end

            local function drawOutlinedRect(x1, y1, x2, y2, r, g, b, a, thickness)
                if not ImGui.DrawLine then
                    return
                end
                ImGui.DrawLine(x1, y1, x2, y1, r, g, b, a, thickness)
                ImGui.DrawLine(x2, y1, x2, y2, r, g, b, a, thickness)
                ImGui.DrawLine(x2, y2, x1, y2, r, g, b, a, thickness)
                ImGui.DrawLine(x1, y2, x1, y1, r, g, b, a, thickness)
            end

            local function drawStateMark(nodeState, x, y, size, color)
                if not ImGui.DrawLine then
                    return
                end

                local r = clampColorComponent(color[1])
                local g = clampColorComponent(color[2])
                local b = clampColorComponent(color[3])
                local a = clampColorComponent(color[4] or 1.0)

                if nodeState == "locked" then
                    ImGui.DrawLine(x + 7, y + 7, x + size - 7, y + size - 7, r, g, b, a, 2.5)
                    ImGui.DrawLine(x + size - 7, y + 7, x + 7, y + size - 7, r, g, b, a, 2.5)
                elseif nodeState == "purchased" then
                    ImGui.DrawLine(x + 8, y + size * 0.54, x + size * 0.40, y + size - 9, r, g, b, a, 3.0)
                    ImGui.DrawLine(x + size * 0.40, y + size - 9, x + size - 8, y + 9, r, g, b, a, 3.0)
                elseif nodeState == "hovered" then
                    drawOutlinedRect(x - 2, y - 2, x + size + 2, y + size + 2, r, g, b, a, 2.0)
                end
            end

            local graphAvailW, graphAvailH = ImGui.GetContentRegionAvail()
            local graphHeight = math.max(280, math.min(360, math.floor((graphAvailH or 0) * 0.62)))
            local hasGraphChild = false
            if ImGui.BeginChild then
                hasGraphChild = true
                ImGui.BeginChild("TalentGraphScroll", 0, graphHeight, 0)
            end

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

            local nodeIconSize = 40
            local nodeStepXBase = 80
            local nodeStepXMin = 60
            local nodeStepXMax = 124
            local nodeStepXDepth3Boost = 1.22
            local nodeStepXDepth3Max = 160
            local rowGapY = 18
            local nodeCenters = {}
            local nodeLayoutXById = {}

            for depth = 0, maxDepth do
                local row = rowSorted[depth] or {}

                if #row == 0 then
                    ImGui.Dummy(1, rowGapY)
                else
                    local desiredXByNodeId = {}
                    for i = 1, #row do
                        local node = row[i]
                        local nodeId = tostring(node.id or "")
                        local parentId = tostring(node.parent or "")
                        if parentId ~= "" and nodeLayoutXById[parentId] ~= nil then
                            desiredXByNodeId[nodeId] = nodeLayoutXById[parentId]
                        else
                            desiredXByNodeId[nodeId] = tonumber(node.column or (i - 1)) or (i - 1)
                        end
                    end

                    table.sort(row, function(a, b)
                        local aId = tostring(a.id or "")
                        local bId = tostring(b.id or "")
                        local aParent = tostring(a.parent or "")
                        local bParent = tostring(b.parent or "")
                        local ax = desiredXByNodeId[aId] or 0
                        local bx = desiredXByNodeId[bId] or 0
                        if ax ~= bx then
                            return ax < bx
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
                end

                local availW, _ = ImGui.GetContentRegionAvail()
                local rowStepX = nodeStepXBase
                if #row > 1 then
                    local fitStep = (availW - nodeIconSize) / (#row - 1)
                    rowStepX = math.max(nodeStepXMin, math.min(nodeStepXMax, fitStep))
                end
                if depth == 2 then
                    rowStepX = math.min(nodeStepXDepth3Max, rowStepX * nodeStepXDepth3Boost)
                end

                local siblingFanStepUnits = (depth >= 2) and 1.1 or 1.0
                local groupGapUnits = 1.35

                local rowDesired = {}
                local rowPos = {}
                local desiredByNodeId = {}
                local solvedByNodeId = {}
                local groupsByParent = {}
                local groups = {}
                for i = 1, #row do
                    local node = row[i]
                    local nodeId = tostring(node.id or "")
                    local parentId = tostring(node.parent or "")
                    if parentId == "" then
                        parentId = "__root__"
                    end

                    local group = groupsByParent[parentId]
                    if not group then
                        local pivot = nil
                        if parentId ~= "__root__" and nodeLayoutXById[parentId] ~= nil then
                            pivot = nodeLayoutXById[parentId]
                        else
                            pivot = tonumber(node.column or (i - 1)) or (i - 1)
                        end
                        group = { parentId = parentId, pivot = pivot, nodes = {} }
                        groupsByParent[parentId] = group
                        table.insert(groups, group)
                    end
                    table.insert(group.nodes, node)
                end

                for _, group in ipairs(groups) do
                    table.sort(group.nodes, function(a, b)
                        local aId = tostring(a.id or "")
                        local bId = tostring(b.id or "")
                        local parentId = tostring(a.parent or "")
                        local rankMap = childrenOrderRankByParent[parentId]
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
                        local ac = tonumber(a.column or 0) or 0
                        local bc = tonumber(b.column or 0) or 0
                        if ac ~= bc then
                            return ac < bc
                        end
                        return aId < bId
                    end)

                    local childCount = #group.nodes
                    local span = (childCount > 1) and ((childCount - 1) * siblingFanStepUnits) or 0.0
                    group.span = span
                    group.desiredStart = group.pivot - (span * 0.5)
                end

                table.sort(groups, function(a, b)
                    if a.pivot ~= b.pivot then
                        return a.pivot < b.pivot
                    end
                    return a.parentId < b.parentId
                end)

                local previousGroupEnd = nil
                for _, group in ipairs(groups) do
                    local start = group.desiredStart
                    if previousGroupEnd ~= nil and start < (previousGroupEnd + groupGapUnits) then
                        start = previousGroupEnd + groupGapUnits
                    end

                    local childCount = #group.nodes
                    for childIndex = 1, childCount do
                        local node = group.nodes[childIndex]
                        local nodeId = tostring(node.id or "")
                        local centeredIndex = childIndex - ((childCount + 1) * 0.5)
                        desiredByNodeId[nodeId] = group.pivot + (centeredIndex * siblingFanStepUnits)
                        solvedByNodeId[nodeId] = start + ((childIndex - 1) * siblingFanStepUnits)
                    end

                    previousGroupEnd = start + group.span
                end

                table.sort(row, function(a, b)
                    local aId = tostring(a.id or "")
                    local bId = tostring(b.id or "")
                    local ax = solvedByNodeId[aId] or 0
                    local bx = solvedByNodeId[bId] or 0
                    if ax ~= bx then
                        return ax < bx
                    end
                    return aId < bId
                end)

                local desiredSum = 0.0
                for i = 1, #row do
                    local nodeId = tostring(row[i].id or "")
                    rowDesired[i] = desiredByNodeId[nodeId] or (i - 1)
                    rowPos[i] = solvedByNodeId[nodeId] or rowDesired[i]
                    desiredSum = desiredSum + rowDesired[i]
                end

                if #row > 0 then
                    local desiredMean = desiredSum / #row
                    local placedSum = 0.0
                    for i = 1, #row do
                        placedSum = placedSum + rowPos[i]
                    end
                    local placedMean = placedSum / #row
                    local shift = desiredMean - placedMean
                    for i = 1, #row do
                        rowPos[i] = rowPos[i] + shift
                    end
                end

                local minPos = 0.0
                local maxPos = 0.0
                if #row > 0 then
                    minPos = rowPos[1]
                    maxPos = rowPos[1]
                    for i = 2, #row do
                        if rowPos[i] < minPos then
                            minPos = rowPos[i]
                        end
                        if rowPos[i] > maxPos then
                            maxPos = rowPos[i]
                        end
                    end
                end

                local rowWidth = ((maxPos - minPos) * rowStepX) + nodeIconSize
                local rowStartX = math.max(6, (availW - rowWidth) * 0.5)
                local _, rowStartY = ImGui.GetCursorPos()

                for i = 1, #row do
                    local node = row[i]
                    local nodeX = rowStartX + ((rowPos[i] - minPos) * rowStepX)
                    ImGui.SetCursorPos(nodeX, rowStartY)

                    local unlocked = node.unlocked == true
                    local currentLevel = tonumber(node.currentLevel or 0) or 0
                    local canUnlock = node.canUnlock == true
                    local maxLevel = tonumber(node.maxLevel or 1) or 1
                    local purchased = unlocked or currentLevel > 0
                    local maxedOut = currentLevel >= maxLevel
                    local lockedOut = not purchased and not canUnlock
                    local hoveredState = false
                    local stateName = "available"
                    local stateColor = uiConfig.accent

                    if purchased then
                        stateName = "purchased"
                        stateColor = uiConfig.unlocked
                        ImGui.PushStyleColor(ImGuiCol.Button, clampColorComponent(uiConfig.unlocked[1] + 0.01),
                            clampColorComponent(uiConfig.unlocked[2] + 0.01), clampColorComponent(uiConfig.unlocked[3] + 0.01), 0.98)
                        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, clampColorComponent(uiConfig.unlocked[1] + 0.10),
                            clampColorComponent(uiConfig.unlocked[2] + 0.14), clampColorComponent(uiConfig.unlocked[3] + 0.08), 1.0)
                        ImGui.PushStyleColor(ImGuiCol.ButtonActive, clampColorComponent(uiConfig.unlocked[1] - 0.02),
                            clampColorComponent(uiConfig.unlocked[2] - 0.05), clampColorComponent(uiConfig.unlocked[3] - 0.03), 1.0)
                    elseif canUnlock then
                        stateName = "available"
                        stateColor = uiConfig.accent
                        ImGui.PushStyleColor(ImGuiCol.Button, clampColorComponent(uiConfig.accent[1] + 0.01),
                            clampColorComponent(uiConfig.accent[2] + 0.01), clampColorComponent(uiConfig.accent[3] + 0.01), 0.98)
                        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, clampColorComponent(uiConfig.accent[1] + 0.15),
                            clampColorComponent(uiConfig.accent[2] + 0.13), clampColorComponent(uiConfig.accent[3] + 0.06), 1.0)
                        ImGui.PushStyleColor(ImGuiCol.ButtonActive, clampColorComponent(uiConfig.accent[1] - 0.06),
                            clampColorComponent(uiConfig.accent[2] - 0.05), clampColorComponent(uiConfig.accent[3] - 0.02), 1.0)
                    else
                        stateName = "locked"
                        stateColor = uiConfig.locked
                        ImGui.PushStyleColor(ImGuiCol.Button, clampColorComponent(uiConfig.locked[1]),
                            clampColorComponent(uiConfig.locked[2]), clampColorComponent(uiConfig.locked[3]), 0.82)
                        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, clampColorComponent(uiConfig.locked[1] + 0.03),
                            clampColorComponent(uiConfig.locked[2] + 0.03), clampColorComponent(uiConfig.locked[3] + 0.03), 0.88)
                        ImGui.PushStyleColor(ImGuiCol.ButtonActive, clampColorComponent(uiConfig.locked[1] - 0.03),
                            clampColorComponent(uiConfig.locked[2] - 0.03), clampColorComponent(uiConfig.locked[3] - 0.03), 0.90)
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

                    hoveredState = ImGui.IsItemHovered and ImGui.IsItemHovered() or false

                    if hoveredState then
                        drawOutlinedRect(iconX - 3, iconY - 3, iconX + nodeIconSize + 3, iconY + nodeIconSize + 3,
                            clampColorComponent(stateColor[1] + 0.16), clampColorComponent(stateColor[2] + 0.16),
                            clampColorComponent(stateColor[3] + 0.10), 0.95, 2.5)
                    end

                    if purchased then
                        drawOutlinedRect(iconX - 1, iconY - 1, iconX + nodeIconSize + 1, iconY + nodeIconSize + 1,
                            clampColorComponent(uiConfig.unlocked[1] + 0.10), clampColorComponent(uiConfig.unlocked[2] + 0.12),
                            clampColorComponent(uiConfig.unlocked[3] + 0.06), 0.95, 1.8)
                        if maxedOut then
                            drawStateMark("purchased", iconX, iconY, nodeIconSize,
                                { clampColorComponent(uiConfig.unlocked[1] + 0.16),
                                  clampColorComponent(uiConfig.unlocked[2] + 0.22),
                                  clampColorComponent(uiConfig.unlocked[3] + 0.12), 1.0 })
                        end
                    elseif lockedOut then
                        drawOutlinedRect(iconX - 1, iconY - 1, iconX + nodeIconSize + 1, iconY + nodeIconSize + 1,
                            clampColorComponent(uiConfig.locked[1] + 0.12), clampColorComponent(uiConfig.locked[2] + 0.12),
                            clampColorComponent(uiConfig.locked[3] + 0.12), 0.92, 1.6)
                        drawStateMark("locked", iconX, iconY, nodeIconSize,
                            { clampColorComponent(uiConfig.locked[1] + 0.14), clampColorComponent(uiConfig.locked[2] + 0.14),
                              clampColorComponent(uiConfig.locked[3] + 0.14), 1.0 })
                    elseif hoveredState then
                        drawStateMark("hovered", iconX, iconY, nodeIconSize,
                            { clampColorComponent(uiConfig.accent[1] + 0.20), clampColorComponent(uiConfig.accent[2] + 0.18),
                              clampColorComponent(uiConfig.accent[3] + 0.08), 1.0 })
                    end

                    local nodeTopCenterX = iconX + nodeIconSize * 0.5
                    local nodeTopY = iconY
                    local nodeBottomY = iconY + nodeIconSize

                    local nodeId = tostring(node.id or "")
                    if nodeId ~= "" then
                        nodeCenters[nodeId] = { x = nodeTopCenterX, yTop = nodeTopY, yBottom = nodeBottomY }
                        nodeLayoutXById[nodeId] = rowPos[i]
                    end

                    local parentId = tostring(node.parent or "")
                    if parentId ~= "" and nodeCenters[parentId] and ImGui.DrawLine then
                        local p = nodeCenters[parentId]
                        ImGui.DrawLine(p.x, p.yBottom, nodeTopCenterX,
                            nodeTopY,
                            uiConfig.accent[1], uiConfig.accent[2], uiConfig.accent[3], 0.78, 1.6)
                    end

                    if clicked then
                        if not lockedOut and requestSelectedTowerUpgrade then
                            requestSelectedTowerUpgrade(tostring(node.id or ""))
                        end
                    end
                    if ImGui.IsItemHovered and ImGui.SetTooltip and ImGui.IsItemHovered() then
                        ImGui.SetTooltip(buildNodeTooltip(node))
                    end
                    ImGui.PopStyleColor(3)
                end

                if #row > 0 then
                    ImGui.SetCursorPos(rowStartX, rowStartY + nodeIconSize)
                    if depth < maxDepth then
                        ImGui.Dummy(1, rowGapY)
                    else
                        ImGui.Dummy(1, 8)
                    end
                end
            end

            if hasGraphChild and ImGui.EndChild then
                ImGui.EndChild()
            end

            local towerSpent = tonumber(state.totalSpent or 0) or 0
            if towerSpent <= 0 then
                towerSpent = tonumber(state.baseCost or 0) or 0
                local nodes = state.nodes or {}
                for i = 1, #nodes do
                    local node = nodes[i]
                    local currentLevel = tonumber(node.currentLevel or 0) or 0
                    local levels = node.upgradeLevels or {}
                    local appliedLevels = math.min(currentLevel, #levels)
                    for levelIndex = 1, appliedLevels do
                        local levelData = levels[levelIndex]
                        towerSpent = towerSpent + (tonumber(levelData and levelData.cost or 0) or 0)
                    end
                end
            end
            local towerDamageDealt = tonumber(state.totalDamageDealt or 0) or 0

            ImGui.Separator()
            ImGui.Text(string.format("TYPE %s   SPENT %d   DMG %.1f", tostring(state.damageType or "physical"),
                math.floor(math.max(0, towerSpent)), towerDamageDealt))
            ImGui.Text(string.format("DMG %.1f   AP %.1f   RNG %.1f",
                tonumber(state.attackDamage or 0),
                tonumber(state.armorPiercing or 0),
                tonumber(state.attackRange or 0)))
            ImGui.Text(string.format("SPD %.2f   PROJ %.1f   SPL %.1f",
                tonumber(state.attackSpeed or 0),
                tonumber(state.projectileSpeed or 0),
                tonumber(state.splashRadius or 0)))
            ImGui.Text(string.format("SHOT %d   CHAIN %d @ %.1f   BOUNCE %d @ %.1f",
                tonumber(state.projectileCount or 1),
                tonumber(state.chainTargetCount or 1),
                tonumber(state.chainRange or 0),
                tonumber(state.ricochetCount or 0),
                tonumber(state.ricochetRange or 0)))

            ImGui.Separator()
            local towerKey = string.format("%s:%d", tostring(state.towerId or "tower"),
                tonumber(state.towerInstanceOrdinal or 0) or 0)

            local bioVisible = towerBioVisibleByKey[towerKey] == true
            if ImGui.Button("Bio") then
                towerBioVisibleByKey[towerKey] = not bioVisible
                bioVisible = towerBioVisibleByKey[towerKey] == true
            end

            ImGui.SameLine()
            local targetModes = { "first", "last", "nearest", "random", "highest_hp", "lowest_hp" }
            local currentTargetMode = tostring(state.targetingMode or "nearest")
            local currentTargetIndex = 3
            for i = 1, #targetModes do
                if targetModes[i] == currentTargetMode then
                    currentTargetIndex = i
                    break
                end
            end
            local targetLabel = string.format("Target: %s", targetingModeDisplayName(currentTargetMode))
            if ImGui.Button(targetLabel) then
                local nextIndex = (currentTargetIndex % #targetModes) + 1
                local nextMode = targetModes[nextIndex]
                if requestSelectedTowerTargetingMode then
                    requestSelectedTowerTargetingMode(nextMode)
                end
            end

            ImGui.SameLine()
            local sellPopupId = "SellTowerConfirm"
            if ImGui.Button("Sell (80%)") and requestSellSelectedTower then
                if isShiftHeld() then
                    requestSellSelectedTower()
                elseif ImGui.OpenPopup then
                    ImGui.OpenPopup(sellPopupId)
                end
            end

            if ImGui.BeginPopupModal and ImGui.EndPopup and ImGui.BeginPopupModal(sellPopupId, ImGuiWindowFlags.AlwaysAutoResize) then
                ImGui.Text("Sell this tower for 80% of invested cost?")
                ImGui.TextDisabled("Tip: hold Shift while clicking Sell to skip this dialog.")
                ImGui.Separator()

                if ImGui.Button("Confirm Sell") and requestSellSelectedTower then
                    requestSellSelectedTower()
                    if ImGui.CloseCurrentPopup then
                        ImGui.CloseCurrentPopup()
                    end
                end
                ImGui.SameLine()
                if ImGui.Button("Cancel") and ImGui.CloseCurrentPopup then
                    ImGui.CloseCurrentPopup()
                end

                ImGui.EndPopup()
            end

            if bioVisible then
                ImGui.Separator()
                UiTextWrapped(state.bio ~= "" and state.bio or "No bio set for this tower yet.")
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

    local frameDt = tonumber(dt or 0.0) or 0.0
    if frameDt > 1e-6 then
        local instantFps = 1.0 / frameDt
        local instantMs = frameDt * 1000.0
        perfFpsInstant = instantFps
        if perfSampleFrames <= 0 then
            perfFpsSmoothed = instantFps
            perfFrameMsSmoothed = instantMs
        else
            local alpha = 0.10
            perfFpsSmoothed = perfFpsSmoothed + ((instantFps - perfFpsSmoothed) * alpha)
            perfFrameMsSmoothed = perfFrameMsSmoothed + ((instantMs - perfFrameMsSmoothed) * alpha)
        end
        perfSampleFrames = perfSampleFrames + 1
    end

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
        if isMouseClicked(imguiMouseButton.Right, false) and hasActivePlacement() then
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
        if debugUiVisible and placement.debug then
            local pd = placement.debug
            ImGui.Text(string.format(
                "Bungee active=%s anchor=%s rawValid=%s",
                tostring(pd.bungeeActive),
                tostring(pd.hasAnchor),
                tostring(pd.rawCandidateValid)))
            ImGui.Text(string.format(
                "Anchor->Candidate %.2f  |  Resolved->Candidate %.2f",
                tonumber(pd.anchorToCandidateDistance or 0.0),
                tonumber(pd.resolvedToCandidateDistance or 0.0)))
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
    ImGui.Text(string.format("FPS %.1f (avg %.1f)  |  Frame %.2f ms", perfFpsInstant or 0.0, perfFpsSmoothed or 0.0,
        perfFrameMsSmoothed or 0.0))

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
    ImGui.Text(string.format("FPS %.1f (avg %.1f)  |  Frame %.2f ms", perfFpsInstant or 0.0, perfFpsSmoothed or 0.0,
        perfFrameMsSmoothed or 0.0))
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
    towerBioVisibleByKey = {}
    perfFpsInstant = 0.0
    perfFpsSmoothed = 0.0
    perfFrameMsSmoothed = 0.0
    perfSampleFrames = 0

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
