import bpy
import math

obj = bpy.data.objects["MesaCanyon"]
cam = bpy.data.objects["Camera"]

for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        for region in area.regions:
            if region.type == 'WINDOW':
                override = {"area": area, "region": region}
                with bpy.context.temp_override(**override):
                    bpy.ops.object.select_all(action='DESELECT')
                    obj.select_set(True)
                    bpy.context.view_layer.objects.active = obj
                    for space in area.spaces:
                        if space.type == 'VIEW_3D':
                            space.region_3d.view_perspective = 'CAMERA'
                    bpy.ops.view3d.view_selected()
                    bpy.ops.view3d.camera_to_view_selected()

cam.data.lens = 38.0
print("camera_fit", cam.location, cam.rotation_euler)
