local M = {}

local clickSound = nil
local closeSound = nil

local function modeLabel(mode)
    return string.format("%d x %d @ %d Hz", mode.width or 0, mode.height or 0, mode.refreshRate or 0)
end

local function playClick()
    if clickSound then
        Audio.playSfx(clickSound, false, 1.0)
    end
end

local function playClose()
    if closeSound then
        Audio.playSfx(closeSound, false, 1.0)
    end
end

function M.onEnter()
    clickSound = Audio.loadSfx("assets/audio/click.ogg")
    closeSound = Audio.loadSfx("assets/audio/close.ogg")
end

function M.onExit()
end

local function renderGfxTab(state, settings)
    local kInnerTabPadding = 16.0
    ImGui.Indent(kInnerTabPadding)

    local changed, value = ImGui.Checkbox("Fullscreen", settings.fullscreen)
    if changed then
        playClick()
        settings.fullscreen = value
    end

    if settings.fullscreen then
        if ImGui.RadioButton("Borderless (recommended)", not settings.exclusiveFullscreen) then
            playClick()
            settings.exclusiveFullscreen = false
        end
        if ImGui.RadioButton("Exclusive", settings.exclusiveFullscreen) then
            playClick()
            settings.exclusiveFullscreen = true
        end
    end

    changed, value = ImGui.Checkbox("V-Sync", settings.vSyncEnabled)
    if changed then
        playClick()
        settings.vSyncEnabled = value
    end

    if state.displayModes and #state.displayModes > 0 then
        local currentIndex = state.selectedDisplayModeIndex or 0
        local currentMode = state.displayModes[currentIndex + 1]
        if not currentMode then
            currentIndex = 0
            currentMode = state.displayModes[1]
        end

        local currentLabel = currentMode and modeLabel(currentMode) or "No mode selected"
        ImGui.TextUnformatted("Current Display Mode")
        ImGui.TextColored(0.90, 0.95, 1.0, 1.0, currentLabel)

        if ImGui.Combo then
            local modeLabels = {}
            for _, mode in ipairs(state.displayModes) do
                modeLabels[#modeLabels + 1] = modeLabel(mode)
            end

            local selectChanged, selectedIndex = ImGui.Combo("Display Mode", currentIndex, modeLabels)
            if selectChanged and state.displayModes[selectedIndex + 1] then
                local selectedMode = state.displayModes[selectedIndex + 1]
                playClick()
                state.selectedDisplayModeIndex = selectedIndex
                settings.displayWidth = selectedMode.width
                settings.displayHeight = selectedMode.height
                settings.refreshRate = selectedMode.refreshRate
            end
        else
            ImGui.TextDisabled("Dropdown unavailable in current runtime. Rebuild required.")
            local cursorX, _ = ImGui.GetCursorPos()
            ImGui.SetCursorPosX(cursorX + 16.0)
            local childVisible = ImGui.BeginChild("DisplayModeList", 260.0, 120.0)
            if childVisible then
                for i, mode in ipairs(state.displayModes) do
                    local zeroBasedIndex = i - 1
                    local selected = (state.selectedDisplayModeIndex == zeroBasedIndex)
                    local selectChanged = ImGui.Selectable(modeLabel(mode), selected)
                    if selectChanged then
                        playClick()
                        state.selectedDisplayModeIndex = zeroBasedIndex
                        settings.displayWidth = mode.width
                        settings.displayHeight = mode.height
                        settings.refreshRate = mode.refreshRate
                    end
                end
            end
            ImGui.EndChild()
        end
    end

    changed, value = ImGui.SliderInt("Graphics Quality", settings.graphicsQuality, 0, 3)
    if changed then
        playClick()
        settings.graphicsQuality = value
    end

    ImGui.Unindent(kInnerTabPadding)
end

local function renderAudioTab(settings)
    local kInnerTabPadding = 16.0
    ImGui.Indent(kInnerTabPadding)

    local changed, value = ImGui.SliderFloat("Master Volume", settings.masterVolume, 0.0, 1.0)
    if changed then
        playClick()
        settings.masterVolume = value
    end

    changed, value = ImGui.SliderFloat("Music Volume", settings.musicVolume, 0.0, 1.0)
    if changed then
        playClick()
        settings.musicVolume = value
    end

    changed, value = ImGui.SliderFloat("SFX Volume", settings.sfxVolume, 0.0, 1.0)
    if changed then
        playClick()
        settings.sfxVolume = value
    end

    changed, value = ImGui.Checkbox("Mute when unfocused", settings.muteWhenUnfocused)
    if changed then
        playClick()
        settings.muteWhenUnfocused = value
    end

    ImGui.Unindent(kInnerTabPadding)
end

local function renderGameplayTab()
    local kInnerTabPadding = 16.0
    ImGui.Indent(kInnerTabPadding)
    ImGui.TextWrapped("Gameplay settings will appear here as they are added.")
    ImGui.Unindent(kInnerTabPadding)
end

function M.render(state, dt, elapsedSeconds)
    local _ = dt
    local __ = elapsedSeconds

    local settings = state.settings
    if not settings then
        return
    end

    local dw, dh = ImGui.GetDisplaySize()
    local kOptionsW = dw * 0.95
    local kOptionsH = dh * 0.95
    ImGui.SetNextWindowPos((dw - kOptionsW) * 0.5, (dh - kOptionsH) * 0.5, ImGuiCond.Always)
    ImGui.SetNextWindowSize(kOptionsW, kOptionsH, ImGuiCond.Always)

    local flags = ImGuiWindowFlags.NoResize |
                  ImGuiWindowFlags.NoMove |
                  ImGuiWindowFlags.NoCollapse |
                  ImGuiWindowFlags.NoTitleBar
    ImGui.Begin("Options", flags)

    if HeadingFont then
        ImGui.PushFont(HeadingFont)
    end
    ImGui.TextUnformatted("Settings")
    if HeadingFont then
        ImGui.PopFont()
    end

    ImGui.Separator()

    local _, availH = ImGui.GetContentRegionAvail()
    local kFooterReserve = 56.0
    local contentHeight = math.max(0.0, availH - kFooterReserve)
    local contentVisible = ImGui.BeginChild("OptionsContent", 0.0, contentHeight)
    if contentVisible then
        local hasTabs = ImGui.BeginTabBar and ImGui.BeginTabItem and ImGui.EndTabItem and ImGui.EndTabBar
        if hasTabs and ImGui.BeginTabBar("OptionsTabs") then
            if ImGui.BeginTabItem("GFX") then
                renderGfxTab(state, settings)
                ImGui.EndTabItem()
            end

            if ImGui.BeginTabItem("Audio") then
                renderAudioTab(settings)
                ImGui.EndTabItem()
            end

            if ImGui.BeginTabItem("Gameplay") then
                renderGameplayTab()
                ImGui.EndTabItem()
            end

            ImGui.EndTabBar()
        else
            renderGfxTab(state, settings)
            ImGui.Separator()
            renderAudioTab(settings)
            ImGui.Separator()
            renderGameplayTab()
        end
    end
    ImGui.EndChild()

    if state.displayConfirmationActive then
        ImGui.OpenPopup("Confirm Display Changes")
    end

    ImGui.SetNextWindowSize(420.0, 170.0, ImGuiCond.Appearing)
    if ImGui.BeginPopupModal("Confirm Display Changes", ImGuiWindowFlags.NoResize) then
        ImGui.TextWrapped("Keep these display settings? They will be reverted automatically if not confirmed.")
        ImGui.Spacing()
        ImGui.Text(string.format("Reverting in %.1f seconds", state.displayConfirmationSecondsRemaining or 0.0))
        ImGui.Separator()

        local acceptClicked = ImGui.Button("Accept Changes", 170.0, 0.0)
        ImGui.SameLine()
        local revertClicked = ImGui.Button("Revert", 120.0, 0.0)

        if acceptClicked then
            playClick()
            Gameplay.requestAcceptDisplayChanges()
            ImGui.CloseCurrentPopup()
        elseif revertClicked then
            playClick()
            Gameplay.requestRevertDisplayChanges()
            ImGui.CloseCurrentPopup()
        end

        ImGui.EndPopup()
    end

    ImGui.Separator()
    local applyClicked = ImGui.Button("Apply", 140.0, 40.0)
    ImGui.SameLine()
    local backClicked = ImGui.Button("Back", 140.0, 40.0)

    ImGui.End()

    if applyClicked then
        playClick()
        Gameplay.requestApplySettings()
    elseif backClicked then
        playClose()
        Gameplay.requestScene(Gameplay.Scene.MainMenu, "Returning to main menu...")
    end
end

return M
