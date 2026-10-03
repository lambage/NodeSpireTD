function OnEnter(width, height, initialState)
    ui.reset()
    ui.createButton("Play", "Lobby", -128, "play_button")
    ui.createButton("Options", "Options", -6, "options_button")
    ui.createButton("Exit", "Quit", 116, "quit_button")
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
