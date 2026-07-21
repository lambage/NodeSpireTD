local M = {}

local kWindowW = 820
local kWindowH = 480

function M.onEnter()
end

function M.onExit()
end

function M.render(state, dt, elapsedSeconds)
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
	ImGui.BeginChild("MissionList", missionListWidth, -56.0, ImGuiWindowFlags.NoScrollbar)
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
	ImGui.BeginChild("MissionDetails", 0.0, -56.0, ImGuiWindowFlags.NoScrollbar)
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

	if not hasLevels then
		ImGui.BeginDisabled()
	end
	if ImGui.Button("Load Level", 170.0, 40.0) then
		local selectedName = "level"
		for i = 1, #levels do
			if levels[i].selected then
				selectedName = tostring(levels[i].name or selectedName)
				break
			end
		end
		Gameplay.requestScene(Gameplay.Scene.PlayLevel, string.format("Loading level: %s...", selectedName))
	end
	if not hasLevels then
		ImGui.EndDisabled()
	end

	ImGui.SameLine()
	if ImGui.Button("Back", 140.0, 40.0) then
		Gameplay.requestScene(Gameplay.Scene.MainMenu, "Returning to main menu...")
	end

	ImGui.End()
end

return M
