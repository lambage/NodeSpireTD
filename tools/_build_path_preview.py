"""Visual-only preview strip along the path centerline (matches the engine's ~4m no-build
corridor: pathCorridorHalfWidth 2.0) so lane widths (river / path / tower lanes) can be
sanity-checked directly in the viewport. Uses ray_cast against the real terrain mesh so it
doesn't need to duplicate the height/noise functions from mesa_canyon_gen.py.
"""
import bpy
import bmesh
import math
from mathutils import Vector

PATH_Y_MIN = -40.0
PATH_Y_MAX = 40.0
PATH_HALF_WIDTH = 2.0


def centerline_x(y):
    return 6.0 * math.sin(y * 0.045) + 3.0 * math.sin(y * 0.11 + 1.3)


def sample_height(terrain_obj, x, y):
    origin = Vector((x, y, 200.0))
    direction = Vector((0.0, 0.0, -1.0))
    ok, loc, _normal, _idx = terrain_obj.ray_cast(origin, direction, distance=400.0)
    return loc.z if ok else 0.0


def build_path_preview():
    old = bpy.data.objects.get("MesaPathPreview")
    if old:
        bpy.data.objects.remove(old, do_unlink=True)

    terrain = bpy.data.objects["MesaCanyon"]
    bm = bmesh.new()
    step = 2.0
    y = PATH_Y_MIN
    rows = []
    while y <= PATH_Y_MAX:
        cx = centerline_x(y)
        z = sample_height(terrain, cx, y) + 0.05
        v1 = bm.verts.new((cx - PATH_HALF_WIDTH, y, z))
        v2 = bm.verts.new((cx + PATH_HALF_WIDTH, y, z))
        rows.append((v1, v2))
        y += step

    bm.verts.ensure_lookup_table()
    for i in range(len(rows) - 1):
        a1, a2 = rows[i]
        b1, b2 = rows[i + 1]
        bm.faces.new((a1, a2, b2, b1))

    bm.normal_update()
    mesh = bpy.data.meshes.new("MesaPathPreviewMesh")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new("MesaPathPreview", mesh)
    bpy.context.collection.objects.link(obj)

    mat = bpy.data.materials.get("MesaPathPreviewMat")
    if mat:
        bpy.data.materials.remove(mat)
    mat = bpy.data.materials.new("MesaPathPreviewMat")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs['Base Color'].default_value = (0.30, 0.18, 0.10, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.95
    obj.data.materials.append(mat)
    return obj


path_obj = build_path_preview()
print("PATH_PREVIEW_BUILT", path_obj.name)
