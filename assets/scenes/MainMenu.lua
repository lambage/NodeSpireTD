local M = {}

local kMenuW = 420
local kMenuH = 320

--local backTexture = nil

function M.onEnter()
    local tex, err = Texture.load(VulkanContext, "assets/images/splash_screen.png")
    if tex then
        backTexture = tex
    else
        -- Texture failed to load; the splash screen will show text only
    end
end

function M.onExit()
    backTexture = nil
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
                  ImGuiWindowFlags.NoTitleBar
    ImGui.Begin("MainMenu", flags)

    if HeadingFont then ImGui.PushFont(HeadingFont) end
    ImGui.Text("NodeSpireTD")
    if HeadingFont then ImGui.PopFont() end

    ImGui.Text("Deploy. Defend. Adapt.")
    ImGui.Separator()

    -- Render all buttons first so ImGui processes them all this frame
    local playClicked    = ImGui.Button("Play",    -1, 42)
    local optionsClicked = ImGui.Button("Options", -1, 42)
    local quitClicked    = ImGui.Button("Quit",    -1, 42)

    ImGui.End()

    if playClicked then
        return {
            requestTransition = true,
            target            = "LevelSelection",
            message           = "Loading level selection...",
        }
    elseif optionsClicked then
        return {
            requestTransition = true,
            target            = "Options",
            message           = "Loading options...",
        }
    elseif quitClicked then
        return { requestQuit = true }
    end
end

return M
