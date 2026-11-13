"""Pure geometry helpers for the generated city. No ``import unreal`` anywhere in here.

Used by ``parse_osm.py`` (plain Python, run by Tools/fetch-osm.ps1) and by
``generate_city.py`` / ``verify_city.py`` (inside the editor). Tested by ``test_geo.py``,
which the engine's bundled Python runs without the editor: ``Tools/test-geo.ps1``.

Coordinate frames
-----------------
* WGS84 lat/lon in degrees, straight out of OpenStreetMap.
* Local tangent plane in metres around an origin (lat0, lon0)::

      x_m = (lon - lon0) * 111320 * cos(lat0)      metres east
      y_m = (lat - lat0) * 110540                  metres north

  Accurate to well under a metre over a few kilometres, which is all a district needs.
* Unreal centimetres: ``X = east``, ``Y = south``, ``Z = up``. Unreal is left-handed, so
  flipping north to -Y is what keeps the map *un*-mirrored: in the editor's top view +X is
  screen-right and +Y is screen-down, i.e. east is right and north is up, like a paper map.
  A pawn looking north has yaw -90 and has east on its right hand.
"""

import hashlib
import json
import math
import re

# An equirectangular projection about the origin, with the usual WGS84 approximations for metres per degree
# (latitude constant, longitude scaled by cos(lat0)): good to well under a metre across one district.
M_PER_DEG_LAT = 110540.0
M_PER_DEG_LON_EQUATOR = 111320.0

# --------------------------------------------------------------------------------------
# projection
# --------------------------------------------------------------------------------------


def project(lat, lon, lat0, lon0):
    """(lat, lon) degrees -> (x_m east, y_m north) about the origin (lat0, lon0)."""
    x = (lon - lon0) * M_PER_DEG_LON_EQUATOR * math.cos(math.radians(lat0))
    y = (lat - lat0) * M_PER_DEG_LAT
    return x, y


def unproject(x_m, y_m, lat0, lon0):
    """Inverse of project()."""
    lon = lon0 + x_m / (M_PER_DEG_LON_EQUATOR * math.cos(math.radians(lat0)))
    lat = lat0 + y_m / M_PER_DEG_LAT
    return lat, lon


def to_unreal_cm(x_m, y_m):
    """Local metres (east, north) -> Unreal centimetres (X east, Y south)."""
    return x_m * 100.0, -y_m * 100.0


def from_unreal_cm(x_cm, y_cm):
    return x_cm / 100.0, -y_cm / 100.0


def latlon_to_unreal_cm(lat, lon, lat0, lon0):
    return to_unreal_cm(*project(lat, lon, lat0, lon0))


# --------------------------------------------------------------------------------------
# polygons (lists of (x, y) tuples, not closed: the first point is not repeated)
# --------------------------------------------------------------------------------------


def signed_area(pts):
    """Shoelace area. Positive when the ring runs counter-clockwise in a y-up frame."""
    area = 0.0
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        area += x1 * y2 - x2 * y1
    return area * 0.5


def is_ccw(pts):
    return signed_area(pts) > 0.0


def oriented(pts, ccw=True):
    """The ring with the requested winding (in the frame the points are given in)."""
    pts = list(pts)
    if is_ccw(pts) != ccw:
        pts.reverse()
    return pts


def centroid(pts):
    """Area centroid of a simple polygon; the vertex mean for a degenerate one."""
    a = signed_area(pts)
    n = len(pts)
    if abs(a) < 1e-9:
        return (sum(p[0] for p in pts) / n, sum(p[1] for p in pts) / n)
    cx = cy = 0.0
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        cross = x1 * y2 - x2 * y1
        cx += (x1 + x2) * cross
        cy += (y1 + y2) * cross
    return cx / (6.0 * a), cy / (6.0 * a)


def point_in_polygon(pt, pts):
    """Even-odd rule. Points exactly on an edge may go either way."""
    x, y = pt
    inside = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y):
            x_cross = xi + (y - yi) * (xj - xi) / (yj - yi)
            if x < x_cross:
                inside = not inside
        j = i
    return inside


