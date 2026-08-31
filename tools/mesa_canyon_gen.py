"""Procedural mesa canyon TD-map generator, executed inside Blender via the MCP execute_code command.

Layout goals (see gameplay-architecture notes: 4m no-build corridor = pathCorridorHalfWidth 2.0,
tower footprint radius ~0.6m*scale, min tower spacing 1.7m, max buildable slope 30 degrees):
  - A wide, meandering canyon floor with room for: a no-build path lane, a river lane, and
    buildable flat lanes on both sides for towers.
  - Flat-topped mesa plateaus flanking the canyon (buildable) with terraced/strata cliff faces
    (too steep to build on, matching the engine's slope-based placement rule).
  - A tall outer rim ring far beyond the playable canyon so the horizon is never empty sky/void
    when the free-roaming camera wanders (no skybox yet), self-contained "box canyon" arena.
  - Start / Waypoint_N / End marker empties along the canyon centerline (matches the engine's
    required map marker naming convention) plus a river strip mesh for a water feature.
"""
import bpy
import bmesh
import math
import random
from mathutils import noise, Vector

random.seed(7)

SIZE = 140.0
RES = 175  # segments per side (~0.8m spacing)

CANYON_HALF_WIDTH = 16.0
CLIFF_WIDTH = 6.5
FLOOR_H = 0.0
PLATEAU_H = 11.0
STRATA_LEVELS = 4
STRATA_BLEND = 0.45
STRATA_EDGE = 0.28

# path runs along Y through the canyon; river is offset to one side of the path
PATH_Y_MIN = -40.0
PATH_Y_MAX = 40.0
MARKER_STEP = 2.0  # route marker spacing; 2m keeps chord sag under ~3cm on the tightest bend
RIVER_OFFSET_X = -7.0
RIVER_HALF_WIDTH = 2.2
RIVER_DEPTH = 1.0
RIVER_BANK = 1.2

# outer rim: far beyond the canyon, height ramps up into an enclosing wall so the
# camera never sees an empty edge-of-world in any direction
RIM_START = 50.0
RIM_END = 66.0
RIM_HEIGHT = 26.0


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def centerline_x(y):
    return 6.0 * math.sin(y * 0.045) + 3.0 * math.sin(y * 0.11 + 1.3)


def butte_profile(dist, radius, cliff_w):
    return smoothstep(1.0 - (dist - radius) / max(cliff_w, 0.001))


def river_dip(x, y, mask):
    if y < PATH_Y_MIN - 6.0 or y > PATH_Y_MAX + 6.0:
        return 0.0
    rx = centerline_x(y) + RIVER_OFFSET_X
    d = abs(x - rx)
    channel = 1.0 - smoothstep((d - RIVER_HALF_WIDTH) / max(RIVER_BANK, 0.001))
    # fade the channel out as the ground rises toward the cliffs/plateau
    return channel * RIVER_DEPTH * (1.0 - smoothstep(mask / 0.35))


def height_and_mask(x, y):
    center_x = centerline_x(y)
    dist = abs(x - center_x)

    width_var = 2.0 * noise.noise(Vector((x * 0.04, y * 0.04, 0.0)))
    half_w = CANYON_HALF_WIDTH + width_var
    cliff_w = CLIFF_WIDTH + 1.5 * noise.noise(Vector((x * 0.04 + 50, y * 0.04, 0.0)))

    t = smoothstep((dist - half_w) / max(cliff_w, 0.001))

    plateau_noise = 6.0 * noise.turbulence(Vector((x * 0.015, y * 0.015, 0.0)), 3, False)
    plateau_h = PLATEAU_H + plateau_noise
    floor_noise = 0.35 * noise.noise(Vector((x * 0.12, y * 0.12, 5.0)))
    floor_h = FLOOR_H + floor_noise

    # a few wide, flat-treaded terraces with smooth risers (sedimentary rock shelves)
    tt = t * STRATA_LEVELS
    i = math.floor(tt)
    f = tt - i
    if f < STRATA_EDGE:
        f2 = smoothstep(f / STRATA_EDGE) * 0.5
    elif f > 1.0 - STRATA_EDGE:
        f2 = 0.5 + smoothstep((f - (1.0 - STRATA_EDGE)) / STRATA_EDGE) * 0.5
    else:
        f2 = 0.5
    step_t = (i + f2) / STRATA_LEVELS
    t_final = t * (1.0 - STRATA_BLEND) + step_t * STRATA_BLEND

    height = floor_h + t_final * (plateau_h - floor_h)
    mask = t  # 0 = canyon floor, 1 = plateau top

    for (bx, by, br, bcw, bh) in BUTTES:
        d = math.hypot(x - bx, y - by)
        bt = butte_profile(d, br, bcw)
        if bt > 0.001:
            butte_top = height + bh
            height = height * (1.0 - bt) + butte_top * bt
            mask = max(mask, bt)

    height -= river_dip(x, y, mask)

    # outer rim wall, far outside the playable canyon
    radial = max(abs(x), abs(y))
    rim_t = smoothstep((radial - RIM_START) / max(RIM_END - RIM_START, 0.001))
    if rim_t > 0.0:
        height += rim_t * RIM_HEIGHT
        mask = max(mask, rim_t)

    return height, mask


