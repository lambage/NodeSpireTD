import bpy

world = bpy.context.scene.world
bg = world.node_tree.nodes.get("Background")
bg.inputs[0].default_value = (0.72, 0.78, 0.85, 1.0)
bg.inputs[1].default_value = 0.55

scene = bpy.context.scene
scene.eevee.use_gtao = True
scene.eevee.gtao_distance = 1.5
scene.eevee.gtao_factor = 1.0
print("lighting_tuned")
