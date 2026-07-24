local M = {}

local kMenuW = 420
local kMenuH = 480

local backTexture = nil

local playButton = nil
local optionsButton = nil
local quitButton = nil

local closeSound = nil

function M.onEnter()
    local tex, err = Texture.load(VulkanContext, "assets/images/splash_screen.png")
    if tex then
        backTexture = tex
    else
        -- Texture failed to load; the splash screen will show text only
    end

    local dw, dh = ImGui.GetDisplaySize()
    local buttonW, buttonH = dw * 0.12, dh * 0.12

    playButton, err = GameImageButton.new(VulkanContext, "Play", "assets/images/play_button.png",
        "assets/images/play_button_hover.png", buttonW, buttonH,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    if not playButton then
        -- Button textures failed to load; Play will be unavailable this session
    end

    optionsButton, err = GameImageButton.new(VulkanContext, "Options", "assets/images/options_button.png",
        "assets/images/options_button_hover.png", buttonW, buttonH,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    if not optionsButton then
        -- Button textures failed to load; Options will be unavailable this session
    end

    -- Quit plays a distinct "close" sound and must wait for it before exiting,
    -- so its click sound is handled manually instead of via the button.
    quitButton, err = GameImageButton.new(VulkanContext, "Quit", "assets/images/quit_button.png",
        "assets/images/quit_button_hover.png", buttonW, buttonH, "assets/audio/hover.ogg")
    if not quitButton then
        -- Button textures failed to load; Quit will be unavailable this session
    end

    closeSound = Audio.loadSfx("assets/audio/close.ogg")
end

function M.onExit()
    backTexture = nil
    playButton = nil
    optionsButton = nil
    quitButton = nil
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

    local buttonW, buttonH = dw * 0.12, dh * 0.12

    -- Render all buttons first so ImGui processes them all this frame
    local playClicked = false
    if playButton then
        playButton:setSize(buttonW, buttonH)
        playClicked = playButton:render()
    end

    local optionsClicked = false
    if optionsButton then
        optionsButton:setSize(buttonW, buttonH)
        optionsClicked = optionsButton:render()
    end

    local quitClicked = false
    if quitButton then
        quitButton:setSize(buttonW, buttonH)
        quitClicked = quitButton:render()
    end

    ImGui.PopStyleColor()
    ImGui.PopStyleColor()
    ImGui.PopStyleColor()
    ImGui.End()

    if playClicked then
        Gameplay.requestScene(Gameplay.Scene.Lobby, "Loading level selection...")
    elseif optionsClicked then
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

