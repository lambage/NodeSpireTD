import bpy
import math
from mathutils import Vector

cam = bpy.data.objects["Camera"]
cam_pos = Vector((0.0, -5.0, 70.0))
target = Vector((0.0, -5.0, 0.0))
direction = (target - cam_pos).normalized()
rot_quat = direction.to_track_quat('-Z', 'Y')
cam.location = cam_pos
cam.rotation_euler = rot_quat.to_euler()
cam.data.lens = 20.0
cam.data.clip_end = 500.0

scene = bpy.context.scene
scene.camera = cam
scene.render.resolution_x = 900
scene.render.resolution_y = 1100
scene.render.filepath = "E:/projects/NodeSpireTD/_to_delete/render_topdown.png"
bpy.ops.render.render(write_still=True)
print("topdown_rendered")
