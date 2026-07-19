local M = {}

local lastResult = ""
local lastLoadoutResult = ""
local debugAutoPick = true
local debugPickSpheresVisible = false
local debugUiVisible = true
local towerSlotTextures = {}

local function UiTextWrapped(text)
    if ImGui.TextWrapped then
        ImGui.TextWrapped(tostring(text or ""))
    else
        ImGui.Text(tostring(text or ""))
    end
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

function M.onEnter()
    lastResult = "PlayLevel script loaded"
    Gameplay.setDebugPickEnabled(debugAutoPick)
    if Gameplay.getDebugPickSpheresVisible then
        debugPickSpheresVisible = Gameplay.getDebugPickSpheresVisible()
    end
    if Gameplay.setDebugPickSpheresVisible then
        Gameplay.setDebugPickSpheresVisible(debugPickSpheresVisible)
    end
end

function M.render(state, dt, elapsed)
    local gs = Gameplay.getState()
    local frameResult = {}

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

    if gs.worldLoading then
        local displayW, displayH = ImGui.GetDisplaySize()
        local panelW, panelH = 600, 260

        ImGui.SetNextWindowPos((displayW - panelW) * 0.5, (displayH - panelH) * 0.5, ImGuiCond.Once)
        ImGui.SetNextWindowSize(panelW, panelH, ImGuiCond.Once)
        ImGui.SetNextWindowBgAlpha(0.88)
        ImGui.Begin("WorldLoading", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoScrollbar)
        ImGui.Text(string.format("Loading  %s", state.activeLevelName or "Level"))
        ImGui.Separator()
        ImGui.Spacing()
        ImGui.Text(tostring(gs.loadActivity or "Starting..."))
        ImGui.Spacing()
        ImGui.ProgressBar(gs.loadProgress or 0.0, -1.0, 18.0, "")
        ImGui.Text(string.format("  %.0f%%", (gs.loadProgress or 0.0) * 100.0))
        ImGui.End()
        return frameResult
    end
    
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

    local slotW, slotH = 150, 108
    local previewH = 52
    local gap = 8
    local barW = slotW * 5 + gap * 4 + 20
    local barH = slotH + 54
    ImGui.SetNextWindowPos((displayW - barW) * 0.5, displayH - barH - 12, ImGuiCond.Always)
    ImGui.SetNextWindowSize(barW, barH, ImGuiCond.Always)
    ImGui.SetNextWindowBgAlpha(0.84)
    ImGui.Begin("TowerLoadout", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoResize)

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

        ImGui.BeginGroup()
        local previewClicked = false
        if available then
            local preview = getTowerPreviewTexture(slot)
            if preview then
                if ImGui.ImageButton then
                    previewClicked = ImGui.ImageButton(string.format("tower_preview_%d", i), preview, slotW, previewH)
                else
                    ImGui.Image(preview, slotW, previewH)
                end
            else
                previewClicked = ImGui.Button(string.format("[%d] Preview", i), slotW, previewH)
            end
        else
            previewClicked = ImGui.Button(string.format("[%d] Empty", i), slotW, previewH)
        end

        local label = string.format("[%d]", i)
        if available then
            local displayName = tostring((slot and slot.displayName) or (slot and slot.id) or "Tower")
            local cost = math.floor(tonumber((slot and slot.cost) or 0) or 0)
            label = string.format("[%d] %s\n$%d", i, displayName, cost)
        else
            label = string.format("[%d] Empty", i)
        end

        local slotButtonClicked = ImGui.Button(label, slotW, slotH - previewH)
        if previewClicked or slotButtonClicked then
            if available and Gameplay.selectTowerSlot then
                local r = Gameplay.selectTowerSlot(i)
                lastLoadoutResult = string.format("Slot %d -> ok=%s reason=%s", i, tostring(r.ok), tostring(r.reason))
            end
        end

        ImGui.EndGroup()

        if selected then
            ImGui.PopStyleColor(3)
        end
    end

    if placement and placement.active then
        local reason = tostring(placement.reason or "")
        local verdict = placement.canPlace and "VALID" or "INVALID"
        ImGui.Text(string.format("Placement: %s  |  %s", verdict, tostring(placement.displayName or placement.towerId or "tower")))
        ImGui.Text(string.format("Range: %.1f  |  Esc: cancel", tonumber(placement.attackRange or 0.0)))
        if reason ~= "" then
            UiTextWrapped(reason)
        end
    else
        ImGui.Text("Select a tower with 1-5 or click a slot. Esc exits placement mode.")
    end

    if lastLoadoutResult ~= "" then
        UiTextWrapped(lastLoadoutResult)
    end

    ImGui.End()    
    
    if not debugUiVisible then
        return frameResult
    end

    ImGui.SetNextWindowPos(16, displayH - hudH - 16, ImGuiCond.Once)
    ImGui.SetNextWindowSize(hudW, hudH, ImGuiCond.Once)
    ImGui.SetNextWindowBgAlpha(0.70)
    ImGui.Begin("WorldHUD", ImGuiWindowFlags.NoCollapse + ImGuiWindowFlags.NoTitleBar + ImGuiWindowFlags.NoScrollbar)
    ImGui.Text(state.activeLevelName or "")
    ImGui.Separator()

    if gs.worldLoaded then
        ImGui.Text(string.format("Meshes %d  |  Verts %d  |  Tris %d", gs.meshCount or 0, gs.vertexCount or 0, gs.triCount or 0))
        ImGui.Text(string.format("Base HP %d  |  Money %d  |  Wave %d", gs.baseHealth, gs.playerMoney, gs.currentWave))
        ImGui.Text(string.format("Enemy Clip %s", gs.enemyAnimationName or "none"))
        ImGui.Text(string.format("To Spawn %d  |  Alive %d  |  Defeated %d", gs.enemiesToSpawn or 0, gs.enemiesAlive or 0, gs.enemiesDefeated or 0))
        ImGui.Text(string.format("Path Points %d  |  Waves %d", gs.routePointCount or 0, gs.waveCount or 0))
        if gs.cameraPosition then
            ImGui.Text(string.format("Pos (%.1f, %.1f, %.1f)", gs.cameraPosition.x or 0.0, gs.cameraPosition.y or 0.0, gs.cameraPosition.z or 0.0))
        end
        ImGui.Text("RMB+drag: look   WASD: fly   Space/Q: up/down   Shift: sprint")
    else
        ImGui.Text("Status: LOAD FAILED")
        UiTextWrapped(gs.worldStatus or "Unknown loading failure")
    end

    ImGui.Spacing()
    if ImGui.Button("Exit to Mission Select", -1.0, 30.0) then
        frameResult.requestTransition = true
        frameResult.target = "LevelSelection"
        frameResult.message = "Returning to mission select..."
        frameResult.duration = 0.0
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
    ImGui.Text(string.format("Pre-Wave Countdown: %s (%.1fs)", tostring(gs.waveCountdownActive), gs.waveCountdownRemainingSeconds or 0.0))
    ImGui.Text(string.format("Round Timer: %.1fs / %.1fs", gs.waveRoundRemainingSeconds or 0.0, gs.waveRoundDurationSeconds or 0.0))
    ImGui.Text(string.format("To Spawn: %d", gs.enemiesToSpawn or 0))
    ImGui.Text(string.format("Alive: %d", gs.enemiesAlive or 0))
    ImGui.Text(string.format("Defeated: %d", gs.enemiesDefeated or 0))
    ImGui.Text(string.format("Route Points: %d", gs.routePointCount or 0))
    ImGui.Text(string.format("Waves Defined: %d", gs.waveCount or 0))
    ImGui.Text(string.format("Status: %s", gs.matchStatus))

    if ImGui.Button("Spend 25", -1, 0) then
        local r = Gameplay.requestSpendMoney(25)
        lastResult = string.format("Spend 25 -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    if ImGui.Button("Damage Base 10", -1, 0) then
        local r = Gameplay.requestDamageBase(10)
        lastResult = string.format("Damage 10 -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    if ImGui.Button("Start Wave", -1, 0) then
        local r = Gameplay.requestStartWave()
        lastResult = string.format("Start wave -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    ImGui.Separator()
    ImGui.Text(lastResult)
    ImGui.End()

    ImGui.SetNextWindowPos(1400, 20, ImGuiCond.Once)
    ImGui.SetNextWindowSize(520, 680, ImGuiCond.Once)
    ImGui.SetNextWindowBgAlpha(0.76)
    ImGui.Begin("ModelDebugger", ImGuiWindowFlags.NoCollapse)

    ImGui.Text("Model Debugger (Lua-driven)")
    ImGui.Separator()
    ImGui.Text(string.format("[H] Pick Spheres: %s", debugPickSpheresVisible and "ON" or "OFF"))

    if ImGui.Button(debugAutoPick and "Auto Pick: ON" or "Auto Pick: OFF", 160, 0) then
        debugAutoPick = not debugAutoPick
        Gameplay.setDebugPickEnabled(debugAutoPick)
    end
    if ImGui.Button("Pick At Cursor", 160, 0) then
        local pick = Gameplay.pickAtCursor()
        lastResult = string.format("Pick at cursor -> hit=%s", tostring(pick.hit))
    end
    if ImGui.Button("Clear Selection", 160, 0) then
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
    if ImGui.Button(compositeMode and "Composite: ON" or "Composite: OFF", 160, 0) then
        local r = Gameplay.setCompositeAnimationMode(not compositeMode)
        lastResult = string.format("Toggle composite -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end
    if clips.names then
        for i = 1, #clips.names do
            local zeroBased = i - 1
            local marker = (zeroBased == activeClip) and "*" or " "
            ImGui.Text(string.format("%s [%d] %s", marker, zeroBased, tostring(clips.names[i])))
        end
    end

    if ImGui.Button("Prev Clip", 160, 0) then
        local r = Gameplay.setAnimationClip(math.max(0, activeClip - 1))
        lastResult = string.format("Prev clip -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end
    if ImGui.Button("Next Clip", 160, 0) then
        local r = Gameplay.setAnimationClip(math.min(math.max(0, clipCount - 1), activeClip + 1))
        lastResult = string.format("Next clip -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end
    if ImGui.Button("Select Walking", 160, 0) then
        local r = Gameplay.setAnimationClip("Walking")
        lastResult = string.format("Select Walking -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    local ad = sel.animationDebug
    if ad and ad.enabled then
        ImGui.Text(string.format("Debug Clip Index: %d / %d", ad.selectedClipIndex or -1, ad.clipCount or 0))
        ImGui.Text(string.format("Composite Applied Clips: %d  Mode: %s", ad.compositeAppliedClips or 0, tostring(ad.compositeMode)))
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
        ImGui.Text(string.format("Mesh: %d  Node: %d  Skin: %d", sel.meshIndex or -1, sel.nodeIndex or -1, sel.skinIndex or -1))
        ImGui.Text(string.format("Instance: %d", sel.instanceIndex or -1))
        ImGui.Text(string.format("Distance: %.3f", sel.distance or 0.0))
        if sel.hitPosition then
            ImGui.Text(string.format("Hit Pos: (%.2f, %.2f, %.2f)", sel.hitPosition.x or 0.0, sel.hitPosition.y or 0.0, sel.hitPosition.z or 0.0))
        end
        if sel.hitNormal then
            ImGui.Text(string.format("Hit Nrm: (%.2f, %.2f, %.2f)", sel.hitNormal.x or 0.0, sel.hitNormal.y or 0.0, sel.hitNormal.z or 0.0))
        end
    else
        ImGui.Text("No selection")
        ImGui.Text("Tip: with Auto Pick ON, left-click a model in the world.")
    end

    ImGui.End()

    return frameResult
end

function M.onExit()
    towerSlotTextures = {}
end

return M
