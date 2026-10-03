-- Splash scene UI, run fresh each time this scene is entered (including on
-- the app's /reload dev hotkey). UI.Root is this scene's cleared widget
-- root; see LuaUiScene::onEnter (src/lambui/LuaUiScene.cpp).

local root = UI.CreateFrame("Frame", "SplashRoot")
root:SetAllPoints(UI.Root)

local image = root:CreateImage("SplashImage")
image:SetAllPoints(root)
image:SetFit("CONTAIN")
if not image:SetSource("assets/images/splash_screen.png") then
	local title = root:CreateFontString("Title")
	title:SetText("NodeSpire TD")
	title:SetPoint("CENTER", root, "CENTER", 0, -40)
end

local status = root:CreateFontString("Status")
status:SetText("Loading...")
status:SetPoint("BOTTOM", root, "BOTTOM", 0, -24)

local entered = false

function OnEnter()
	entered = true
	Audio.Play("assets/music/Heroic_Demise.mp3", "Music", true, 0.5)
end

function OnExit()
	entered = false
end

function OnShortcut(scanCode)
	return false
end
