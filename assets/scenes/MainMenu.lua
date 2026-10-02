-- MainMenu scene UI: Play/Options/Exit navigation. Run fresh each time this
-- scene is entered (including on the app's /reload dev hotkey).

local HOVER_SFX = "assets/audio/hover.ogg"
local CLICK_SFX = "assets/audio/click.ogg"
local CLOSE_SFX = "assets/audio/close.ogg"

Audio.Preload(HOVER_SFX, "Sfx")
Audio.Preload(CLICK_SFX, "Sfx")
Audio.Preload(CLOSE_SFX, "Sfx")

local root = UI.CreateFrame("Frame", "MainMenuRoot")
root:SetAllPoints(UI.Root)

local title = root:CreateFontString("Title")
title:SetText("NodeSpire TD")
title:SetPoint("TOP", root, "TOP", 0, 80)

local function makeMenuButton(name, text, yOffset, onClick)
    local button = UI.CreateFrame("Button", name, root)
    button:SetSize(240, 48)
    button:SetPoint("CENTER", root, "CENTER", 0, yOffset)
    button:SetButtonColors(0x3A3F4BFFu, 0x4C5566FFu, 0x2A2E38FFu)

    local label = button:CreateFontString(name .. "Label")
    label:SetText(text)
    label:SetAllPoints(button)

    button:SetScript("OnEnter", function()
        Audio.Play(HOVER_SFX, "Sfx")
    end)
    button:SetScript("OnClick", function()
        Audio.Play(CLICK_SFX, "Sfx")
        onClick()
    end)
    return button
end

makeMenuButton("PlayButton", "Play", -20, function()
    Scene.GoTo("Lobby")
end)

makeMenuButton("OptionsButton", "Options", 40, function()
    Scene.GoTo("Options")
end)

makeMenuButton("ExitButton", "Exit", 100, function()
    Audio.Play(CLOSE_SFX, "Sfx")
    Scene.Quit()
end)
