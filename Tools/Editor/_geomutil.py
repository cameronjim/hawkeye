"""Plain 2D geometry the city generator places things with: distances to segments, outlines and
rings, segment crossings, the oriented bounding rectangle, and stable_hash. No ``unreal``, so it
runs (and is tested) in plain CPython; generate_city re-exports every name.

The ring distances differ on purpose; pick by what the caller needs inside the outline:
ring_distance is 0 inside, _edge_distance is the unsigned distance to the outline, _edge_clearance
is that distance signed (positive inside, negative outside).
"""

import math
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402


# Not merged: returns (distance, closest point) over a whole polyline; the others return a distance.
def closest_point_on_polyline(p, path):
    best = None
    for i in range(len(path) - 1):
        ax, ay = path[i]
        bx, by = path[i + 1]
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L2))
        q = (ax + t * dx, ay + t * dy)
        d = math.hypot(q[0] - p[0], q[1] - p[1])
        if best is None or d < best[0]:
            best = (d, q)
    return best


# One segment, distance only; also takes 3D a and b (it reads [0] and [1]), which the polyline form does not.
def _segment_distance(p, a, b):
    ax, ay, bx, by = a[0], a[1], b[0], b[1]
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 <= 0.0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / length2))
    return math.hypot(p[0] - (ax + t * dx), p[1] - (ay + t * dy))


# _near_segment(pt, a, b) was closest_point_on_polyline(pt, [a, b])[0]: the same clamp and the
# same hypot as _segment_distance (the difference vector negated, which hypot ignores), so the
# same float for every input it accepted.
_near_segment = _segment_distance


# Differs from _edge_distance: 0 anywhere inside the ring.
def ring_distance(pt, ring):
    """0 inside the ring, else the distance to its nearest edge."""
    if geo.point_in_polygon(pt, ring):
        return 0.0
    return closest_point_on_polyline(pt, list(ring) + [ring[0]])[0]


# _point_quad_distance(pt, quad) was ring_distance line for line.
_point_quad_distance = ring_distance


# Differs from ring_distance: the distance to the outline even inside it (unsigned).
def _edge_distance(pt, ring):
    return closest_point_on_polyline(pt, list(ring) + [ring[0]])[0]


# Differs from _edge_distance only in sign: negative outside the ring.
def _edge_clearance(pt, ring):
    """Distance inside the ring to its nearest edge; negative outside."""
    d = closest_point_on_polyline(pt, list(ring) + [ring[0]])[0]
    return d if geo.point_in_polygon(pt, ring) else -d


def _segments_cross(a, b, p, q):
    def orient(o, s, t):
        return (s[0] - o[0]) * (t[1] - o[1]) - (s[1] - o[1]) * (t[0] - o[0])
    d1, d2 = orient(p, q, a), orient(p, q, b)
    d3, d4 = orient(a, b, p), orient(a, b, q)
    return (d1 > 0) != (d2 > 0) and (d3 > 0) != (d4 > 0)


def _segment_params(a, b, ring):
    """Parameters (0..1) along a->b where it crosses the ring's edges, plus 0 and 1 if an end is inside."""
    ts = []
    if geo.point_in_polygon(a, ring):
        ts.append(0.0)
    if geo.point_in_polygon(b, ring):
        ts.append(1.0)
    dx, dy = b[0] - a[0], b[1] - a[1]
    n = len(ring)
    for i in range(n):
        p, q = ring[i], ring[(i + 1) % n]
        ex, ey = q[0] - p[0], q[1] - p[1]
        den = dx * ey - dy * ex
        if abs(den) < 1e-9:
            continue
        t = ((p[0] - a[0]) * ey - (p[1] - a[1]) * ex) / den
        u = ((p[0] - a[0]) * dy - (p[1] - a[1]) * dx) / den
        if 0.0 <= t <= 1.0 and 0.0 <= u <= 1.0:
            ts.append(t)
    return ts


def oriented_rect(ring):
    """Smallest rectangle around the ring with a side along one of its edges:
    (centre x, centre y, half length, half width, yaw degrees)."""
    best = None
    n = len(ring)
    for i in range(n):
        ax, ay = ring[i]
        bx, by = ring[(i + 1) % n]
        L = math.hypot(bx - ax, by - ay)
        if L < 1.0:
            continue
        ux, uy = (bx - ax) / L, (by - ay) / L
        us = [x * ux + y * uy for x, y in ring]
        vs = [-x * uy + y * ux for x, y in ring]
        area = (max(us) - min(us)) * (max(vs) - min(vs))
        if best is None or area < best[0]:
            cu, cv = (max(us) + min(us)) * 0.5, (max(vs) + min(vs)) * 0.5
            best = (area, cu * ux - cv * uy, cu * uy + cv * ux,
                    (max(us) - min(us)) * 0.5, (max(vs) - min(vs)) * 0.5, math.degrees(math.atan2(uy, ux)))
    return best[1:] if best else None


def stable_hash(*parts):
    """0..1 from the parts' text, the same on every run and machine."""
    text = "|".join(str(p) for p in parts)
    return (zlib.crc32(text.encode("utf-8")) & 0xffffffff) / 4294967296.0
