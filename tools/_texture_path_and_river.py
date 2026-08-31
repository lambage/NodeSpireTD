"""Generate simple tileable dirt-path and water color textures directly as pixel buffers
(no shading network needed, so nothing is lost on glTF export), rebuild the path/river strips
with UVs, and wire the images straight into each material's Base Color.
"""
import bpy
import math
from mathutils import noise, Vector

OUT_DIR = "E:/projects/NodeSpireTD/assets/levels/mesa_canyon/"


def make_texture(name, size, color_fn):
    img = bpy.data.images.get(name)
    if img:
        bpy.data.images.remove(img)
    img = bpy.data.images.new(name, width=size, height=size, alpha=False)
    pixels = [0.0] * (size * size * 4)
    for py in range(size):
        v = py / (size - 1)
        for px in range(size):
            u = px / (size - 1)
            r, g, b = color_fn(u, v)
            idx = (py * size + px) * 4
            pixels[idx + 0] = r
            pixels[idx + 1] = g
            pixels[idx + 2] = b
            pixels[idx + 3] = 1.0
    img.pixels = pixels
    img.filepath_raw = OUT_DIR + name + ".png"
    img.file_format = 'PNG'
    img.save()
    return img


def dirt_color(u, v):
    n1 = noise.noise(Vector((u * 20.0, v * 40.0, 3.0)))
    n2 = noise.noise(Vector((u * 60.0, v * 120.0, 9.0)))
    shade = 0.5 + 0.35 * n1 + 0.15 * n2
    shade = max(0.0, min(1.0, shade))
    base = (0.34, 0.22, 0.13)
    dark = (0.20, 0.12, 0.07)
    r = dark[0] + (base[0] - dark[0]) * shade
    g = dark[1] + (base[1] - dark[1]) * shade
    b = dark[2] + (base[2] - dark[2]) * shade
    # sparse lighter pebbles
    pebble = noise.noise(Vector((u * 140.0, v * 260.0, 21.0)))
    if pebble > 0.62:
        t = (pebble - 0.62) / 0.38
        r = r + (0.55 - r) * t * 0.6
        g = g + (0.48 - g) * t * 0.6
        b = b + (0.40 - b) * t * 0.6
    return r, g, b


def water_color(u, v):
    n1 = noise.noise(Vector((u * 12.0, v * 50.0, 5.0)))
    n2 = noise.noise(Vector((u * 30.0, v * 140.0, 17.0)))
    ripple = 0.5 + 0.5 * math.sin(v * 90.0 + n1 * 6.0)
    shade = 0.55 + 0.30 * n1 + 0.15 * ripple
    shade = max(0.0, min(1.0, shade))
    deep = (0.05, 0.16, 0.22)
    shallow = (0.16, 0.42, 0.46)
    r = deep[0] + (shallow[0] - deep[0]) * shade
    g = deep[1] + (shallow[1] - deep[1]) * shade
    b = deep[2] + (shallow[2] - deep[2]) * shade
    foam = noise.noise(Vector((u * 50.0, v * 220.0, 31.0))) * 0.5 + n2 * 0.2
    if foam > 0.28:
        t = min(1.0, (foam - 0.28) / 0.3)
        r = r + (0.75 - r) * t * 0.5
        g = g + (0.85 - g) * t * 0.5
        b = b + (0.85 - b) * t * 0.5
    return r, g, b


dirt_img = make_texture("mesa_canyon_path_color", 512, dirt_color)
water_img = make_texture("mesa_canyon_water_color", 512, water_color)


def add_strip_uvs(obj_name, row_step, tile_length):
    obj = bpy.data.objects[obj_name]
    mesh = obj.data
    if mesh.uv_layers:
        mesh.uv_layers.remove(mesh.uv_layers[0])
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for poly in mesh.polygons:
        for li in poly.loop_indices:
            vidx = mesh.loops[li].vertex_index
            u = 0.0 if vidx % 2 == 0 else 1.0
            row = vidx // 2
            v = (row * row_step) / tile_length
            uv_layer.data[li].uv = (u, v)


add_strip_uvs("MesaPathPreview", 2.0, 6.0)
add_strip_uvs("MesaRiver", 2.0, 8.0)


def wire_image_material(mat_name, img):
    mat = bpy.data.materials.get(mat_name)
    if mat:
        bpy.data.materials.remove(mat)
    mat = bpy.data.materials.new(mat_name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    tex_node = nt.nodes.new('ShaderNodeTexImage')
    tex_node.image = img
    tex_node.location = (-350, 0)
    nt.links.new(tex_node.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.9 if "path" in mat_name.lower() else 0.2
    return mat


path_mat = wire_image_material("MesaPathPreviewMat", dirt_img)
water_mat = wire_image_material("MesaWater", water_img)

path_obj = bpy.data.objects["MesaPathPreview"]
path_obj.data.materials.clear()
path_obj.data.materials.append(path_mat)

river_obj = bpy.data.objects["MesaRiver"]
river_obj.data.materials.clear()
river_obj.data.materials.append(water_mat)

print("TEXTURES_WIRED")
