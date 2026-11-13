"""Relief and street dressing on the hero block (look pass 2, claude-docs/city-look.md).

Render-only, so nothing here can change a collision, the traversal audit or the golden hashes:

* relief on the street faces of every building within HERO_RADIUS of the PlayerStart: a stepped cornice under the
  parapet and a shop cornice over the sign band (buildings over 7 m, as M_Facade draws them), a sill and a lintel on
  every window M_Facade draws (its 160 cm bays and 330 cm storeys, on the face's own coordinate), belt courses on long
  fronts, a door with a surround and a two-step stoop per building, and canvas awnings over some shops (the shop
  bays M_Facade picks per building, from the same hash, computed in float32 here);
* bare street trees in tree pits along the sidewalks, crosswalk bars at the corners, newspaper boxes.

Every piece is a box: (cx, cy, z0, length along, depth out, height, yaw degrees, pitch degrees), base-centred.
``plan`` is pure Python (a stub ``unreal`` is enough); ``build`` needs the editor.
"""

import math
import struct

from _district import PARAPET_HEIGHT, PARAPET_MIN_HEIGHT_M, ROAD_TOP, SIDEWALK_TOP, SIDEWALK_WIDTH, road_paths
import _geo as geo

PLAN_VERSION = 2
HERO_RADIUS = 12000.0          # cm from the PlayerStart to a building's nearest wall, a tree or a crossing
MIN_EDGE = 300.0               # cm; shorter walls get nothing
ROAD_REACH = 2600.0            # cm from a wall's middle to a road centre line in front of it
FACING = 0.8                   # cos of the angle between the wall's normal and the way to that road
BAY, STOREY, SILL, WIN_W, WIN_H = 160.0, 330.0, 95.0, 110.0, 150.0
UPPER_FROM, TOP_CLEAR = 420.0, 165.0
TALL = 700.0
BELT_MIN_EDGE = 2000.0         # one belt course, over the shops, on fronts this long
TREE_SPACING = 900.0
TREE_KERB_IN = 110.0           # cm in from the kerb
TREE_CLEAR = 250.0             # cm from lamps, hydrants, bins and the PlayerStart
AWNING_SHARE = 0.35
AWNING_TILT = -50.0           # degrees of roll on the canvas: steep enough that snow slides off (MF_Snow: normal z 0.7)

# Material keys (MI_Prop_<key> under M_Prop unless noted): (colour, roughness).
COLOURS = {
    "TrimCream": ((0.40, 0.37, 0.31), 0.8),
    "TrimBrown": ((0.12, 0.07, 0.05), 0.8),
    "TrimGrey": ((0.28, 0.28, 0.27), 0.85),
    "DoorWood": ((0.07, 0.035, 0.02), 0.5),
    "AwningGreen": ((0.03, 0.12, 0.05), 0.8),
    "AwningRed": ((0.35, 0.03, 0.03), 0.8),
    "AwningNavy": ((0.02, 0.03, 0.12), 0.8),
    "AwningCream": ((0.55, 0.50, 0.40), 0.8),
    "TreeBark": ((0.05, 0.045, 0.04), 0.9),
    "TreePit": ((0.04, 0.035, 0.03), 0.9),
    "NewsBlue": ((0.03, 0.08, 0.30), 0.45),
    "NewsRed": ((0.40, 0.03, 0.03), 0.45),
}
AWNINGS = ("AwningGreen", "AwningRed", "AwningNavy", "AwningCream")
PAINT = "Paint"                # M_RoadPaint


def _f(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def _frac(x):
    return _f(x - math.floor(x))


def h21(x, y):
    """_look's FLook.H21 in float32."""
    qx, qy, qz = _frac(_f(x * _f(0.1031))), _frac(_f(y * _f(0.1031))), _frac(_f(x * _f(0.1031)))
    d = _f(_f(qx * _f(qy + 33.33)) + _f(qy * _f(qz + 33.33)) + _f(qz * _f(qx + 33.33)))
    qx, qy, qz = _f(qx + d), _f(qy + d), _f(qz + d)
    return _frac(_f(_f(qx + qy) * qz))


def trim_key(style):
    return {"Brownstone": "TrimBrown", "Stone": "TrimGrey", "Painted": "TrimGrey"}.get(style, "TrimCream")


def outward_edges(ring):
    """[(a, b, length, (ux, uy) along, (ox, oy) out)] for a ring in either winding."""
    out = []
    for i in range(len(ring)):
        a, b = ring[i], ring[(i + 1) % len(ring)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length < 1e-3:
            continue
        ux, uy = dx / length, dy / length
        ox, oy = uy, -ux
        mid = ((a[0] + b[0]) * 0.5 + ox * 20.0, (a[1] + b[1]) * 0.5 + oy * 20.0)
        if geo.point_in_polygon(mid, ring):
            ox, oy = -ox, -oy
        out.append((a, b, length, (ux, uy), (ox, oy)))
    return out


def _seg_dist(p, a, b):
    ax, ay = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((p[0] - a[0]) * ax + (p[1] - a[1]) * ay) / max(ax * ax + ay * ay, 1e-9)))
    cx, cy = a[0] + ax * t, a[1] + ay * t
    return math.hypot(p[0] - cx, p[1] - cy), (cx, cy)