BUTTES = []
for _ in range(4):
    bx = random.uniform(-SIZE * 0.32, SIZE * 0.32)
    by = random.uniform(-SIZE * 0.32, SIZE * 0.32)
    if abs(bx - centerline_x(by)) < CANYON_HALF_WIDTH + CLIFF_WIDTH + 6.0:
        continue
    BUTTES.append((bx, by, random.uniform(4.0, 7.0), random.uniform(2.5, 4.5), random.uniform(3.0, 7.0)))


def build_terrain():
    old = bpy.data.objects.get("MesaCanyon")
    if old:
        bpy.data.objects.remove(old, do_unlink=True)

    bm = bmesh.new()
    verts = [[None] * (RES + 1) for _ in range(RES + 1)]
    step = SIZE / RES
    for iy in range(RES + 1):
        y = -SIZE / 2 + iy * step
        for ix in range(RES + 1):
            x = -SIZE / 2 + ix * step
            h, _mask = height_and_mask(x, y)
            v = bm.verts.new((x, y, h))
            verts[iy][ix] = v

    bm.verts.ensure_lookup_table()
    for iy in range(RES):
        for ix in range(RES):
            v1 = verts[iy][ix]
            v2 = verts[iy][ix + 1]
            v3 = verts[iy + 1][ix + 1]
            v4 = verts[iy + 1][ix]
            bm.faces.new((v1, v2, v3, v4))

    bm.normal_update()

    mesh = bpy.data.meshes.new("MesaCanyonMesh")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new("MesaCanyon", mesh)
    bpy.context.collection.objects.link(obj)

    mesh.color_attributes.new("terrain_mask", 'FLOAT_COLOR', 'CORNER')
    mask_layer = mesh.color_attributes["terrain_mask"]

    for poly in mesh.polygons:
        for li in poly.loop_indices:
            loop = mesh.loops[li]
            co = mesh.vertices[loop.vertex_index].co
            _h, mask = height_and_mask(co.x, co.y)
            nz = mesh.vertices[loop.vertex_index].normal.z
            slope = 1.0 - max(0.0, min(1.0, nz))
            mask_layer.data[li].color = (mask, slope, co.z / PLATEAU_H, 1.0)

    mesh.update()
    obj.rotation_mode = 'XYZ'

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35))
    except Exception:
        bpy.ops.object.shade_smooth()

    return obj


def build_river():
    old = bpy.data.objects.get("MesaRiver")
    if old:
        bpy.data.objects.remove(old, do_unlink=True)

    bm = bmesh.new()
    step = 2.0
    y = PATH_Y_MIN - 6.0
    rows = []
    while y <= PATH_Y_MAX + 6.0:
        rx = centerline_x(y) + RIVER_OFFSET_X
        h, mask = height_and_mask(rx, y)
        z = h + river_dip(rx, y, mask) - 0.3  # sit slightly below the carved bank tops
        v1 = bm.verts.new((rx - RIVER_HALF_WIDTH * 0.85, y, z))
        v2 = bm.verts.new((rx + RIVER_HALF_WIDTH * 0.85, y, z))
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
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.shade_smooth()
    return obj


def build_markers():
    for name in list(bpy.data.objects.keys()):
        if name == "Start" or name == "End" or name.startswith("Waypoint_"):
            bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)

    # engine walks route points in straight lines, so sample the meandering centerline densely
    # enough that the chords stay on the painted path and follow the terrain height
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
        h, _mask = height_and_mask(x, y)
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = 'PLAIN_AXES'
        empty.empty_display_size = 0.6
        empty.location = (x, y, h + 0.05)
        bpy.context.collection.objects.link(empty)


terrain_obj = build_terrain()
river_obj = build_river()
build_markers()
print("BUILT", terrain_obj.name, len(terrain_obj.data.polygons), "faces;", river_obj.name, "river segments")
