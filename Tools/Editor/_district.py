"""The district's records and the street layout every placement pass shares: where the OSM data
is, District (the records projected into Unreal centimetres), the shared dimensions (parapet,
slab tops, sidewalk width), road_paths, ground_ring_cm, facade_style and the street lamps'
spots. No ``unreal``; generate_city re-exports every name.
"""

import io
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402
from _geomutil import closest_point_on_polyline, ring_distance, stable_hash  # noqa: E402


DISTRICT = "east_village"

# Bump when the mesh recipe changes, so every mesh regenerates once.
GENERATOR_VERSION = 1

PARAPET_HEIGHT = 90.0      # cm
PARAPET_THICK = 30.0       # cm
PARAPET_MIN_HEIGHT_M = 6.0  # no parapets on sheds and garages
PARAPET_MITER_LIMIT = 4.0  # times the thickness; a sharper concave corner's inner edge is squared off

ROAD_TOP = 2.0             # cm above the ground
SIDEWALK_TOP = 15.0
PARK_TOP = 5.0
SLAB_THICK = 20.0          # every flat surface is a slab this thick, so kerbs have faces
GROUND_THICK = 50.0
SIDEWALK_WIDTH = 400.0


def data_dir():
    return os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "osm")


def data_paths():
    base = os.path.join(data_dir(), DISTRICT)
    return base + ".buildings.json", base + ".streets.json"


def data_available():
    return all(os.path.exists(p) for p in data_paths())


def load_json(path):
    with io.open(path, "r", encoding="utf-8") as f:
        return json.load(f)


class District(object):
    """The parsed records, projected into Unreal centimetres about the district origin."""

    def __init__(self):
        buildings_path, streets_path = data_paths()
        b = load_json(buildings_path)
        s = load_json(streets_path)
        self.meta = b["_meta"]
        self.lat0 = self.meta["origin"]["lat"]
        self.lon0 = self.meta["origin"]["lon"]
        self.buildings = b["buildings"]
        self.roads = s["roads"]
        self.parks = s["parks"]

    def cm(self, latlon):
        return geo.latlon_to_unreal_cm(latlon[0], latlon[1], self.lat0, self.lon0)

    def ring_cm(self, ring):
        return [self.cm(p) for p in ring]


# Fire escapes go on buildings in this band (_fire_escapes); facade_style calls them tenements.
FIRE_ESCAPE_MIN_HEIGHT_M = 10.0
FIRE_ESCAPE_MAX_HEIGHT_M = 30.0

PAINTED_BUILDINGS = ("garage", "garages", "shed", "roof", "warehouse", "parking", "service")
STONE_BUILDINGS = ("church", "civic", "school", "synagogue", "religious", "public", "university")
# Tenements (10 to 30 m) pick a brick or brownstone by a stable hash of their id, in these shares.
TENEMENT_STYLES = (("BrickRed", 0.32), ("BrickBrown", 0.26), ("BrickPurple", 0.2), ("Brownstone", 0.22))


def facade_style(rec):
    """MI_Facade_<style> for a building record: painted grey for sheds and garages (and anything
    under 6 m), stone for churches, civic buildings and anything over 30 m, brownstone from 6 to
    10 m, and a brick or brownstone by hash for the 10 to 30 m tenements."""
    kind = (rec.get("tags") or {}).get("building", "")
    h = rec["height_m"]
    if kind in PAINTED_BUILDINGS or h < PARAPET_MIN_HEIGHT_M:
        return "Painted"
    if kind in STONE_BUILDINGS or h > FIRE_ESCAPE_MAX_HEIGHT_M:
        return "Stone"
    if h < FIRE_ESCAPE_MIN_HEIGHT_M:
        return "Brownstone"
    pick = stable_hash(rec["id"], "facade")
    for style, share in TENEMENT_STYLES:
        if pick < share:
            return style
        pick -= share
    return TENEMENT_STYLES[-1][0]


def ground_ring_cm(district):
    return district.ring_cm(district.meta["ground_corners_latlon"])


def road_paths(district):
    """[(record, [path_cm, ...])] for every road record."""
    out = []
    for rec in district.roads:
        paths = [district.ring_cm(piece) for piece in rec["pieces"] if len(piece) >= 2]
        if paths:
            out.append((rec, paths))
    return out


# Street lamps: where they stand (the lights themselves are generate_city's). Fire escapes and the
# grapple reach model keep clear of the poles and heads.
LAMP_SPACING = 3000.0               # cm along each road, both sidewalks
LAMP_MIN_GAP = 800.0                # no two lamps closer than this (corners where roads meet)
LAMP_KERB_INSET = 60.0              # cm from the kerb into the sidewalk
LAMP_POLE_HEIGHT = 700.0
LAMP_POLE_DIAMETER = 14.0
LAMP_ARM = 90.0                     # cm the head reaches out over the road
LAMP_SHADOW_DISTANCE = 3500.0       # lamps this close to the park cast shadows; the rest don't


def lamp_spots(district):
    """[(x, y, yaw toward the road, casts shadows)] in a stable order."""
    roads = sorted(road_paths(district), key=lambda rp: rp[0]["id"])
    carriageway = [(p, q, rec["width_m"] * 50.0) for rec, paths in roads
                   for path in paths for p, q in zip(path, path[1:])]
    rings = []
    for rec in district.buildings:
        ring = district.ring_cm(rec["outer"])
        if len(ring) >= 3:
            rings.append((geo.bounds(ring), ring))
    ground = ground_ring_cm(district)
    parks = [district.ring_cm(p["outer"]) for p in district.parks]

    def in_carriageway(pt):
        return any(closest_point_on_polyline(pt, [p, q])[0] < half + 30.0 for p, q, half in carriageway)

    def in_building(pt):
        for (x0, y0, x1, y1), ring in rings:
            if x0 - 50 <= pt[0] <= x1 + 50 and y0 - 50 <= pt[1] <= y1 + 50 and ring_distance(pt, ring) < 50.0:
                return True
        return False

    spots = []
    for rec, paths in roads:
        offset = rec["width_m"] * 50.0 + LAMP_KERB_INSET
        for path in paths:
            s_next = LAMP_SPACING * 0.5
            walked = 0.0
            for (ax, ay), (bx, by) in zip(path, path[1:]):
                L = math.hypot(bx - ax, by - ay)
                if L < 1.0:
                    continue
                ux, uy = (bx - ax) / L, (by - ay) / L
                nx, ny = -uy, ux
                while s_next <= walked + L:
                    t = s_next - walked
                    px, py = ax + ux * t, ay + uy * t
                    for side in (1.0, -1.0):
                        pt = (px + nx * offset * side, py + ny * offset * side)
                        if not geo.point_in_polygon(pt, ground) or in_carriageway(pt) or in_building(pt):
                            continue
                        if any(math.hypot(pt[0] - s[0], pt[1] - s[1]) < LAMP_MIN_GAP for s in spots):
                            continue
                        yaw = math.degrees(math.atan2(-ny * side, -nx * side))
                        shadows = any(ring_distance(pt, park) <= LAMP_SHADOW_DISTANCE for park in parks)
                        spots.append((pt[0], pt[1], yaw, shadows))
                    s_next += LAMP_SPACING
                walked += L
    return spots
