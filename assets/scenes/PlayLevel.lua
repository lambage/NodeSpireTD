local M = {}

local lastResult = ""

function M.onEnter()
    lastResult = "PlayLevel script loaded"
end

function M.render(state, dt, elapsed)
    ImGui.SetNextWindowPos(20, 20, ImGuiCond.Always)
    ImGui.SetNextWindowSize(460, 600, ImGuiCond.Always)
    ImGui.Begin("GameplayScriptHarness", ImGuiWindowFlags.NoResize + ImGuiWindowFlags.NoCollapse)

    ImGui.Text("Lua Gameplay Harness")
    ImGui.Separator()

    local gs = Gameplay.getState()
    ImGui.Text(string.format("HP: %d", gs.baseHealth))
    ImGui.Text(string.format("Money: %d", gs.playerMoney))
    ImGui.Text(string.format("Wave: %d", gs.currentWave))
    ImGui.Text(string.format("Wave Active: %s", tostring(gs.waveInProgress)))
    ImGui.Text(string.format("Status: %s", gs.matchStatus))

    if ImGui.Button("Spend 25", -1, 0) then
        local r = Gameplay.requestSpendMoney(25)
        lastResult = string.format("Spend 25 -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    if ImGui.Button("Damage Base 10", -1, 0) then
        local r = Gameplay.requestDamageBase(10)
        lastResult = string.format("Damage 10 -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    if ImGui.Button("Start Wave", -1, 0) then
        local r = Gameplay.requestStartWave()
        lastResult = string.format("Start wave -> ok=%s reason=%s", tostring(r.ok), r.reason)
    end

    ImGui.Separator()
    ImGui.Text(lastResult)
    ImGui.End()

    return {}
end

function M.onExit()
end

return M
