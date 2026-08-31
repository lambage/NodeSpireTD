import bpy
import math

# ensure a strong sun for clear directional shading across the terrain relief
sun = bpy.data.objects.get("MesaSun")
if not sun:
    sun_data = bpy.data.lights.new("MesaSunData", type='SUN')
    sun = bpy.data.objects.new("MesaSun", sun_data)
    bpy.context.collection.objects.link(sun)
sun.data.energy = 3.0
sun.data.angle = math.radians(2.0)
sun.rotation_euler = (math.radians(55), 0, math.radians(35))

world = bpy.data.worlds.get("World") or bpy.data.worlds.new("World")
bpy.context.scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes.get("Background")
if bg:
    bg.inputs[0].default_value = (0.55, 0.68, 0.85, 1.0)
    bg.inputs[1].default_value = 0.6

for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        for space in area.spaces:
            if space.type == 'VIEW_3D':
                space.shading.type = 'RENDERED'
print("rendered_setup_done")
