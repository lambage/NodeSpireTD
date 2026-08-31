import bpy
import math
from mathutils import Vector

cam = bpy.data.objects["Camera"]
cam_pos = Vector((14.0, -105.0, 58.0))
target = Vector((0.0, 5.0, 2.0))
direction = (target - cam_pos).normalized()
rot_quat = direction.to_track_quat('-Z', 'Y')
cam.location = cam_pos
cam.rotation_euler = rot_quat.to_euler()
cam.data.lens = 24.0
cam.data.clip_start = 0.5
cam.data.clip_end = 500.0

scene = bpy.context.scene
scene.camera = cam
scene.render.resolution_x = 1280
scene.render.resolution_y = 870
scene.render.filepath = "E:/projects/NodeSpireTD/assets/levels/mesa_canyon/mesa_canyon.png"
scene.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
print("thumbnail_rendered")
