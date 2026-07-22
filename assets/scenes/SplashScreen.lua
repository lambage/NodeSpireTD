local M = {}

local splashTexture = nil
local mainMusic = nil
local kMinimumSplashSeconds = 3.0

function M.onEnter()
    local tex, err = Texture.load(VulkanContext, "assets/images/splash_screen.png")
    if tex then
        splashTexture = tex
    else
        -- Texture failed to load; the splash screen will show text only
    end
    mainMusic = Audio.loadMusic("assets/music/Heroic_Demise.mp3")
    Audio.playMusic(mainMusic, true, 0.5) -- Play the main music in a loop at half volume
end

function M.onExit()
    splashTexture = nil
    -- __gc on the texture userdata will destroy the Vulkan resources
end

function M.render(state, dt, elapsedSeconds)
    -- Full-screen borderless window
    ImGui.SetNextWindowFullscreen()
    local flags = ImGuiWindowFlags.NoDecoration  |
                  ImGuiWindowFlags.NoMove         |
                  ImGuiWindowFlags.NoSavedSettings |
                  ImGuiWindowFlags.NoBringToFrontOnFocus
    ImGui.Begin("SplashScreen", flags)

    -- Centered splash image
    if splashTexture and splashTexture:isValid() then
        local aw, ah   = ImGui.GetContentRegionAvail()
        local imgW, imgH = splashTexture:width(), splashTexture:height()
        local scale    = math.min(aw / imgW, ah / imgH)
        local drawW    = imgW * scale
        local drawH    = imgH * scale
        ImGui.SetCursorPos(
            math.max(0, (aw - drawW) * 0.5),
            math.max(0, (ah - drawH) * 0.45)
        )
        ImGui.Image(splashTexture, drawW, drawH)
    end

    -- Status line anchored near the bottom
    local status  = "Initializing systems..."
    local statusW = ImGui.CalcTextSize(status)
    ImGui.SetCursorPos(
        math.max(0, (ImGui.GetWindowWidth()  - statusW) * 0.5),
        math.max(0,  ImGui.GetWindowHeight() - 46)
    )
    ImGui.Text(status)

    ImGui.End()

    -- Signal transition once loading is done and minimum time has elapsed
    if state.loadingComplete and elapsedSeconds >= kMinimumSplashSeconds then
        Gameplay.requestScene(Gameplay.Scene.MainMenu, "Loading main menu...", 0.5)
    end
end

return M
