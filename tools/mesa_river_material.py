import bpy

mat = bpy.data.materials.get("MesaWater")
if mat:
    bpy.data.materials.remove(mat)
mat = bpy.data.materials.new("MesaWater")
mat.use_nodes = True
bsdf = mat.node_tree.nodes.get("Principled BSDF")
bsdf.inputs['Base Color'].default_value = (0.10, 0.30, 0.34, 1.0)
bsdf.inputs['Roughness'].default_value = 0.15
bsdf.inputs['Transmission Weight'].default_value = 0.55
bsdf.inputs['IOR'].default_value = 1.33

obj = bpy.data.objects["MesaRiver"]
obj.data.materials.clear()
obj.data.materials.append(mat)
print("river_material_assigned")
