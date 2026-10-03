-- MainMenu scene UI: Play/Options/Exit navigation. Run fresh each time this
-- scene is entered (including on the app's /reload dev hotkey).

local HOVER_SFX = "assets/audio/hover.ogg"
local CLICK_SFX = "assets/audio/click.ogg"
local CLOSE_SFX = "assets/audio/close.ogg"

local entered, assetsPreloaded = false, false

local function preloadAssets()
    if assetsPreloaded then return end
    Audio.Preload(HOVER_SFX, "Sfx")
    Audio.Preload(CLICK_SFX, "Sfx")
    Audio.Preload(CLOSE_SFX, "Sfx")
    assetsPreloaded = true
end

local root = UI.CreateFrame("Frame", "MainMenuRoot")
root:SetAllPoints(UI.Root)
root:SetBackgroundColor(0x000000FF)

local background = root:CreateImage("MainMenuBackground")
background:SetAllPoints(root)
background:SetMouseEnabled(false)
background:SetFit("CONTAIN")
if not background:SetSource("assets/images/splash_screen.png") then
    local title = root:CreateFontString("Title")
    title:SetText("NodeSpire TD")
    title:SetPoint("TOP", root, "TOP", 0, 40)
end

local function makeMenuButton(name, text, imageName, yOffset, onClick)
    local normalImage = UI.LoadImage("assets/images/" .. imageName .. ".png")
    local hoverImage = UI.LoadImage("assets/images/" .. imageName .. "_hover.png") or normalImage
    local button = UI.CreateFrame("Button", name, root)
    button:SetSize(260, 110)
    button:SetPoint("CENTER", root, "CENTER", 0, yOffset)
    if normalImage then
        button:SetButtonColors(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)
        assert(button:SetImage(normalImage))
    else
        button:SetButtonColors(0x3A3F4BFF, 0x4C5566FF, 0x2A2E38FF)
        local label = button:CreateFontString(name .. "Label")
        label:SetText(text)
        label:SetAllPoints(button)
        label:SetMouseEnabled(false)
    end

    button:SetScript("OnEnter", function()
        if normalImage then assert(button:SetImage(hoverImage)) end
        Audio.Play(HOVER_SFX, "Sfx")
    end)
    button:SetScript("OnLeave", function()
        if normalImage then assert(button:SetImage(normalImage)) end
    end)
    button:SetScript("OnClick", function()
        Audio.Play(CLICK_SFX, "Sfx")
        onClick()
    end)
    return button
end

makeMenuButton("PlayButton", "Play", "play_button", -128, function()
    Scene.GoTo("Lobby")
end)

makeMenuButton("OptionsButton", "Options", "options_button", -6, function()
    Scene.GoTo("Options")
end)

makeMenuButton("ExitButton", "Exit", "quit_button", 116, function()
    Audio.Play(CLOSE_SFX, "Sfx")
    Scene.Quit()
end)

function OnEnter()
    preloadAssets()
    entered = true
end

function OnExit()
    entered = false
end

function OnShortcut(scanCode)
    return false
end
