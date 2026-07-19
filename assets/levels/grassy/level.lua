local M = {
    displayName = "Grassy",
    mapAssetPath = "grassy_map.glb",
    wavesScriptPath = "assets/scenes/PlayLevelWaves.lua",
    inheritActiveSelection = false,
    worldAssets = {
        startModelPath = "assets/models/base/portal.glb",
        endModelPath = "assets/models/base/base.glb",
        extraWorldModels = {
            {
                modelPath = "assets/models/base/base.glb",
                debugGroup = "prop",
                debugLabel = "home_base_preview",
                anchor = "End",
                facePath = true,
                positionOffset = { x = 0.0, y = 0.0, z = -6.0 },
                eulerDegrees = { x = 0.0, y = 0.0, z = 0.0 },
                scale = { x = 0.4, y = 0.4, z = 0.4 },
            },
        },
        uiTextures = {
            { id = "goldIcon", path = "assets/images/gold.png" },
        },
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
