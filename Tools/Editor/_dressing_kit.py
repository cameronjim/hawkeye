"""December dressing, the kit half: what the engine draws for each record of a dressing plan (_dressing.py).

``instance_groups(doc)`` turns a plan document (``_dressing.plan_json``'s, read back with ``load_plan``) into
instance groups, one per mesh and material: ``{group: [instance]}`` where an instance is
``(x, y, z, pitch, yaw, roll, sx, sy, sz)`` in world cm and degrees, the scale over the mesh's 100 cm unit.
generate_city.py writes each group into DA_EastVillage_CityProps as one FCityClutterGroup, so ACityLedgeSpawner
draws the whole block as one hierarchical instanced mesh per group: about 25 draws for the 966 records and the
bulbs on them, not one actor per record.

``GROUPS`` names each group's mesh, material, collision and shadow; ``light_spots`` the few real lights (a string
light or a tree stand that should throw colour on the street); ``shot_cameras`` the reference cameras the
Hawkeye.Screenshot.Dressing pass looks through. ``MESH_UNITS`` documents the generated meshes' unit shapes.

Pure: no ``unreal``. Every instance comes from the records alone, so the counts verify_city and verify_content
check are this module's, computed from the committed plan without the district.
"""

import io
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _geomutil import stable_hash  # noqa: E402

PLAN_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Data", "dressing")
GROUP_PREFIX = "Dress_"

# The meshes the groups draw. Engine basic shapes are 100 cm and centred; the generated ones are built to the
# same unit so a record's scale goes straight onto them.
MESH_UNITS = {
    "cube": "/Engine/BasicShapes/Cube",
    "cylinder": "/Engine/BasicShapes/Cylinder",
    "sphere": "/Engine/BasicShapes/Sphere",
    "bulb": "SM_Dress_Bulb: a low sphere 100 cm across, centred (a few hundred bulbs, not the engine's dense one)",
    "wreath": "SM_Dress_Wreath: a ring in the XY plane round +Z, 100 cm across and 100 cm deep, centred, its "
              "tube flattened by the record's 10 cm Z scale",
    "tree": "SM_Dress_Tree: three stacked cones on a stub trunk filling a 100 cm box, centred",
    "roof_snow": "SM_Dress_RoofSnow_<block>: every roof_snow outline extruded by its thickness, in world cm",
}

# group: (mesh, material key, collision, casts shadow). Materials: the Glow_ keys are emissive (M_DressGlow), the
# rest flat colours under M_Prop's snow layer (MI_Prop_Dress<key>); generate_city's dressing step has the colours.
BULB_COLOURS = ("Red", "Warm", "Green", "Blue")
AWNING_COLOURS = ("purple", "cream", "grey", "black")
GROUPS = {
    "Dress_Wire": ("cube", "Wire", False, False),
    "Dress_Bulb_Red": ("bulb", "Glow_Red", False, False),
    "Dress_Bulb_Warm": ("bulb", "Glow_Warm", False, False),
    "Dress_Bulb_Green": ("bulb", "Glow_Green", False, False),
    "Dress_Bulb_Blue": ("bulb", "Glow_Blue", False, False),
    "Dress_Wreath": ("wreath", "Evergreen", False, False),
    "Dress_WreathBow": ("cube", "BowRed", False, False),
    "Dress_Window_Dim": ("cube", "Glow_WindowDim", False, False),
    "Dress_Window_Warm": ("cube", "Glow_WindowWarm", False, False),
    "Dress_Window_Bright": ("cube", "Glow_WindowBright", False, False),
    "Dress_Candle": ("cylinder", "CandleWax", False, False),
    "Dress_CandleFlame": ("bulb", "Glow_Flame", False, False),
    "Dress_RoofSnow": ("roof_snow", "Snow", False, False),
    "Dress_ParapetSnow": ("cube", "Snow", False, False),
    "Dress_SnowBank": ("sphere", "Snow", True, False),
    "Dress_SnowBankDirty": ("sphere", "SnowDirty", True, False),
    "Dress_TreeStand": ("cube", "Lumber", True, True),
    "Dress_Tree": ("tree", "Evergreen", True, True),
    "Dress_TreeNetted": ("tree", "TreeNetted", True, True),
    "Dress_SteamStack": ("cylinder", "ConEdOrange", True, False),
    "Dress_SteamStripe": ("cylinder", "ConEdWhite", False, False),
    "Dress_CarSnow": ("sphere", "Snow", False, False),
    "Dress_Awning_purple": ("cube", "AwningPurple", False, False),
    "Dress_Awning_cream": ("cube", "AwningCream", False, False),
    "Dress_Awning_grey": ("cube", "AwningGrey", False, False),
    "Dress_Awning_black": ("cube", "AwningBlack", False, False),
    "Dress_Sign": ("cube", "SignBlank", False, False),
}

