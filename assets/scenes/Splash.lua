local root, status

function OnEnter(width, height, initialState)
    root = UI.CreateFrame("Frame", "SplashRoot")
    root:SetAllPoints(UI.Root)

    local image = root:CreateImage("SplashImage")
    image:SetAllPoints(root)
    image:SetFit("CONTAIN")
    if not image:SetSource("assets/images/splash_screen.png") then
        local title = root:CreateFontString("Title")
        title:SetText("NodeSpire TD")
        title:SetPoint("CENTER", root, "CENTER", 0, -40)
    end

    status = root:CreateFontString("Status")
    status:SetText("Loading...")
    status:SetPoint("BOTTOM", root, "BOTTOM", 0, -24)

    Audio.Play("assets/music/Heroic_Demise.mp3", "Music", true, 0.5)
end

function OnUpdate(state, dt)
end

function OnLayoutChanged(width, height)
end

function OnExit()
end

function OnShortcut(scanCode)
    return false
end
