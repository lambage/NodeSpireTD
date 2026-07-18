return {
    -- true keeps the currently selected mission map from Level Selection.
    -- Set to false and provide mapAssetPath to force a specific map for this scene.
    inheritActiveSelection = true,

    -- Optional override when inheritActiveSelection = false.
    -- mapAssetPath = "assets/levels/grassy/grassy_map.glb",

    -- Can point to a shared wave script or a map-specific override.
    wavesScriptPath = "assets/scenes/PlayLevelWaves.lua"
}
