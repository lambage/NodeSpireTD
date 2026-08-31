import bpy

names = ["MesaCanyon", "MesaRiver", "MesaPathPreview", "Start", "Waypoint_1", "Waypoint_2", "Waypoint_3", "End"]
bpy.ops.object.select_all(action='DESELECT')
for n in names:
    obj = bpy.data.objects.get(n)
    if obj:
        obj.select_set(True)

out_path = "E:/projects/NodeSpireTD/assets/levels/mesa_canyon/mesa_canyon_map.glb"
bpy.ops.export_scene.gltf(
    filepath=out_path,
    use_selection=True,
    export_format='GLB',
    export_apply=True,
    export_yup=True,
    export_cameras=False,
    export_lights=False,
)
print("EXPORTED", out_path)