# --- the kit's own dimensions -----------------------------------------------------------------------------------------
BULB_SIZE = 9.0                 # cm across a bulb: bigger than a real C9 so a string reads from across the street
BULB_DROP = 6.0                 # cm the bulb hangs under the wire
BULB_END_CLEAR = 20.0           # cm from either end of a string to the first bulb
RAIL_BULB_SPACING = 25.0        # cm between the bulbs along the tree stand's rail
BOW_SIZE = (6.0, 18.0, 13.0)    # out, along, up
BOW_DOWN = 24.0                 # cm under the wreath's middle
BOW_OUT = 4.0                   # cm in front of the wreath's middle
WREATH_BULBS = 10               # warm bulbs round each wreath's face
WREATH_BULB_SIZE = 5.0
WREATH_BULB_RING = 0.75         # of the wreath's radius: on the ring's tube (SM_Dress_Wreath: 37.5 of 50)
FLAME_SIZE = (4.0, 4.0, 7.0)
FLAME_UP = 3.5                  # cm over the candle's top to the flame's middle
WINDOW_DIM_BELOW = 0.73         # a window's warmth under this is dim, under WINDOW_WARM_BELOW warm, else bright
WINDOW_WARM_BELOW = 0.87
DIRTY_FROM = 0.6                # a snow bank this dirty or more is grey
STRIPE_HEIGHT = 16.0            # cm of each white band round a steam stack
STRIPE_AT = (0.18, 0.38)        # of the stack's height over its middle
STRIPE_GROW = 1.06              # the band's radius over the stack's

# --- the real lights ------------------------------------------------------------------------------------------------
LIGHT_STRINGS = 3               # string lights that get a light under their lowest point
LIGHT_UNDER = 40.0              # cm under the lowest point
LIGHT_LUMENS = 450.0
LIGHT_RADIUS = 900.0
LIGHT_COLOUR = (1.0, 0.72, 0.45)
TREE_LIGHT_OUT = 90.0           # cm out in front of the tree stand's rail (toward the kerb side it faces)
TREE_LIGHT_LUMENS = 350.0
TREE_LIGHT_RADIUS = 600.0

EYE = 170.0                     # cm over the ground for the street-level cameras
ROOF_PITCH = -14.0              # the rooftop camera's look down
OVERVIEW_UP = 2500.0            # cm up for the overview camera


def plan_path(block):
    return os.path.join(PLAN_DIR, block + ".json")


def load_plan(path):
    with io.open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _r(v, nd=2):
    v = round(float(v), nd)
    return 0.0 if v == 0.0 else v


def _inst(pos, pitch=0.0, yaw=0.0, roll=0.0, scale=(1.0, 1.0, 1.0)):
    return (_r(pos[0]), _r(pos[1]), _r(pos[2]), _r(pitch, 3), _r(yaw, 3), _r(roll, 3),
            _r(scale[0], 4), _r(scale[1], 4), _r(scale[2], 4))


