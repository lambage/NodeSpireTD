local M = {}

local kWindowW = 820
local kWindowH = 560

local backTexture = nil

local loadLevelButton = nil
local backButton = nil
local hostButton = nil
local joinButton = nil

local joinAddressText = "127.0.0.1"
local portValue = 47321

function M.onEnter()
	local tex, err = Texture.load(VulkanContext, "assets/images/splash_screen.png")
    if tex then
        backTexture = tex
    else
        -- Texture failed to load; the splash screen will show text only
    end

    loadLevelButton = GameButton.new("loadLevel", "Load Level", 170.0, 40.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    backButton = GameButton.new("back", "Back", 140.0, 40.0,
        "assets/audio/hover.ogg", "assets/audio/close.ogg")
    hostButton = GameButton.new("hostMatch", "Host Co-op", 170.0, 40.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    joinButton = GameButton.new("joinMatch", "Join Co-op", 170.0, 40.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")

    local mpState = Gameplay.getMultiplayerState and Gameplay.getMultiplayerState() or nil
    if mpState then
        portValue = mpState.port or portValue
        if mpState.joinAddress and mpState.joinAddress ~= "" then
            joinAddressText = mpState.joinAddress
        end
    end
end

function M.onExit()
	backTexture = nil
	loadLevelButton = nil
	backButton = nil
	hostButton = nil
	joinButton = nil
end

function M.render(state, dt, elapsedSeconds)
	
	ImGui.PushStyleVar(ImGuiStyleVar.WindowPadding, 0.0, 0.0)
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
	ImGui.PopStyleVar()

	local levels = Gameplay.getLobbyLevels and Gameplay.getLobbyLevels() or {}
	local hasLevels = levels ~= nil and #levels > 0

	local displayW, displayH = ImGui.GetDisplaySize()
	ImGui.SetNextWindowPos((displayW - kWindowW) * 0.5, (displayH - kWindowH) * 0.5, ImGuiCond.Always)
	ImGui.SetNextWindowSize(kWindowW, kWindowH, ImGuiCond.Always)

	local flags = ImGuiWindowFlags.NoResize +
				  ImGuiWindowFlags.NoMove +
				  ImGuiWindowFlags.NoCollapse +
				  ImGuiWindowFlags.NoTitleBar
	ImGui.Begin("LevelSelection", flags)

	if HeadingFont then
		ImGui.PushFont(HeadingFont)
	end
	ImGui.Text("Mission Control")
	if HeadingFont then
		ImGui.PopFont()
	end

	ImGui.Text("Select a mission profile")
	ImGui.Separator()

	local missionListWidth = 260
	ImGui.BeginChild("MissionList", missionListWidth, -140.0, ImGuiWindowFlags.NoScrollbar)
	for i = 1, #levels do
		local level = levels[i]
		local selected = level and level.selected or false
		if ImGui.Selectable(tostring(level.name or ("Mission " .. i)), selected) then
			if Gameplay.selectLobbyLevel then
				Gameplay.selectLobbyLevel(i)
			end
		end
	end
	ImGui.EndChild()

	ImGui.SameLine()
	ImGui.BeginChild("MissionDetails", 0.0, -140.0, ImGuiWindowFlags.NoScrollbar)
	if hasLevels then
		local selectedLevel = nil
		for i = 1, #levels do
			if levels[i].selected then
				selectedLevel = levels[i]
				break
			end
		end
		if not selectedLevel then
			selectedLevel = levels[1]
		end

		ImGui.Text(string.format("Selected mission: %s", tostring(selectedLevel.name or "Unknown")))
		ImGui.Spacing()
		ImGui.TextWrapped("Scan complete. Terrain analytics, choke points, and enemy wave patterns are ready for deployment simulation.")
		ImGui.Spacing()
		ImGui.TextWrapped(string.format("Asset source: %s", tostring(selectedLevel.assetPath or "")))
	else
		ImGui.Text("No valid levels found.")
		ImGui.Spacing()
		ImGui.TextWrapped("Each folder in assets/levels must provide a valid level.lua with mapAssetPath.")
	end
	ImGui.EndChild()

	ImGui.Separator()
	ImGui.Text("Co-op (LAN)")
	local portChanged, newPort = ImGui.InputInt("Port", portValue)
	if portChanged then
		portValue = newPort
	end
	ImGui.SameLine()
	if hostButton:render() then
		Gameplay.setMultiplayerMode(true, portValue, "")
		Gameplay.requestScene(Gameplay.Scene.PlayLevel, "Hosting co-op match...")
	end

	local addressChanged, newAddress = ImGui.InputText("Host address", joinAddressText)
	if addressChanged then
		joinAddressText = newAddress
	end
	ImGui.SameLine()
	if joinButton:render() then
		Gameplay.setMultiplayerMode(false, portValue, joinAddressText)
		Gameplay.requestScene(Gameplay.Scene.PlayLevel, "Joining co-op match...")
	end

	if not hasLevels then
		ImGui.BeginDisabled()
	end
	if loadLevelButton:render() then
		local selectedName = "level"
		for i = 1, #levels do
			if levels[i].selected then
				selectedName = tostring(levels[i].name or selectedName)
				break
			end
		end
		Gameplay.setMultiplayerMode(false, portValue, "")
		Gameplay.requestScene(Gameplay.Scene.PlayLevel, string.format("Loading level: %s...", selectedName))
	end

	if not hasLevels then
		ImGui.EndDisabled()
	end

	ImGui.SameLine()
	if backButton:render() then
		Gameplay.requestScene(Gameplay.Scene.MainMenu, "Returning to main menu...")
	end

	ImGui.End()
end

return M