def _cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def clean_ring(pts, min_edge=0.05, collinear_tol=0.01):
    """Drop the repeated closing point, near-duplicate vertices and collinear vertices.

    ``min_edge`` and ``collinear_tol`` are in the ring's units (metres or cm; callers pass
    the right scale). Returns [] when fewer than 3 vertices survive.
    """
    pts = [tuple(p) for p in pts]
    if len(pts) > 1 and math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1]) < 1e-9:
        pts = pts[:-1]

    out = []
    for p in pts:
        if out and math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) < min_edge:
            continue
        out.append(p)
    if len(out) > 1 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < min_edge:
        out.pop()

    changed = True
    while changed and len(out) >= 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            base = math.hypot(c[0] - a[0], c[1] - a[1])
            if base < 1e-12:
                out.pop(i)
                changed = True
                break
            # distance of b from the line a-c
            if abs(_cross(a, b, c)) / base < collinear_tol:
                out.pop(i)
                changed = True
                break
    return out if len(out) >= 3 else []


def bounds(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


# --------------------------------------------------------------------------------------
# lines and a rotated rectangle (the district outline, aligned with the street grid)
# --------------------------------------------------------------------------------------


def fit_line(pts):
    """Total-least-squares line through points: returns (point_on_line, unit_direction)."""
    n = float(len(pts))
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts)
    syy = sum((p[1] - my) ** 2 for p in pts)
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pts)
    theta = 0.5 * math.atan2(2.0 * sxy, sxx - syy)
    return (mx, my), (math.cos(theta), math.sin(theta))


def intersect_lines(p1, d1, p2, d2):
    """Intersection of two infinite lines given as point + direction. None if parallel."""
    denom = d1[0] * d2[1] - d1[1] * d2[0]
    if abs(denom) < 1e-12:
        return None
    t = ((p2[0] - p1[0]) * d2[1] - (p2[1] - p1[1]) * d2[0]) / denom
    return p1[0] + t * d1[0], p1[1] + t * d1[1]


class Frame(object):
    """A 2D frame with origin ``o`` and unit axes ``u`` and ``v`` (v = u rotated +90 deg)."""

    def __init__(self, origin, u_axis):
        length = math.hypot(u_axis[0], u_axis[1])
        self.o = tuple(origin)
        self.u = (u_axis[0] / length, u_axis[1] / length)
        self.v = (-self.u[1], self.u[0])

    def to_local(self, p):
        dx, dy = p[0] - self.o[0], p[1] - self.o[1]
        return dx * self.u[0] + dy * self.u[1], dx * self.v[0] + dy * self.v[1]

    def to_world(self, q):
        return (self.o[0] + q[0] * self.u[0] + q[1] * self.v[0],
                self.o[1] + q[0] * self.u[1] + q[1] * self.v[1])


def clip_segment_to_box(a, b, box):
    """Liang-Barsky. ``box`` = (umin, vmin, umax, vmax). Returns (a', b') or None."""
    umin, vmin, umax, vmax = box
    x0, y0 = a
    dx, dy = b[0] - x0, b[1] - y0
    t0, t1 = 0.0, 1.0
    for p, q in ((-dx, x0 - umin), (dx, umax - x0), (-dy, y0 - vmin), (dy, vmax - y0)):
        if abs(p) < 1e-15:
            if q < 0:
                return None
            continue
        r = q / p
        if p < 0:
            if r > t1:
                return None
            t0 = max(t0, r)
        else:
            if r < t0:
                return None
            t1 = min(t1, r)
    return (x0 + t0 * dx, y0 + t0 * dy), (x0 + t1 * dx, y0 + t1 * dy)


