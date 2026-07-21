local M = {}

local clickSound = nil
local closeSound = nil

local kOptionsW = 760
local kOptionsH = 460

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

function M.render(state, dt, elapsedSeconds)
    local _ = dt
    local __ = elapsedSeconds

    local settings = state.settings
    if not settings then
        return
    end

    local dw, dh = ImGui.GetDisplaySize()
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

    local changed, value = ImGui.Checkbox("Fullscreen", settings.fullscreen)
    if changed then
        settings.fullscreen = value
    end

    if settings.fullscreen then
        if ImGui.RadioButton("Borderless (recommended)", not settings.exclusiveFullscreen) then
            settings.exclusiveFullscreen = false
        end
        if ImGui.RadioButton("Exclusive", settings.exclusiveFullscreen) then
            settings.exclusiveFullscreen = true
        end
    end

    changed, value = ImGui.Checkbox("V-Sync", settings.vSyncEnabled)
    if changed then
        settings.vSyncEnabled = value
    end

    if state.displayModes and #state.displayModes > 0 then
        ImGui.Text("Display Mode")
        local childVisible = ImGui.BeginChild("DisplayModeList", 0.0, 130.0)
        if childVisible then
            for i, mode in ipairs(state.displayModes) do
                local zeroBasedIndex = i - 1
                local selected = (state.selectedDisplayModeIndex == zeroBasedIndex)
                local selectChanged = ImGui.Selectable(modeLabel(mode), selected)
                if selectChanged then
                    state.selectedDisplayModeIndex = zeroBasedIndex
                    settings.displayWidth = mode.width
                    settings.displayHeight = mode.height
                    settings.refreshRate = mode.refreshRate
                end
            end
        end
        ImGui.EndChild()
    end

    changed, value = ImGui.SliderInt("Graphics Quality", settings.graphicsQuality, 0, 3)
    if changed then
        settings.graphicsQuality = value
    end

    changed, value = ImGui.SliderFloat("Master Volume", settings.masterVolume, 0.0, 1.0)
    if changed then
        settings.masterVolume = value
    end

    changed, value = ImGui.SliderFloat("Music Volume", settings.musicVolume, 0.0, 1.0)
    if changed then
        settings.musicVolume = value
    end

    changed, value = ImGui.SliderFloat("SFX Volume", settings.sfxVolume, 0.0, 1.0)
    if changed then
        settings.sfxVolume = value
    end

    changed, value = ImGui.Checkbox("Mute when unfocused", settings.muteWhenUnfocused)
    if changed then
        settings.muteWhenUnfocused = value
    end

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
