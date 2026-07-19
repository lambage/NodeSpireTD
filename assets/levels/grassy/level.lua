local M = {
    displayName = "Grassy",
    mapAssetPath = "assets/levels/grassy/grassy_map.glb",
    wavesScriptPath = "assets/scenes/PlayLevelWaves.lua",
    thumbnailImagePath = "assets/levels/grassy/grassy.png",
    inheritActiveSelection = false,
    worldAssets = {
        startModelPath = "assets/models/base/portal.glb",
        endModelPath = "assets/models/base/base.glb",
        extraWorldModels = {},
        uiTextures = {},
    },
}

function M.onLoad(level)
    local wavesPath = level.wavesScriptPath or M.wavesScriptPath
    if not wavesPath or wavesPath == "" then
        return
    end

    local chunk, err = loadfile(wavesPath)
    if not chunk then
        error("failed to load waves script '" .. wavesPath .. "': " .. tostring(err))
    end

    local waveModule = chunk()
    if type(waveModule) == "table" and type(waveModule.onLoad) == "function" then
        waveModule.onLoad()
    end
end

return M
