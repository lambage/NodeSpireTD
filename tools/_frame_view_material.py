import bpy

obj = bpy.data.objects["MesaCanyon"]
for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        for region in area.regions:
            if region.type == 'WINDOW':
                override = {"area": area, "region": region}
                with bpy.context.temp_override(**override):
                    bpy.ops.object.select_all(action='DESELECT')
                    obj.select_set(True)
                    bpy.context.view_layer.objects.active = obj
                    bpy.ops.view3d.view_selected()
                for space in area.spaces:
                    if space.type == 'VIEW_3D':
                        space.shading.type = 'MATERIAL'
                        space.overlay.show_overlays = False
print("framed_material")
