"""Widen the river strip so its edges actually meet the carved bank slope (was a narrower
flat plane using a single centerline height sample, so it read as floating over the channel).
Re-samples the real terrain height at each edge via ray_cast instead of assuming a flat bed.
"""
import bpy
import bmesh
import math
from mathutils import Vector

PATH_Y_MIN = -40.0
PATH_Y_MAX = 40.0
RIVER_OFFSET_X = -7.0
WATER_HALF_WIDTH = 2.5  # reaches slightly past the flat channel floor into the bank base
EMBED = 0.06


def centerline_x(y):
    return 6.0 * math.sin(y * 0.045) + 3.0 * math.sin(y * 0.11 + 1.3)


def sample_height(terrain_obj, x, y):
    origin = Vector((x, y, 200.0))
    direction = Vector((0.0, 0.0, -1.0))
    ok, loc, _normal, _idx = terrain_obj.ray_cast(origin, direction, distance=400.0)
    return loc.z if ok else 0.0


def build_river():
    old = bpy.data.objects.get("MesaRiver")
    old_mesh = old.data if old else None
    old_mat = old.data.materials[0] if old and old.data.materials else None
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    if old_mesh:
        bpy.data.meshes.remove(old_mesh)

    terrain = bpy.data.objects["MesaCanyon"]
    bm = bmesh.new()
    step = 2.0
    y = PATH_Y_MIN - 6.0
    rows = []
    while y <= PATH_Y_MAX + 6.0:
        rx = centerline_x(y) + RIVER_OFFSET_X
        left_x = rx - WATER_HALF_WIDTH
        right_x = rx + WATER_HALF_WIDTH
        left_z = sample_height(terrain, left_x, y) - EMBED
        right_z = sample_height(terrain, right_x, y) - EMBED
        v1 = bm.verts.new((left_x, y, left_z))
        v2 = bm.verts.new((right_x, y, right_z))
        rows.append((v1, v2))
        y += step

    bm.verts.ensure_lookup_table()
    for i in range(len(rows) - 1):
        a1, a2 = rows[i]
        b1, b2 = rows[i + 1]
        bm.faces.new((a1, a2, b2, b1))

    bm.normal_update()
    mesh = bpy.data.meshes.new("MesaRiverMesh")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new("MesaRiver", mesh)
    bpy.context.collection.objects.link(obj)
    if old_mat:
        obj.data.materials.append(old_mat)

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.shade_smooth()

    # re-tile UVs to match the new width/row spacing
    uv_layer = mesh.uv_layers.new(name="UVMap")
    tile_length = 8.0
    for poly in mesh.polygons:
        for li in poly.loop_indices:
            vidx = mesh.loops[li].vertex_index
            u = 0.0 if vidx % 2 == 0 else 1.0
            row = vidx // 2
            v = (row * step) / tile_length
            uv_layer.data[li].uv = (u, v)

    return obj


river_obj = build_river()
print("RIVER_REBUILT", river_obj.name, len(river_obj.data.polygons))
