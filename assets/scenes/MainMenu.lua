local M = {}

local kMenuW = 420
local kMenuH = 480

local backTexture = nil
local playButtonTexture = nil
local playButtonHoverTexture = nil
local optionsButtonTexture = nil
local optionsButtonHoverTexture = nil
local quitButtonTexture = nil
local quitButtonHoverTexture = nil

local clickSound = nil
local closeSound = nil
local hoverSound = nil

local menuState = {
    playHovered = false,
    optionsHovered = false,
    quitHovered = false
}

function M.onEnter()
    local tex, err = Texture.load(VulkanContext, "assets/images/splash_screen.png")
    if tex then
        backTexture = tex
    else
        -- Texture failed to load; the splash screen will show text only
    end

    local playTex, err = Texture.load(VulkanContext, "assets/images/play_button.png")
    if playTex then
        playButtonTexture = playTex
    else
        -- Texture failed to load; the button will show text only
    end

    local playHoverTex, err = Texture.load(VulkanContext, "assets/images/play_button_hover.png")
    if playHoverTex then
        playButtonHoverTexture = playHoverTex
    else
        -- Texture failed to load; the button will show text only
    end

    local optionsTex, err = Texture.load(VulkanContext, "assets/images/options_button.png")
    if optionsTex then
        optionsButtonTexture = optionsTex
    else
        -- Texture failed to load; the button will show text only
    end
    local optionsHoverTex, err = Texture.load(VulkanContext, "assets/images/options_button_hover.png")
    if optionsHoverTex then
        optionsButtonHoverTexture = optionsHoverTex
    else
        -- Texture failed to load; the button will show text only
    end
    local quitTex, err = Texture.load(VulkanContext, "assets/images/quit_button.png")
    if quitTex then
        quitButtonTexture = quitTex
    else
        -- Texture failed to load; the button will show text only
    end
    local quitHoverTex, err = Texture.load(VulkanContext, "assets/images/quit_button_hover.png")
    if quitHoverTex then
        quitButtonHoverTexture = quitHoverTex
    else
        -- Texture failed to load; the button will show text only
    end

    clickSound = Audio.loadSfx("assets/audio/click.ogg")
    closeSound = Audio.loadSfx("assets/audio/close.ogg")
    hoverSound = Audio.loadSfx("assets/audio/hover.ogg")
end

function M.onExit()
    backTexture = nil
    playButtonTexture = nil
    playButtonHoverTexture = nil
    optionsButtonTexture = nil
    optionsButtonHoverTexture = nil
    quitButtonTexture = nil
    quitButtonHoverTexture = nil
end

function M.render(state, dt, elapsedSeconds)
    ImGui.SetNextWindowFullscreen()
    local flags = ImGuiWindowFlags.NoDecoration  |
                  ImGuiWindowFlags.NoMove         |
                  ImGuiWindowFlags.NoSavedSettings |
                  ImGuiWindowFlags.NoBringToFrontOnFocus
    ImGui.Begin("MainMenuBackground", flags)

    if backTexture and backTexture:isValid() then
        local aw, ah   = ImGui.GetContentRegionAvail()
        local imgW, imgH = backTexture:width(), backTexture:height()
        local scale    = math.min(aw / imgW, ah / imgH)
        local drawW    = imgW * scale
        local drawH    = imgH * scale
        ImGui.SetCursorPos(
            math.max(0, (aw - drawW) * 0.5),
            math.max(0, (ah - drawH) * 0.45)
        )
        ImGui.Image(backTexture, drawW, drawH)
    end
    ImGui.End()

    local dw, dh = ImGui.GetDisplaySize()
    ImGui.SetNextWindowPos((dw - kMenuW) * 0.5, (dh - kMenuH) * 0.5, ImGuiCond.Always)
    ImGui.SetNextWindowSize(kMenuW, kMenuH, ImGuiCond.Always)

    local flags = ImGuiWindowFlags.NoResize  |
                  ImGuiWindowFlags.NoMove    |
                  ImGuiWindowFlags.NoCollapse |
                  ImGuiWindowFlags.NoTitleBar |
                  ImGuiWindowFlags.NoSavedSettings |
                  ImGuiWindowFlags.NoBackground
    ImGui.Begin("MainMenu", flags)

    ImGui.PushStyleColor(ImGuiCol.Button, 0.0, 0.0, 0.0, 0.0)
    ImGui.PushStyleColor(ImGuiCol.ButtonHovered, 0.0, 0.0, 0.0, 0.0)
    ImGui.PushStyleColor(ImGuiCol.ButtonActive, 0.0, 0.0, 0.0, 0.0)

    -- Render all buttons first so ImGui processes them all this frame
    local playButtonDisplayTexture = menuState.playHovered and playButtonHoverTexture or playButtonTexture
    local playClicked    = ImGui.ImageButton("Play", playButtonDisplayTexture, dw * 0.12, dh * 0.12)
    if ImGui.IsItemHovered() then
        if menuState.playHovered == false and hoverSound then
            Audio.playSfx(hoverSound, false, 0.5)
        end
        menuState.playHovered = true
    else
        menuState.playHovered = false
    end
    local optionsButtonDisplayTexture = menuState.optionsHovered and optionsButtonHoverTexture or optionsButtonTexture
    local optionsClicked = ImGui.ImageButton("Options", optionsButtonDisplayTexture, dw * 0.12, dh * 0.12)
    if ImGui.IsItemHovered() then
        if menuState.optionsHovered == false and hoverSound then
            Audio.playSfx(hoverSound, false, 0.5)
        end
        menuState.optionsHovered = true
    else
        menuState.optionsHovered = false
    end
    local quitButtonDisplayTexture = menuState.quitHovered and quitButtonHoverTexture or quitButtonTexture
    local quitClicked = ImGui.ImageButton("Quit", quitButtonDisplayTexture, dw * 0.12, dh * 0.12)
    if ImGui.IsItemHovered() then
        if menuState.quitHovered == false and hoverSound then
            Audio.playSfx(hoverSound, false, 0.5)
        end        
        menuState.quitHovered = true
    else
        menuState.quitHovered = false
    end

    ImGui.PopStyleColor()
    ImGui.PopStyleColor()
    ImGui.PopStyleColor()
    ImGui.End()

    if playClicked then
        if clickSound then
            Audio.playSfx(clickSound, false, 1.0)
        end
        Gameplay.requestScene(Gameplay.Scene.Lobby, "Loading level selection...")
    elseif optionsClicked then
        if clickSound then
            Audio.playSfx(clickSound, false, 1.0)
        end
        Gameplay.requestScene(Gameplay.Scene.Options, "Loading options...")
    elseif quitClicked then
        if closeSound then
            Audio.playSfxAsync(closeSound, false, 0.75, function()
                Gameplay.requestQuit()
            end)
        else
            Gameplay.requestQuit()
        end
    end
end

return M