def _of(rec, scale=None):
    return _inst(rec["position"], rec["pitch"], rec["yaw"], rec["roll"], scale or rec["scale"])


def _out(yaw):
    return math.cos(math.radians(yaw)), math.sin(math.radians(yaw))


def _length(a, b):
    return math.sqrt(sum((b[k] - a[k]) ** 2 for k in range(3)))


def along_polyline(points, spacing, end_clear):
    """Points every ``spacing`` cm along a 3D polyline, centred on it, ``end_clear`` from either end."""
    lengths = [_length(a, b) for a, b in zip(points, points[1:])]
    total = sum(lengths)
    usable = total - 2.0 * end_clear
    if usable <= 0.0:
        return []
    count = int(usable // spacing) + 1
    start = end_clear + (usable - (count - 1) * spacing) * 0.5
    out = []
    seg, seg_start = 0, 0.0
    for k in range(count):
        d = start + k * spacing
        while seg < len(lengths) - 1 and d > seg_start + lengths[seg]:
            seg_start += lengths[seg]
            seg += 1
        t = 0.0 if lengths[seg] <= 0.0 else (d - seg_start) / lengths[seg]
        a, b = points[seg], points[seg + 1]
        out.append(tuple(a[i] + (b[i] - a[i]) * t for i in range(3)))
    return out


def _bar(a, b, thickness):
    """A cube instance running from a to b, ``thickness`` cm square: local X along the bar."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    yaw = math.degrees(math.atan2(dy, dx))
    mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, (a[2] + b[2]) * 0.5)
    return _inst(mid, pitch, yaw, 0.0, (length / 100.0, thickness / 100.0, thickness / 100.0))


def _bulbs(groups, rid, points, spacing, end_clear, drop, size=BULB_SIZE):
    offset = int(stable_hash(rid, "palette") * len(BULB_COLOURS))
    s = size / 100.0
    for k, p in enumerate(along_polyline(points, spacing, end_clear)):
        colour = BULB_COLOURS[(k + offset) % len(BULB_COLOURS)]
        groups["Dress_Bulb_" + colour].append(_inst((p[0], p[1], p[2] - drop), scale=(s, s, s)))


def _window_group(warmth):
    if warmth < WINDOW_DIM_BELOW:
        return "Dress_Window_Dim"
    if warmth < WINDOW_WARM_BELOW:
        return "Dress_Window_Warm"
    return "Dress_Window_Bright"


def _expand(groups, rec):
    kind, p = rec["kind"], rec["params"]
    x, y, z = rec["position"]
    if kind == "string_light":
        pts = [tuple(q) for q in p["points"]]
        for a, b in zip(pts, pts[1:]):
            groups["Dress_Wire"].append(_bar(a, b, p.get("radius", 2.0) * 2.0))
        # The record's envelope (_dressing.string_envelope) already covers the bulbs' drop and size.
        _bulbs(groups, rec["id"], pts, p.get("bulb_spacing", 30.0), BULB_END_CLEAR, p.get("bulb_drop", BULB_DROP),
               p.get("bulb_size", BULB_SIZE))
    elif kind == "wreath":
        groups["Dress_Wreath"].append(_of(rec))
        ox, oy = _out(rec["yaw"])
        groups["Dress_WreathBow"].append(_inst((x + ox * BOW_OUT, y + oy * BOW_OUT, z - BOW_DOWN), 0.0, rec["yaw"], 0.0,
                                               [v / 100.0 for v in BOW_SIZE]))
        # A ring of warm bulbs on its face: a dark green ring on a dark wall does not read at night, a lit one does.
        ring = rec["scale"][0] * 50.0 * WREATH_BULB_RING
        ax, ay = -oy, ox
        out = rec["scale"][2] * 50.0 - WREATH_BULB_SIZE * 0.3
        s = WREATH_BULB_SIZE / 100.0
        for k in range(WREATH_BULBS):
            a = 2.0 * math.pi * (k + 0.5) / WREATH_BULBS
            groups["Dress_Bulb_Warm"].append(_inst((x + ox * out + ax * ring * math.cos(a), y + oy * out + ay * ring * math.cos(a),
                                                    z + ring * math.sin(a)), scale=(s, s, s)))
    elif kind == "window_light":
        groups[_window_group(p.get("warmth", 0.8))].append(_of(rec))
    elif kind == "window_candle":
        groups["Dress_Candle"].append(_of(rec))
        top = z + rec["scale"][2] * 50.0
        groups["Dress_CandleFlame"].append(_inst((x, y, top + FLAME_UP), 0.0, rec["yaw"], 0.0,
                                                 [v / 100.0 for v in FLAME_SIZE]))
    elif kind == "roof_snow":
        pass   # one mesh for the whole block's roofs, below
    elif kind == "parapet_snow":
        groups["Dress_ParapetSnow"].append(_of(rec))
    elif kind == "snow_bank":
        groups["Dress_SnowBankDirty" if p.get("dirty", 0.0) >= DIRTY_FROM else "Dress_SnowBank"].append(_of(rec))
    elif kind == "tree_stand":
        groups["Dress_TreeStand"].append(_of(rec))
        if p.get("lights"):
            half = rec["scale"][0] * 50.0
            ux, uy = _out(rec["yaw"])
            top = z + rec["scale"][2] * 50.0
            ends = [(x - ux * half, y - uy * half, top), (x + ux * half, y + uy * half, top)]
            # Half sunk into the rail's top: 4.5 cm over the record's box, inside every margin the plan tests with.
            _bulbs(groups, rec["id"], ends, RAIL_BULB_SPACING, 10.0, 0.0)
    elif kind == "tree_for_sale":
        groups["Dress_TreeNetted" if p.get("netted") else "Dress_Tree"].append(_of(rec))
    elif kind == "steam_vent":
        groups["Dress_SteamStack"].append(_of(rec))
        sx, sy, sz = rec["scale"]
        for at in STRIPE_AT:
            groups["Dress_SteamStripe"].append(_inst((x, y, z + sz * 100.0 * at), 0.0, 0.0, 0.0,
                                                     (sx * STRIPE_GROW, sy * STRIPE_GROW, STRIPE_HEIGHT / 100.0)))
    elif kind == "car_snow":
        groups["Dress_CarSnow"].append(_of(rec))
    elif kind == "awning":
        colour = p.get("colour") if p.get("colour") in AWNING_COLOURS else AWNING_COLOURS[0]
        groups["Dress_Awning_" + colour].append(_of(rec))
    elif kind == "shop_sign":
        groups["Dress_Sign"].append(_of(rec))
    else:
        raise ValueError("no kit for dressing kind " + str(kind))


def roof_snow_outlines(doc):
    """[(points [[x, y, z]], thickness)] of every roof_snow record, in plan order: SM_Dress_RoofSnow_<block>."""
    return [(rec["params"]["points"], rec["params"]["thickness"]) for rec in doc["records"] if rec["kind"] == "roof_snow"]


def roof_snow_mesh_name(doc):
    return "SM_Dress_RoofSnow_" + doc["block"]


def instance_groups(doc):
    """{group: [instance]} for every non-empty group of the plan document, in GROUPS order. The roof snow is one
    instance of the block's own mesh at the origin (its vertices are world cm)."""
    groups = {key: [] for key in GROUPS}
    for rec in doc["records"]:
        _expand(groups, rec)
    if roof_snow_outlines(doc):
        groups["Dress_RoofSnow"].append(_inst((0.0, 0.0, 0.0)))
    return {key: groups[key] for key in GROUPS if groups[key]}


def group_counts(doc):
    return {key: len(v) for key, v in instance_groups(doc).items()}


def light_spots(doc):
    """[(name, (x, y, z), rgb, lumens, radius)]: a warm point light under the lowest point of LIGHT_STRINGS string
    lights spread along the block's strings (by id), and one in front of the tree stand's rail. No shadows."""
    out = []
    strings = [r for r in doc["records"] if r["kind"] == "string_light"]
    if strings:
        n = len(strings)
        picks = sorted({int(round(i * (n - 1) / float(max(1, LIGHT_STRINGS - 1)))) for i in range(min(n, LIGHT_STRINGS))})
        for k in picks:
            rec = strings[k]
            low = min(rec["params"]["points"], key=lambda q: q[2])
            out.append(("String{0}".format(k), (_r(low[0]), _r(low[1]), _r(low[2] - LIGHT_UNDER)), LIGHT_COLOUR,
                        LIGHT_LUMENS, LIGHT_RADIUS))
    rails = [r for r in doc["records"] if r["kind"] == "tree_stand" and r["params"].get("lights")]
    if rails:
        rec = rails[0]
        trees = [r for r in doc["records"] if r["kind"] == "tree_for_sale"]
        x, y, z = rec["position"]
        if trees:
            # Out toward the side the trees lean from, so the light falls on them and the pavement.
            tx = sum(t["position"][0] for t in trees) / len(trees)
            ty = sum(t["position"][1] for t in trees) / len(trees)
            d = math.hypot(tx - x, ty - y) or 1.0
            x, y = x + (tx - x) / d * TREE_LIGHT_OUT, y + (ty - y) / d * TREE_LIGHT_OUT
        out.append(("TreeStand", (_r(x), _r(y), _r(z + 30.0)), LIGHT_COLOUR, TREE_LIGHT_LUMENS, TREE_LIGHT_RADIUS))
    return out


def _face_of(rec):
    """The street face id inside a string light's id: <block>/string_light/<face>/s<k>."""
    return rec["id"].split("/")[2]


def _look(eye, target):
    dx, dy, dz = target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]
    return (_r(math.degrees(math.atan2(dz, math.hypot(dx, dy))), 2), _r(math.degrees(math.atan2(dy, dx)), 2))


