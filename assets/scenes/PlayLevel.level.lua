return {
    -- true keeps the currently selected mission map from Level Selection.
    -- Set to false and provide mapAssetPath to force a specific map for this scene.
    inheritActiveSelection = true,

    -- Optional override when inheritActiveSelection = false.
    -- mapAssetPath = "assets/levels/grassy/grassy_map.glb",

    -- Can point to a shared wave script or a map-specific override.
    wavesScriptPath = "assets/scenes/PlayLevelWaves.lua",

    -- World assets are now fully data-driven from Lua.
    worldAssets = {
        -- Required by loader: map must include Start, End, and at least one Waypoint_N marker.
        startModelPath = "assets/models/base/portal.glb",
        endModelPath = "assets/models/base/base.glb",

        -- Optional extra world props.
        -- anchor: "Start", "End", or omitted.
        -- positionOffset/eulerDegrees/scale are applied in world space.
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

        -- Optional UI textures. Available in Lua global LevelUiTextures.
        -- Example:
        -- for _, tex in ipairs(LevelUiTextures) do
        --   local image = Texture.load(VulkanContext, tex.path)
        -- end
        uiTextures = {
            { id = "goldIcon", path = "assets/images/gold.png" },
        },
    },
}