def clip_polyline_to_box(pts, box):
    """Clip an open polyline to an axis-aligned box. Returns a list of polylines."""
    pieces = []
    current = []
    for i in range(len(pts) - 1):
        seg = clip_segment_to_box(pts[i], pts[i + 1], box)
        if seg is None:
            if len(current) >= 2:
                pieces.append(current)
            current = []
            continue
        a, b = seg
        if current and math.hypot(current[-1][0] - a[0], current[-1][1] - a[1]) < 1e-6:
            current.append(b)
        else:
            if len(current) >= 2:
                pieces.append(current)
            current = [a, b]
        # the segment left the box: close this piece
        if math.hypot(b[0] - pts[i + 1][0], b[1] - pts[i + 1][1]) > 1e-6:
            pieces.append(current)
            current = []
    if len(current) >= 2:
        pieces.append(current)
    return pieces


def polyline_length(pts):
    return sum(math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
               for i in range(len(pts) - 1))


# --------------------------------------------------------------------------------------
# OSM tag interpretation
# --------------------------------------------------------------------------------------

LEVEL_HEIGHT_M = 3.2
PARAPET_ALLOWANCE_M = 1.5
DEFAULT_HEIGHT_M = 15.0
SMALL_HEIGHT_M = 5.0
SMALL_BUILDING_TYPES = frozenset(
    ("garage", "garages", "shed", "carport", "hut", "kiosk", "cabin", "toilets", "roof"))

_NUMBER = re.compile(r"[-+]?\d+(?:\.\d+)?")


def parse_length_m(value):
    """OSM length string -> metres, or None. Handles '15', '15 m', '15.5m', "50'", "50 ft",
    and "12'6\"" (feet and inches)."""
    if value is None:
        return None
    s = str(value).strip().lower().replace(",", ".")
    if not s:
        return None
    feet_inches = re.match(r"^\s*(\d+(?:\.\d+)?)\s*'\s*(?:(\d+(?:\.\d+)?)\s*\")?\s*$", s)
    if feet_inches:
        feet = float(feet_inches.group(1))
        inches = float(feet_inches.group(2) or 0.0)
        return (feet * 12.0 + inches) * 0.0254
    m = _NUMBER.search(s)
    if not m:
        return None
    number = float(m.group(0))
    rest = s[m.end():].strip()
    if rest.startswith("ft") or rest.startswith("feet") or rest.startswith("foot"):
        return number * 0.3048
    if rest.startswith("cm"):
        return number / 100.0
    return number


def building_height_m(tags):
    """(height_m, source) for a building's tags.

    source is 'height', 'levels', 'default' or 'small'. Levels are 3.2 m each plus 1.5 m for
    the parapet, which is how a Manhattan walk-up measures to the top of its cornice.
    """
    h = parse_length_m(tags.get("height"))
    if h is not None and h > 0.5:
        return h, "height"
    levels = parse_length_m(tags.get("building:levels"))
    if levels is not None and levels > 0:
        return levels * LEVEL_HEIGHT_M + PARAPET_ALLOWANCE_M, "levels"
    kind = (tags.get("building") or "yes").strip().lower()
    if kind in SMALL_BUILDING_TYPES:
        return SMALL_HEIGHT_M, "small"
    return DEFAULT_HEIGHT_M, "default"


def median(values):
    s = sorted(values)
    n = len(s)
    if n == 0:
        return None
    mid = n // 2
    return s[mid] if n % 2 else 0.5 * (s[mid - 1] + s[mid])


# --------------------------------------------------------------------------------------
# change detection
# --------------------------------------------------------------------------------------


def record_hash(*parts):
    """Short stable hash of JSON-able parts. Floats are rounded to 1 mm first so a
    re-download that only jitters the last decimal does not regenerate every mesh."""

    def norm(v):
        if isinstance(v, float):
            return round(v, 3)
        if isinstance(v, (list, tuple)):
            return [norm(x) for x in v]
        if isinstance(v, dict):
            return {str(k): norm(v[k]) for k in sorted(v)}
        return v

    blob = json.dumps(norm(list(parts)), sort_keys=True, separators=(",", ":"))
    return hashlib.sha1(blob.encode("utf-8")).hexdigest()[:16]
