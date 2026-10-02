-- Splash scene UI, run fresh each time this scene is entered (including on
-- the app's /reload dev hotkey). UI.Root is this scene's cleared widget
-- root; see LuaUiScene::onEnter (src/lambui/LuaUiScene.cpp).
--
-- NOTE: logo texture + custom font loading aren't wired up yet (need a
-- native Images.Load/Fonts.Get binding backed by the still-to-be-written
-- Vulkan renderer's texture/font-atlas upload path) -- this is text-only
-- for now.

local root = UI.CreateFrame("Frame", "SplashRoot")
root:SetAllPoints(UI.Root)

local title = root:CreateFontString("Title")
title:SetText("NodeSpire TD")
title:SetPoint("CENTER", root, "CENTER", 0, -40)

local status = root:CreateFontString("Status")
status:SetText("Loading...")
status:SetPoint("CENTER", root, "CENTER", 0, 40)

Audio.Play("assets/music/Heroic_Demise.mp3", "Music", true, 0.5)