def street_edges(ring, segments):
    """The ring's outward edges that face a road centre line within ROAD_REACH."""
    out = []
    for edge in outward_edges(ring):
        a, b, length, _u, (ox, oy) = edge
        if length < MIN_EDGE:
            continue
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        for p, q, _half in segments:
            d, (cx, cy) = _seg_dist(mid, p, q)
            if 1.0 < d < ROAD_REACH and ((cx - mid[0]) * ox + (cy - mid[1]) * oy) / d >= FACING:
                out.append(edge)
                break
    return out


def _segments(district):
    return [(p, q, rec["width_m"] * 50.0) for rec, paths in road_paths(district) for path in paths
            for p, q in zip(path, path[1:])]


def _box(out, key, x, y, z0, along, depth, height, ux, uy, ox, oy, pitch=0.0):
    """A box against a wall: (x, y) on the wall line, depth out of it."""
    yaw = math.degrees(math.atan2(uy, ux))
    out.setdefault(key, []).append((round(x + ox * depth * 0.5, 2), round(y + oy * depth * 0.5, 2), round(z0, 2),
                                    round(along, 2), round(depth, 2), round(height, 2), round(yaw, 3), pitch))


def relief(district, start, records):
    """{material key: [box]} for the hero buildings. ``records`` [(rec, ring cm, style)]."""
    segments = _segments(district)
    out = {}
    for rec, ring, style in records:
        if min(math.hypot(x - start[0], y - start[1]) for x, y in ring) > HERO_RADIUS:
            continue
        height = rec["height_m"] * 100.0
        top = height + (PARAPET_HEIGHT if rec["height_m"] >= PARAPET_MIN_HEIGHT_M else 0.0)
        tall = top > TALL
        trim = trim_key(style)
        x0, y0, x1, y1 = geo.bounds(ring)
        bs = h21(math.floor((x0 + x1) * 0.5 / 100.0), math.floor((y0 + y1) * 0.5 / 100.0))
        pitch_s = 280.0 if bs < 0.33 else (330.0 if bs < 0.66 else 400.0)
        door_done = False
        for a, b, length, (ux, uy), (ox, oy) in street_edges(ring, segments):
            tx, ty = -oy, ox                                   # the shader's tangent: up x normal
            ua = a[0] * tx + a[1] * ty
            sgn = ux * tx + uy * ty                            # +1 or -1: which way u runs along the edge
            u_lo, u_hi = sorted((ua, ua + sgn * length))

            def at(u, a=a, ua=ua, sgn=sgn, ux=ux, uy=uy):
                s = (u - ua) * sgn
                return a[0] + ux * s, a[1] + uy * s

            mx, my = at((u_lo + u_hi) * 0.5)
            if tall:
                for z0, h, d in ((top - 152.0, 12.0, 12.0), (top - 140.0, 40.0, 22.0), (top - 100.0, 12.0, 38.0)):
                    _box(out, trim, mx, my, z0, length, d, h, ux, uy, ox, oy)
                _box(out, trim, mx, my, 392.0, length - 10.0, 16.0, 16.0, ux, uy, ox, oy)
            if length >= BELT_MIN_EDGE and STOREY * 2 + 14.0 < top - 200.0:
                _box(out, trim, mx, my, STOREY * 2, length - 10.0, 6.0, 14.0, ux, uy, ox, oy)
            # sills and lintels on M_Facade's windows that fit the wall
            k = math.floor(u_lo / BAY)
            while (k + 0.5) * BAY - 66.0 < u_hi:
                uc = (k + 0.5) * BAY
                k += 1
                if uc - 66.0 < u_lo + 10.0 or uc + 66.0 > u_hi - 10.0:
                    continue
                wx, wy = at(uc)
                s = 1
                while STOREY * s + SILL + WIN_H <= top - TOP_CLEAR:
                    zb = STOREY * s + SILL
                    if zb >= UPPER_FROM:
                        _box(out, trim, wx, wy, zb - 9.0, 128.0, 12.0, 9.0, ux, uy, ox, oy)
                        _box(out, trim, wx, wy, zb + WIN_H, 132.0, 7.0, 22.0, ux, uy, ox, oy)
                    s += 1
            # awnings over some of the shop bays (the bays M_Facade picks for this building)
            if tall:
                k = math.floor(u_lo / pitch_s)
                while (k + 0.5) * pitch_s < u_hi:
                    uc = (k + 0.5) * pitch_s
                    k += 1
                    if uc - pitch_s * 0.5 < u_lo + 20.0 or uc + pitch_s * 0.5 > u_hi - 20.0:
                        continue
                    hk = h21(k + 0.5, mx * 0.01 + my * 0.013)
                    if hk >= AWNING_SHARE:
                        continue
                    sx, sy = at(uc)
                    width = pitch_s - 50.0
                    key = AWNINGS[int(h21(hk * 97.0, 3.3) * len(AWNINGS)) % len(AWNINGS)]
                    # a sloped canvas from just under the sign band, 120 cm out and 45 cm down, and its valance
                    _box(out, key, sx + ox * 2.0, sy + oy * 2.0, 240.0, width, 125.0, 4.0, ux, uy, ox, oy, pitch=AWNING_TILT)
                    _box(out, key, sx + ox * 104.0, sy + oy * 104.0, 172.0, width, 2.0, 22.0, ux, uy, ox, oy)
            if not door_done and length >= 600.0:
                door_done = True
                dx, dy = at(math.floor((u_lo + u_hi) * 0.5 / BAY) * BAY + BAY * 0.5 if tall else (u_lo + u_hi) * 0.5)
                _box(out, "DoorWood", dx, dy, SIDEWALK_TOP, 110.0, 3.0, 230.0, ux, uy, ox, oy)
                for side in (-1.0, 1.0):
                    _box(out, trim, dx + ux * side * 67.0, dy + uy * side * 67.0, SIDEWALK_TOP, 22.0, 9.0, 250.0,
                         ux, uy, ox, oy)
                _box(out, trim, dx, dy, SIDEWALK_TOP + 250.0, 170.0, 13.0, 24.0, ux, uy, ox, oy)
                _box(out, trim, dx, dy, SIDEWALK_TOP, 170.0, 60.0, 15.0, ux, uy, ox, oy)
                _box(out, trim, dx, dy, SIDEWALK_TOP + 15.0, 150.0, 30.0, 15.0, ux, uy, ox, oy)
    return out


