"""Bake the procedural terrain shader to an image texture and rewire the material to use it,
since glTF/GLB only supports simple PBR factors + baked textures (no Blender node graphs, and
the vertex-color-driven Attribute node used for sand/rock blending does not survive export).
"""
import bpy

obj = bpy.data.objects["MesaCanyon"]
mesh = obj.data

# UV unwrap (smart project handles the folded cliff/plateau geometry reasonably)
bpy.context.view_layer.objects.active = obj
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
bpy.ops.object.mode_set(mode='OBJECT')

# bake target image
IMG_NAME = "MesaCanyonBakedColor"
old_img = bpy.data.images.get(IMG_NAME)
if old_img:
    bpy.data.images.remove(old_img)
bake_img = bpy.data.images.new(IMG_NAME, width=2048, height=2048, alpha=False)

mat = obj.data.materials[0]
nt = mat.node_tree
bake_node = nt.nodes.new('ShaderNodeTexImage')
bake_node.image = bake_img
bake_node.select = True
nt.nodes.active = bake_node

scene = bpy.context.scene
prev_engine = scene.render.engine
scene.render.engine = 'CYCLES'
scene.cycles.samples = 32
bpy.context.view_layer.update()

bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, margin=8)

scene.render.engine = prev_engine

tex_dir = "E:/projects/NodeSpireTD/assets/levels/mesa_canyon/"
bake_img.filepath_raw = tex_dir + "mesa_canyon_terrain_color.png"
bake_img.file_format = 'PNG'
bake_img.save()

# rewire the material to a simple baked-texture material for export
nodes = nt.nodes
links = nt.links
bsdf = None
for n in nodes:
    if n.type == 'BSDF_PRINCIPLED':
        bsdf = n
        break
links.new(bake_node.outputs['Color'], bsdf.inputs['Base Color'])
bsdf.inputs['Roughness'].default_value = 0.9
bsdf.inputs['Specular IOR Level'].default_value = 0.2
if 'Normal' in bsdf.inputs:
    for link in list(bsdf.inputs['Normal'].links):
        links.remove(link)

print("BAKED_AND_REWIRED", bake_img.filepath_raw)
