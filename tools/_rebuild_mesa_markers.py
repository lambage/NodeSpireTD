"""Rebuild the mesa canyon route markers in an existing .blend without regenerating terrain.

Mirrors build_markers() in mesa_canyon_gen.py, but ray_casts the real MesaCanyon mesh for
height so the markers match the shipped geometry rather than the analytic height function.
"""
import bpy
import math
from mathutils import Vector

PATH_Y_MIN = -40.0
PATH_Y_MAX = 40.0
MARKER_STEP = 2.0


def centerline_x(y):
    return 6.0 * math.sin(y * 0.045) + 3.0 * math.sin(y * 0.11 + 1.3)


def sample_height(terrain_obj, x, y):
    ok, loc, _normal, _idx = terrain_obj.ray_cast(Vector((x, y, 200.0)), Vector((0.0, 0.0, -1.0)), distance=400.0)
    return loc.z if ok else 0.0


for name in list(bpy.data.objects.keys()):
    if name == "Start" or name == "End" or name.startswith("Waypoint_"):
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)

terrain = bpy.data.objects["MesaCanyon"]

marker_ys = []
y = PATH_Y_MIN
while y < PATH_Y_MAX - 1e-4:
    marker_ys.append(y)
    y += MARKER_STEP
marker_ys.append(PATH_Y_MAX)

for i, y in enumerate(marker_ys):
    if i == 0:
        name = "Start"
    elif i == len(marker_ys) - 1:
        name = "End"
    else:
        name = f"Waypoint_{i}"
    x = centerline_x(y)
    empty = bpy.data.objects.new(name, None)
    empty.empty_display_type = 'PLAIN_AXES'
    empty.empty_display_size = 0.6
    empty.location = (x, y, sample_height(terrain, x, y) + 0.05)
    bpy.context.collection.objects.link(empty)

print("MARKERS", len(marker_ys))
