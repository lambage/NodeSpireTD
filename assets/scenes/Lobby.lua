local M = {}

local kWindowW = 820
local kWindowH = 420
local kPartyWindowW = 280
local kPartyWindowH = 400
local kPartyWindowGap = 20
local kSubWindowW = 320
local kSubWindowH = 160
-- Taller than kSubWindowH: this window has an extra address-input row above the port field.
local kFindWindowH = 200
local kChatWindowH = 300

local backTexture = nil

local loadLevelButton = nil
local backButton = nil
local hostPartyButton = nil
local findPartyButton = nil
local leavePartyButton = nil
local startHostingButton = nil
local cancelHostButton = nil
local connectPartyButton = nil
local cancelFindButton = nil
local sendChatButton = nil

local joinAddressText = "127.0.0.1"
local portValue = 47321
local localReady = false

local showHostWindow = false
local showFindWindow = false
local chatInputText = ""
local chatMessages = {}
-- True once the user has scrolled the chat log away from the bottom while unread messages
-- arrive; shown as a "jump to latest" indicator and cleared once they scroll/jump back down.
local chatHasUnseenMessages = false
local chatScrollToBottomRequested = false
-- Set for one frame when "t" or "/" is pressed outside of any text field; consumed right
-- before the chat InputText widget via ImGui.SetKeyboardFocusHere().
local chatFocusRequested = false
-- Editable copy of the local profile's display name (the profile's UUID is never exposed to
-- Lua). Auto-saved on every keystroke via Gameplay.setLocalProfileName().
local profileNameText = "Player"

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
    hostPartyButton = GameButton.new("hostParty", "Host", 120.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    findPartyButton = GameButton.new("findParty", "Find Party", 120.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    leavePartyButton = GameButton.new("leaveParty", "Leave Party", 130.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/close.ogg")
    startHostingButton = GameButton.new("startHosting", "Start Hosting", 150.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    cancelHostButton = GameButton.new("cancelHost", "Cancel", 100.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/close.ogg")
    connectPartyButton = GameButton.new("connectParty", "Connect", 150.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")
    cancelFindButton = GameButton.new("cancelFind", "Cancel", 100.0, 36.0,
        "assets/audio/hover.ogg", "assets/audio/close.ogg")
    sendChatButton = GameButton.new("sendChat", "Send", 80.0, 32.0,
        "assets/audio/hover.ogg", "assets/audio/click.ogg")

    local mpState = Gameplay.getMultiplayerState and Gameplay.getMultiplayerState() or nil
    if mpState then
        portValue = mpState.port or portValue
        if mpState.joinAddress and mpState.joinAddress ~= "" then
            joinAddressText = mpState.joinAddress
        end
    end

    local profile = Gameplay.getLocalProfile and Gameplay.getLocalProfile() or nil
    profileNameText = (profile and profile.name) or "Player"

    showHostWindow = false
    showFindWindow = false
    chatInputText = ""
    chatMessages = {}
    chatHasUnseenMessages = false
    chatScrollToBottomRequested = false
end

function M.onExit()
	backTexture = nil
	loadLevelButton = nil
	backButton = nil
	hostPartyButton = nil
	findPartyButton = nil
	leavePartyButton = nil
	startHostingButton = nil
	cancelHostButton = nil
	connectPartyButton = nil
	cancelFindButton = nil
	sendChatButton = nil
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

	local mpState = Gameplay.getMultiplayerState and Gameplay.getMultiplayerState() or nil
	local hosting = mpState and mpState.hosting or false
	local joining = mpState and mpState.joining or false
	local isInParty = hosting or joining
	-- Solo play has no host to defer to, so the local player is always the party leader.
	local isPartyHost = (not isInParty) or hosting

	-- Global chat hotkeys: "t" opens chat for typing, "/" opens chat pre-filled for a slash
	-- command. Guarded by WantTextInput so this never hijacks focus while a field is being edited.
	if isInParty and not ImGui.WantTextInput() then
		if ImGui.IsKeyPressed(ImGuiKey.T) then
			chatFocusRequested = true
		elseif ImGui.IsKeyPressed(ImGuiKey.Slash) then
			chatFocusRequested = true
			chatInputText = "/"
		end
	end

	-- Drained every frame (not just while the chat window is visible) so nothing is lost while
	-- the party UI is briefly hidden, e.g. the instant a disconnect drops back to solo play.
	local chatMessageCountBefore = #chatMessages
	for _, message in ipairs(Gameplay.consumePartyChatMessages()) do
		if message.name == "System" then
			-- Host/party lifecycle notices (host started, join/leave/kick, disconnected) are
			-- timestamped so players can tell when things happened in a busy chat log.
			table.insert(chatMessages, {author = "*", text = string.format("[%s] %s", os.date("%H:%M:%S"), tostring(message.text))})
		elseif message.isEmote then
			table.insert(chatMessages, {author = "*", text = tostring(message.text)})
		else
			table.insert(chatMessages, {author = tostring(message.name), text = tostring(message.text)})
		end
	end
	for _, errorText in ipairs(Gameplay.consumePartyChatErrors()) do
		table.insert(chatMessages, {author = "[System]", text = tostring(errorText)})
	end
	local receivedNewMessages = #chatMessages > chatMessageCountBefore

	local roster = Gameplay.getPartyRoster and Gameplay.getPartyRoster() or nil
	local members = roster and roster.members or {}
	local capacity = roster and roster.capacity or 0

	local localMember = nil
	for i = 1, #members do
		if members[i].isLocal then
			localMember = members[i]
			break
		end
	end
	-- Plain "and/or" here would break when ready is false (Lua treats false as falsy), so use
	-- an explicit if to allow the local player to un-ready.
	if localMember ~= nil then
		localReady = localMember.ready
	end

	local allMembersReady = true
	for i = 1, #members do
		if not members[i].ready then
			allMembersReady = false
			break
		end
	end
	-- Host may only start once every party member has readied up; a lone host can always start.
	local canStartMatch = (not isInParty) or (not isPartyHost) or (#members <= 1) or allMembersReady

	-- A client auto-follows the host into PlayLevel the moment a match start is announced.
	if isInParty and not isPartyHost then
		if Gameplay.checkPartyMatchStart and Gameplay.checkPartyMatchStart() then
			Gameplay.requestScene(Gameplay.Scene.PlayLevel, "Joining co-op match...")
		end
	end

	local displayW, displayH = ImGui.GetDisplaySize()
	ImGui.SetNextWindowPos((displayW - kWindowW) * 0.5, (displayH - kWindowH) * 0.5, ImGuiCond.Always)
	ImGui.SetNextWindowSize(kWindowW, kWindowH, ImGuiCond.Always)

	local flags = ImGuiWindowFlags.NoResize +
				  ImGuiWindowFlags.NoMove +
				  ImGuiWindowFlags.NoCollapse +
				  ImGuiWindowFlags.NoTitleBar
	ImGui.Begin("LevelSelection", flags)

	ImGui.Text("Select a level")
	ImGui.Separator()

	local missionListWidth = 260
	ImGui.BeginChild("MissionList", missionListWidth, -60.0, ImGuiWindowFlags.NoScrollbar)
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
	ImGui.BeginChild("MissionDetails", 0.0, -60.0, ImGuiWindowFlags.NoScrollbar)
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

	if isInParty then
		loadLevelButton:setLabel(isPartyHost and "Start Match" or "Join Match")
	else
		loadLevelButton:setLabel("Load Level")
	end

	if not hasLevels or not canStartMatch then
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

		local message
		if not isInParty then
			-- Not in a party: Load Level always (re)starts a fresh solo run.
			Gameplay.setMultiplayerMode(false, portValue, "")
			message = string.format("Loading level: %s...", selectedName)
		elseif isPartyHost then
			Gameplay.announceMatchStart()
			message = "Hosting co-op match..."
		else
			message = "Joining co-op match..."
		end
		Gameplay.requestScene(Gameplay.Scene.PlayLevel, message)
	end
	if not hasLevels or not canStartMatch then
		ImGui.EndDisabled()
	end

	ImGui.SameLine()
	if backButton:render() then
		Gameplay.requestScene(Gameplay.Scene.MainMenu, "Returning to main menu...")
	end

	ImGui.End()

	local partyWindowX = (displayW - kWindowW) * 0.5 + kWindowW + kPartyWindowGap
	local partyWindowY = (displayH - kWindowH) * 0.5
	ImGui.SetNextWindowPos(partyWindowX, partyWindowY, ImGuiCond.Always)
	ImGui.SetNextWindowSize(kPartyWindowW, kPartyWindowH, ImGuiCond.Always)

	local partyFlags = ImGuiWindowFlags.NoResize +
						ImGuiWindowFlags.NoMove +
						ImGuiWindowFlags.NoCollapse +
						ImGuiWindowFlags.NoTitleBar
	ImGui.Begin("PartyWindow", partyFlags)

	if HeadingFont then
		ImGui.PushFont(HeadingFont)
	end
	ImGui.Text("Party")
	if HeadingFont then
		ImGui.PopFont()
	end
	ImGui.Separator()

	-- No explicit Save button: every keystroke is persisted immediately via setLocalProfileName.
	ImGui.SetNextItemWidth(-1.0)
	local nameChanged, newProfileName = ImGui.InputText("##profileName", profileNameText)
	if nameChanged then
		profileNameText = newProfileName
		if profileNameText ~= "" then
			Gameplay.setLocalProfileName(profileNameText)
		end
	end
	ImGui.Spacing()

	ImGui.Text(string.format("Members: %d / %d", #members, capacity))
	ImGui.Spacing()

	local localIsHost = localMember ~= nil and localMember.isHost

	ImGui.BeginChild("PartyRoster", 0.0, -90.0, ImGuiWindowFlags.NoScrollbar)
	for i = 1, #members do
		local member = members[i]
		local label = tostring(member.name or "Player")
		if member.isHost then
			label = label .. " (Host)"
		end
		if member.isLocal then
			label = label .. " (You)"
		end

		if member.ready then
			ImGui.TextColored(0.4, 0.9, 0.4, 1.0, label .. " - Ready")
		else
			ImGui.TextDisabled(label .. " - Not ready")
		end

		if localIsHost and not member.isLocal then
			ImGui.SameLine()
			if ImGui.SmallButton("Kick##party" .. tostring(member.id)) then
				Gameplay.kickPartyMember(member.id)
			end
		end
	end
	ImGui.EndChild()

	if isInParty then
		local readyChanged, readyValue = ImGui.Checkbox("Ready", localReady)
		if readyChanged then
			localReady = readyValue
			if Gameplay.setPartyReady then
				Gameplay.setPartyReady(readyValue)
			end
		end
	end

	if isInParty then
		if leavePartyButton:render() then
			Gameplay.setMultiplayerMode(false, portValue, "")
			chatMessages = {}
			chatHasUnseenMessages = false
			chatScrollToBottomRequested = false
		end
	else
		if hostPartyButton:render() then
			showHostWindow = true
			showFindWindow = false
		end
		ImGui.SameLine()
		if findPartyButton:render() then
			showFindWindow = true
			showHostWindow = false
		end
	end

	ImGui.End()

	if showHostWindow then
		ImGui.SetNextWindowPos((displayW - kSubWindowW) * 0.5, (displayH - kSubWindowH) * 0.5, ImGuiCond.Always)
		ImGui.SetNextWindowSize(kSubWindowW, kSubWindowH, ImGuiCond.Always)
		local subFlags = ImGuiWindowFlags.NoResize +
						  ImGuiWindowFlags.NoMove +
						  ImGuiWindowFlags.NoCollapse +
						  ImGuiWindowFlags.NoTitleBar
		ImGui.Begin("HostPartyWindow", subFlags)
		ImGui.Text("Host Party")
		ImGui.Separator()

		local portChanged, newPort = ImGui.InputInt("Port", portValue)
		if portChanged then
			portValue = newPort
		end
		ImGui.Spacing()

		if startHostingButton:render() then
			Gameplay.setMultiplayerMode(true, portValue, "")
			showHostWindow = false
		end
		ImGui.SameLine()
		if cancelHostButton:render() then
			showHostWindow = false
		end
		ImGui.End()
	end

	if showFindWindow then
		ImGui.SetNextWindowPos((displayW - kSubWindowW) * 0.5, (displayH - kFindWindowH) * 0.5, ImGuiCond.Always)
		ImGui.SetNextWindowSize(kSubWindowW, kFindWindowH, ImGuiCond.Always)
		local subFlags = ImGuiWindowFlags.NoResize +
						  ImGuiWindowFlags.NoMove +
						  ImGuiWindowFlags.NoCollapse +
						  ImGuiWindowFlags.NoTitleBar
		ImGui.Begin("FindPartyWindow", subFlags)
		ImGui.Text("Find Party")
		ImGui.Separator()

		local addressChanged, newAddress = ImGui.InputText("Host", joinAddressText)
		if addressChanged then
			joinAddressText = newAddress
		end
		local portChanged, newPort = ImGui.InputInt("Port", portValue)
		if portChanged then
			portValue = newPort
		end
		ImGui.Spacing()

		if connectPartyButton:render() then
			Gameplay.setMultiplayerMode(false, portValue, joinAddressText)
			showFindWindow = false
		end
		ImGui.SameLine()
		if cancelFindButton:render() then
			showFindWindow = false
		end
		ImGui.End()
	end

	if isInParty then
		local chatWindowW = kWindowW + kPartyWindowGap + kPartyWindowW
		local chatWindowX = (displayW - kWindowW) * 0.5
		-- Start below the taller of the two windows above (LevelSelection, not PartyWindow) so
		-- the chat window never overlaps the level-selection window.
		local chatWindowY = partyWindowY + kWindowH + kPartyWindowGap
		ImGui.SetNextWindowPos(chatWindowX, chatWindowY, ImGuiCond.Always)
		ImGui.SetNextWindowSize(chatWindowW, kChatWindowH, ImGuiCond.Always)

		local chatFlags = ImGuiWindowFlags.NoResize +
						   ImGuiWindowFlags.NoMove +
						   ImGuiWindowFlags.NoCollapse +
						   ImGuiWindowFlags.NoTitleBar +
						   ImGuiWindowFlags.NoScrollbar
		ImGui.Begin("PartyChatWindow", chatFlags)
		ImGui.Text("Party Chat")
		ImGui.Separator()

		ImGui.BeginChild("PartyChatLog", 0.0, -40.0, ImGuiWindowFlags.NoScrollbar)
		-- Read scroll position before this frame's content is appended below: if the log was
		-- already scrolled to the (previous frame's) bottom, keep following new messages.
		local wasAtBottom = ImGui.GetScrollY() >= ImGui.GetScrollMaxY() - 1.0
		for i = 1, #chatMessages do
			local entry = chatMessages[i]
			if entry.author == "*" then
				ImGui.TextDisabled(tostring(entry.text))
			else
				ImGui.TextWrapped(string.format("%s: %s", tostring(entry.author), tostring(entry.text)))
			end
		end
		if chatScrollToBottomRequested or (receivedNewMessages and wasAtBottom) then
			-- SetScrollHereY follows the cursor left by the loop above (this frame's true bottom);
			-- GetScrollMaxY() here would still be last frame's (stale) value and undershoot.
			ImGui.SetScrollHereY(1.0)
			chatHasUnseenMessages = false
		elseif receivedNewMessages then
			chatHasUnseenMessages = true
		end
		chatScrollToBottomRequested = false
		ImGui.EndChild()

		if chatHasUnseenMessages then
			if ImGui.SmallButton("New messages available - click to jump to latest") then
				chatScrollToBottomRequested = true
			end
		end

		ImGui.SetNextItemWidth(-90.0)
		local focusingThisFrame = chatFocusRequested
		if chatFocusRequested then
			ImGui.SetKeyboardFocusHere()
			chatFocusRequested = false
		end
		local chatChanged, newChatText = ImGui.InputText("##chatInput", chatInputText, ImGuiInputTextFlags.EnterReturnsTrue, focusingThisFrame)
		-- InputText also returns changed=true on Enter without the text differing from before;
		-- that's how a submit is distinguished here from an ordinary keystroke edit.
		local enterPressed = chatChanged and newChatText == chatInputText
		chatInputText = newChatText
		ImGui.SameLine()
		if (sendChatButton:render() or enterPressed) and chatInputText ~= "" then
			Gameplay.sendPartyChat(chatInputText)
			chatInputText = ""
		end

		ImGui.End()
	end
end

return M
