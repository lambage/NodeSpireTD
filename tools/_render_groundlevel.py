import bpy
import math
from mathutils import Vector

cam = bpy.data.objects["Camera"]
cam_pos = Vector((3.0, -38.0, 3.5))
target = Vector((0.0, 10.0, 2.5))
direction = (target - cam_pos).normalized()
rot_quat = direction.to_track_quat('-Z', 'Y')
cam.location = cam_pos
cam.rotation_euler = rot_quat.to_euler()
cam.data.lens = 30.0
cam.data.clip_start = 0.05
cam.data.clip_end = 500.0

scene = bpy.context.scene
scene.camera = cam
scene.render.resolution_x = 1366
scene.render.resolution_y = 768
scene.render.filepath = "E:/projects/NodeSpireTD/_to_delete/render_groundlevel.png"
bpy.ops.render.render(write_still=True)
print("groundlevel_rendered")
