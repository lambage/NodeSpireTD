"""Procedural (non image-based) shader for the mesa canyon terrain.

Reads the 'terrain_mask' vertex-color attribute baked by mesa_canyon_gen.py:
  R = height fraction (0 canyon floor .. 1 plateau/butte top)
  G = slope (0 flat .. 1 vertical)
Blends a sandy floor/plateau-top look with a banded sedimentary rock look on steep faces.
"""
import bpy

MAT_NAME = "MesaCanyonMaterial"

mat = bpy.data.materials.get(MAT_NAME)
if mat:
    bpy.data.materials.remove(mat)
mat = bpy.data.materials.new(MAT_NAME)
mat.use_nodes = True
nt = mat.node_tree
nodes = nt.nodes
links = nt.links
nodes.clear()


def n(type_, x, y, **kw):
    node = nodes.new(type_)
    node.location = (x, y)
    for k, v in kw.items():
        setattr(node, k, v)
    return node


out = n('ShaderNodeOutputMaterial', 900, 0)
bsdf = n('ShaderNodeBsdfPrincipled', 650, 0)
links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
bsdf.inputs['Roughness'].default_value = 0.92
bsdf.inputs['Specular IOR Level'].default_value = 0.25

attr = n('ShaderNodeAttribute', -900, 0, attribute_name="terrain_mask")

# --- sand color: warm tan with gentle noise variation, darker in the canyon floor ---
sand_noise = n('ShaderNodeTexNoise', -650, 250, )
sand_noise.inputs['Scale'].default_value = 18.0
sand_noise.inputs['Detail'].default_value = 4.0
sand_noise.inputs['Roughness'].default_value = 0.55

sand_ramp = n('ShaderNodeValToRGB', -400, 250)
sand_ramp.color_ramp.elements[0].position = 0.35
sand_ramp.color_ramp.elements[0].color = (0.55, 0.40, 0.24, 1.0)
sand_ramp.color_ramp.elements[1].position = 0.65
sand_ramp.color_ramp.elements[1].color = (0.72, 0.55, 0.34, 1.0)
links.new(sand_noise.outputs['Fac'], sand_ramp.inputs['Fac'])

height_ramp = n('ShaderNodeValToRGB', -400, 60)
height_ramp.color_ramp.elements[0].position = 0.0
height_ramp.color_ramp.elements[0].color = (0.42, 0.30, 0.20, 1.0)  # canyon floor: deeper red-brown sand
height_ramp.color_ramp.elements[1].position = 1.0
height_ramp.color_ramp.elements[1].color = (0.78, 0.62, 0.40, 1.0)  # plateau top: pale dry sand
links.new(attr.outputs['Color'], height_ramp.inputs['Fac'])

sand_mix = n('ShaderNodeMixRGB', -150, 150, blend_type='MULTIPLY')
sand_mix.inputs['Fac'].default_value = 0.5
links.new(height_ramp.outputs['Color'], sand_mix.inputs['Color1'])
links.new(sand_ramp.outputs['Color'], sand_mix.inputs['Color2'])

# --- rock color: banded sedimentary strata using world Z height ---
sep_xyz = n('ShaderNodeSeparateXYZ', -900, -300)
tex_coord = n('ShaderNodeTexCoord', -1150, -300)
links.new(tex_coord.outputs['Object'], sep_xyz.inputs['Vector'])

band_noise = n('ShaderNodeTexNoise', -900, -500)
band_noise.inputs['Scale'].default_value = 2.0
band_noise.inputs['Detail'].default_value = 2.0

band_wave = n('ShaderNodeTexWave', -650, -420, wave_type='BANDS', bands_direction='Z')
band_wave.inputs['Scale'].default_value = 1.6
band_wave.inputs['Distortion'].default_value = 1.2
band_wave.inputs['Detail'].default_value = 2.0
links.new(band_noise.outputs['Fac'], band_wave.inputs['Distortion'])

rock_ramp = n('ShaderNodeValToRGB', -400, -420)
e0 = rock_ramp.color_ramp.elements[0]
e0.position = 0.30
e0.color = (0.42, 0.20, 0.13, 1.0)  # rust red band
e1 = rock_ramp.color_ramp.elements[1]
e1.position = 0.65
e1.color = (0.62, 0.42, 0.28, 1.0)  # tan band
rock_ramp.color_ramp.elements.new(0.9)
rock_ramp.color_ramp.elements[2].color = (0.35, 0.25, 0.20, 1.0)  # dark band
links.new(band_wave.outputs['Fac'], rock_ramp.inputs['Fac'])

rock_detail_noise = n('ShaderNodeTexNoise', -400, -600)
rock_detail_noise.inputs['Scale'].default_value = 40.0
rock_detail_noise.inputs['Detail'].default_value = 6.0
rock_mix = n('ShaderNodeMixRGB', -150, -450, blend_type='MULTIPLY')
rock_mix.inputs['Fac'].default_value = 0.35
links.new(rock_ramp.outputs['Color'], rock_mix.inputs['Color1'])
links.new(rock_detail_noise.outputs['Fac'], rock_mix.inputs['Color2'])

# --- blend sand vs rock based on slope (G channel of terrain_mask) ---
sep_mask = n('ShaderNodeSeparateColor', -650, 0)
links.new(attr.outputs['Color'], sep_mask.inputs['Color'])

slope_ramp = n('ShaderNodeValToRGB', -400, -80)
slope_ramp.color_ramp.elements[0].position = 0.15
slope_ramp.color_ramp.elements[0].color = (0, 0, 0, 1)
slope_ramp.color_ramp.elements[1].position = 0.45
slope_ramp.color_ramp.elements[1].color = (1, 1, 1, 1)
links.new(sep_mask.outputs['Green'], slope_ramp.inputs['Fac'])

final_mix = n('ShaderNodeMixRGB', 150, 0)
links.new(slope_ramp.outputs['Color'], final_mix.inputs['Fac'])
links.new(sand_mix.outputs['Color'], final_mix.inputs['Color1'])
links.new(rock_mix.outputs['Color'], final_mix.inputs['Color2'])
links.new(final_mix.outputs['Color'], bsdf.inputs['Base Color'])

# --- bump: fine surface roughness + reinforce strata bands on rock ---
bump_noise = n('ShaderNodeTexNoise', -150, -750)
bump_noise.inputs['Scale'].default_value = 45.0
bump = n('ShaderNodeBump', 350, -300)
bump.inputs['Strength'].default_value = 0.25
links.new(bump_noise.outputs['Fac'], bump.inputs['Height'])
links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

obj = bpy.data.objects["MesaCanyon"]
obj.data.materials.clear()
obj.data.materials.append(mat)
print("MATERIAL_ASSIGNED", mat.name)
