import bpy
import math
from mathutils import Vector

world = bpy.context.scene.world
bg = world.node_tree.nodes.get("Background")
bg.inputs[0].default_value = (0.62, 0.72, 0.82, 1.0)
bg.inputs[1].default_value = 0.9

sun = bpy.data.objects["MesaSun"]
sun.data.color = (1.0, 0.93, 0.82)
sun.data.energy = 4.0

# frame the camera to look along the canyon from a low, dramatic angle
cam = bpy.data.objects["Camera"]
cam.location = Vector((-2.0, -34.0, 14.0))
cam.rotation_euler = (math.radians(72), 0.0, math.radians(4.0))
cam.data.lens = 32.0

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.camera = cam
print("camera_and_light_set", scene.render.engine)