def shot_cameras(doc):
    """[(name, (x, y, z), pitch, yaw)] for the reference screenshots, picked from the records:

    * ``street_lights``: eye height in the street with the most strings, 12 m short of its first string, looking
      down the street under them.
    * ``street_back``: the same street from its other end.
    * ``facade_wreaths``: across the street from the wreath with the most dressing round it, looking at its wall.
    * ``tree_stand``: 7 m from the tree stand, three quarters on.
    * ``rooftop``: on the roof of the block's building nearest the strings' street (under 30 m), at its edge, looking
      down the street.
    * ``overview``: 25 m up, 30 m short of the strings' street, looking up it.
    """
    out = []
    strings = [r for r in doc["records"] if r["kind"] == "string_light"]
    by_face = {}
    for rec in strings:
        by_face.setdefault(_face_of(rec), []).append(rec)
    street_mid = None
    if by_face:
        face = sorted(by_face.items(), key=lambda kv: (-len(kv[1]), kv[0]))[0][1]
        # Each string's middle in the street, and the street's direction from the first to the last.
        mids = [((r["params"]["points"][0][0] + r["params"]["points"][-1][0]) * 0.5,
                 (r["params"]["points"][0][1] + r["params"]["points"][-1][1]) * 0.5) for r in face]
        ax = face[0]["params"]["points"]
        across = (ax[-1][0] - ax[0][0], ax[-1][1] - ax[0][1])
        L = math.hypot(*across) or 1.0
        along = (-across[1] / L, across[0] / L)
        s = [m[0] * along[0] + m[1] * along[1] for m in mids]
        lo, hi = mids[s.index(min(s))], mids[s.index(max(s))]
        street_mid = ((lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5)
        height = face[0]["params"]["points"][0][2]
        for name, start, sign in (("street_lights", lo, -1.0), ("street_back", hi, 1.0)):
            eye = (start[0] + along[0] * 1200.0 * sign, start[1] + along[1] * 1200.0 * sign, EYE)
            target = (street_mid[0], street_mid[1], height - 150.0)
            pitch, yaw = _look(eye, target)
            out.append((name, (_r(eye[0]), _r(eye[1]), EYE), pitch, yaw))
    wreaths = [r for r in doc["records"] if r["kind"] == "wreath"]
    if wreaths:
        dressed = [r for r in doc["records"] if r["kind"] in ("window_light", "snow_bank", "string_light", "awning")]

        def busy(w):
            return (-sum(1 for r in dressed if math.hypot(r["position"][0] - w["position"][0],
                                                          r["position"][1] - w["position"][1]) < 1500.0), w["id"])
        w = sorted(wreaths, key=busy)[0]
        ox, oy = _out(w["yaw"])
        side = (-oy, ox)
        x, y, _z = w["position"]
        eye = (x + ox * 1100.0 + side[0] * 500.0, y + oy * 1100.0 + side[1] * 500.0, EYE)
        pitch, yaw = _look(eye, (x, y, 450.0))
        out.append(("facade_wreaths", (_r(eye[0]), _r(eye[1]), EYE), pitch, yaw))
    rails = [r for r in doc["records"] if r["kind"] == "tree_stand" and r["params"].get("lights")]
    trees = [r for r in doc["records"] if r["kind"] == "tree_for_sale"]
    if rails and trees:
        rail = rails[0]
        tx = sum(t["position"][0] for t in trees) / len(trees)
        ty = sum(t["position"][1] for t in trees) / len(trees)
        rx, ry, rz = rail["position"]
        d = math.hypot(tx - rx, ty - ry) or 1.0
        out_dir = ((tx - rx) / d, (ty - ry) / d)
        ux, uy = _out(rail["yaw"])
        # The trees lean on the rail from the wall side, so the street is the other way.
        eye = (rx - out_dir[0] * 550.0 + ux * 400.0, ry - out_dir[1] * 550.0 + uy * 400.0, EYE)
        pitch, yaw = _look(eye, (tx, ty, 120.0))
        out.append(("tree_stand", (_r(eye[0]), _r(eye[1]), EYE), pitch, yaw))
    roofs = [r for r in doc["records"] if r["kind"] == "roof_snow" and r["position"][2] < 3000.0]
    if roofs and street_mid is not None:
        def near_street(r):
            return (min(math.hypot(q[0] - street_mid[0], q[1] - street_mid[1]) for q in r["params"]["points"]), r["id"])
        roof = sorted(roofs, key=near_street)[0]
        edge = min(roof["params"]["points"], key=lambda q: math.hypot(q[0] - street_mid[0], q[1] - street_mid[1]))
        cx, cy, cz = roof["position"]
        d = math.hypot(edge[0] - cx, edge[1] - cy) or 1.0
        # 1.5 m in from the roof's corner nearest the street, toward the roof's middle.
        eye = (edge[0] + (cx - edge[0]) / d * 150.0, edge[1] + (cy - edge[1]) / d * 150.0, cz + EYE)
        _pitch, yaw = _look(eye, (street_mid[0], street_mid[1], 300.0))
        # Over the parapet and down the street, not down at the roof under the lens.
        out.append(("rooftop", (_r(eye[0]), _r(eye[1]), _r(eye[2])), ROOF_PITCH, yaw))
    if street_mid is not None:
        # Up the strings' street from 30 m short of its first string, 25 m up.
        eye = (lo[0] - along[0] * 3000.0, lo[1] - along[1] * 3000.0, OVERVIEW_UP)
        pitch, yaw = _look(eye, (street_mid[0], street_mid[1], 0.0))
        out.append(("overview", (_r(eye[0]), _r(eye[1]), OVERVIEW_UP), pitch, yaw))
    return out