def _segment_cross(p, q, r, s):
    """Intersection point of segments pq and rs, or None."""
    d = (q[0] - p[0]) * (s[1] - r[1]) - (q[1] - p[1]) * (s[0] - r[0])
    if abs(d) < 1e-6:
        return None
    t = ((r[0] - p[0]) * (s[1] - r[1]) - (r[1] - p[1]) * (s[0] - r[0])) / d
    w = ((r[0] - p[0]) * (q[1] - p[1]) - (r[1] - p[1]) * (q[0] - p[0])) / d
    if 0.0 <= t <= 1.0 and 0.0 <= w <= 1.0:
        return p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t
    return None


def crosswalks(district, start):
    """{PAINT: [box]}: ladder crosswalks across each road at the corners within HERO_RADIUS."""
    roads = sorted(road_paths(district), key=lambda rp: rp[0]["id"])
    segs = [(rec["id"], p, q, rec["width_m"] * 50.0) for rec, paths in roads for path in paths for p, q in zip(path, path[1:])]
    out = {}
    seen = set()
    for i, (ida, p, q, ha) in enumerate(segs):
        for idb, r, s, hb in segs[i + 1:]:
            if ida == idb:
                continue
            x = _segment_cross(p, q, r, s)
            if x is None or math.hypot(x[0] - start[0], x[1] - start[1]) > HERO_RADIUS:
                continue
            for (pa, qa, half, other) in ((p, q, ha, hb), (r, s, hb, ha)):
                L = math.hypot(qa[0] - pa[0], qa[1] - pa[1]) or 1.0
                ux, uy = (qa[0] - pa[0]) / L, (qa[1] - pa[1]) / L
                for sgn in (-1.0, 1.0):
                    cx, cy = x[0] + ux * sgn * (other + SIDEWALK_WIDTH * 0.5), x[1] + uy * sgn * (other + SIDEWALK_WIDTH * 0.5)
                    key = (round(cx / 50.0), round(cy / 50.0))
                    if key in seen:
                        continue
                    seen.add(key)
                    n = int((2.0 * half - 100.0) // 120.0)
                    for j in range(n):
                        off = (j - (n - 1) * 0.5) * 120.0
                        bx, by = cx - uy * off, cy + ux * off
                        out.setdefault(PAINT, []).append((round(bx, 2), round(by, 2), ROAD_TOP, 300.0, 60.0, 0.8,
                                                          round(math.degrees(math.atan2(uy, ux)), 3), 0.0))
    return out


def tree_spots(district, start, avoid):
    """[(x, y, yaw, scale)] on the sidewalks of the roads near the start, TREE_KERB_IN from the kerb, clear of
    ``avoid`` [(x, y)] by TREE_CLEAR and of every carriageway."""
    segs = _segments(district)
    out = []
    for rec, paths in sorted(road_paths(district), key=lambda rp: rp[0]["id"]):
        half = rec["width_m"] * 50.0
        for path in paths:
            for p, q in zip(path, path[1:]):
                L = math.hypot(q[0] - p[0], q[1] - p[1])
                if L < 1.0:
                    continue
                ux, uy = (q[0] - p[0]) / L, (q[1] - p[1]) / L
                d = TREE_SPACING * 0.5
                while d < L:
                    for side in (-1.0, 1.0):
                        x = p[0] + ux * d - uy * side * (half + TREE_KERB_IN)
                        y = p[1] + uy * d + ux * side * (half + TREE_KERB_IN)
                        if math.hypot(x - start[0], y - start[1]) > HERO_RADIUS:
                            continue
                        if any(math.hypot(x - ax, y - ay) < TREE_CLEAR for ax, ay in avoid):
                            continue
                        if any(_seg_dist((x, y), a, b)[0] < h + 60.0 for a, b, h in segs):
                            continue
                        hv = h21(round(x / 10.0), round(y / 10.0))
                        out.append((round(x, 1), round(y, 1), round(hv * 360.0, 1), round(0.85 + 0.3 * hv, 3)))
                    d += TREE_SPACING
    return out


PARK_TREE_SPACING = 1700.0     # cm; a jittered grid over the park near the start
PARK_TREE_EDGE = 500.0         # cm in from the park's edge
PARK_RADIUS = 16000.0


def park_tree_spots(district, start, avoid):
    """[(x, y, yaw, scale)] for the park's elms, bigger than the street trees, within PARK_RADIUS of the start."""
    out = []
    for park in district.parks:
        ring = geo.clean_ring(district.ring_cm(park["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3:
            continue
        x0, y0, x1, y1 = geo.bounds(ring)
        gx = x0 + PARK_TREE_SPACING * 0.5
        while gx < x1:
            gy = y0 + PARK_TREE_SPACING * 0.5
            while gy < y1:
                hx, hy = h21(round(gx), round(gy) + 0.5), h21(round(gy), round(gx) + 1.5)
                x = gx + (hx - 0.5) * PARK_TREE_SPACING * 0.6
                y = gy + (hy - 0.5) * PARK_TREE_SPACING * 0.6
                gy += PARK_TREE_SPACING
                if math.hypot(x - start[0], y - start[1]) > PARK_RADIUS or not geo.point_in_polygon((x, y), ring):
                    continue
                if min(_seg_dist((x, y), ring[i], ring[(i + 1) % len(ring)])[0] for i in range(len(ring))) < PARK_TREE_EDGE:
                    continue
                if any(math.hypot(x - ax, y - ay) < TREE_CLEAR * 2.0 for ax, ay in avoid):
                    continue
                out.append((round(x, 1), round(y, 1), round(hx * 360.0, 1), round(1.25 + 0.4 * hy, 3)))
            gx += PARK_TREE_SPACING
    return out


def news_boxes(trees):
    """Two newspaper boxes beside the first trees: [(key, box)]."""
    out = {}
    for i, (x, y, yaw, _s) in enumerate(trees[:2]):
        r = math.radians(yaw)
        out.setdefault("NewsBlue" if i == 0 else "NewsRed", []).append(
            (round(x + math.cos(r) * 120.0, 2), round(y + math.sin(r) * 120.0, 2), SIDEWALK_TOP, 50.0, 45.0, 100.0,
             round(yaw, 3), 0.0))
    return out


def tree_pits(trees):
    out = {}
    for x, y, yaw, _s in trees:
        out.setdefault("TreePit", []).append((x, y, SIDEWALK_TOP - 1.0, 130.0, 130.0, 2.0, 0.0, 0.0))
        for k in range(4):
            a = math.radians(k * 90.0)
            out["TreePit"].append((round(x + math.cos(a) * 62.0, 2), round(y + math.sin(a) * 62.0, 2), SIDEWALK_TOP,
                                   130.0, 6.0, 6.0, round(k * 90.0 + 90.0, 3), 0.0))
    return out


def merge(*dicts):
    out = {}
    for d in dicts:
        for key, boxes in d.items():
            out.setdefault(key, []).extend(boxes)
    return out
