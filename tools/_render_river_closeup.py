import bpy
import math
from mathutils import Vector

cam = bpy.data.objects["Camera"]
cam_pos = Vector((-14.0, -6.0, 5.5))
target = Vector((-7.0, 5.0, 1.0))
direction = (target - cam_pos).normalized()
rot_quat = direction.to_track_quat('-Z', 'Y')
cam.location = cam_pos
cam.rotation_euler = rot_quat.to_euler()
cam.data.lens = 35.0
cam.data.clip_start = 0.05
cam.data.clip_end = 500.0

scene = bpy.context.scene
scene.camera = cam
scene.render.resolution_x = 1200
scene.render.resolution_y = 800
scene.render.filepath = "E:/projects/NodeSpireTD/_to_delete/render_river_closeup.png"
bpy.ops.render.render(write_still=True)
print("river_closeup_rendered")
