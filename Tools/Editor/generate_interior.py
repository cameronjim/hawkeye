"""Build a chapter interior map, L_Int_<Name>, from a layout description (Tools/Interiors/<Name>.json).

The layout's rules and geometry are in _interior.py (pure, tested by Tools/test-interior.ps1); this
script turns them into actors, in the streets' stylised look:

* the shell: walls 20 cm thick (two 10 cm halves, each painted for the room it faces, a dark skirting
  on each inside face), floors and ceilings 330 cm apart (a 5 cm floor in the room's surface over a
  15 cm cream ceiling), the stair well's steps, landings and spine, the mezzanine's balustrade with a
  hidden traversable ledge on its outer face (so mantle works onto it) and grapple anchors on its top
* openings: doors 100 x 220 (ADoorActor, sliding into the wall, one locked to a keycard), archways,
  AInteriorExit doors back to the district, glazed windows with the night outside them
* outside: the district's moon, sky light, atmosphere, fog and post process, a star sphere, lit
  facades across the street on every side and a snowy pavement below
* light: per room, pendants (warm) or tube fittings (cool) under each ceiling, the fixture and its light
  both tagged CityLamp so the EMP arrow kills them; display-case glows, a stage wash, green exit signs
* props by tag from engine primitives (desk, table, chair, shelves, crates, pallet, display case, pedestal,
  sofa, bed, counter, stage, lectern, bench, rug, painting, plant); crates marked traversable are visible
  BP_TraversableBlocks so they can be climbed
* play: a PlayerStart inside the entrance, a nav volume, the layout's enemies (BP_Thug, BP_Archer or
  BP_Thug_Heavy by type, or the Blueprint an enemy names) with their patrol TargetPoints (tagged PatrolFacing where a point gives a yaw),
  alert groups and the keycards they carry (placed pickups in their CarriedPickups), loose pickups,
  BP_GameMode_Interior (no snow, the indoor camera arm)
* every door knows the nav link through it; a locked one keeps it out of the navmesh until it opens

Every actor is labelled Int_<Room>_<Kind>_<n> and tagged Interior; a rerun with the same layout changes
nothing and does not save the map. Actors a changed layout no longer wants are removed.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script="Tools\\Editor\\generate_interior.py"
    (HAWKEYE_INTERIOR=Sample picks one layout; the default builds every Tools/Interiors/*.json)
"""

import glob
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _interior as it  # noqa: E402
import _materials as m  # noqa: E402
import generate_city as gen  # noqa: E402

LAYOUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Interiors")
PLAYER_PATH = "/Game/Blueprints/Player"
GAME_MODE = "BP_GameMode_Interior"
CUBE = "/Engine/BasicShapes/Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder"
SPHERE = "/Engine/BasicShapes/Sphere"
CONE = "/Engine/BasicShapes/Cone"
TAG = "Interior"
LAMP_TAGS = ["CityLamp", "IntLight"]      # CityLamp: the EMP arrow's lamp tag (UArrowEffectsSubsystem::LampTag)
THUG_BP_PATH = "/Game/Blueprints/AI"
THUG_BP_NAME = "BP_Thug"
# The Blueprint each enemy type spawns as (the district's own: generate_city.py).
ENEMY_BLUEPRINTS = {"FISTS": (THUG_BP_PATH, THUG_BP_NAME), "BAT": (THUG_BP_PATH, THUG_BP_NAME),
                    "PISTOL": (THUG_BP_PATH, THUG_BP_NAME), "BOW": (gen.ARCHER_BP_PATH, gen.ARCHER_BP_NAME),
                    "SHIELD": (THUG_BP_PATH, gen.HEAVY_BP_NAME)}
PATROL_FACING_TAG = "PatrolFacing"        # AThugAIController turns to such a point's yaw while he waits there
THUG_HALF_HEIGHT = 96.0
PATROL_POINT_HEIGHT = 100.0

SKIRTING = (12.0, 2.0)                    # height, cm proud of the wall
FLOOR_TOP = 5.0                           # of the 20 cm slab: floor surface over the ceiling
TRIM = 8.0                                # door and window surround width
PENDANT_SPACING = 420.0
TUBE_SPACING = 380.0
PENDANT_DROP = 70.0                       # cm under a ceiling to the bulb; a double-height room hangs them lower
PENDANT_DROP_TALL = 160.0
PENDANT_LUMENS = 260.0
PENDANT_RADIUS = 1000.0
TUBE_LUMENS = 220.0
TUBE_RADIUS = 950.0
WARM = (1.0, 0.72, 0.45)
COOL = (0.82, 0.9, 1.0)
CASE_LUMENS = 45.0
STAGE_LUMENS = 700.0
BACKDROP_OUT = 1800.0                     # cm from the building to the facades across the street
BACKDROP_HEIGHT = 2600.0
STARS_RADIUS = 60000.0
LEDGE_OUTSET = 2.0
LEDGE_DROP = 130.0                        # the ledge covers the balustrade and the slab edge under it
ANCHOR_LANDING = (60.0, 100.0)            # the landing point: inboard of the rail, down to the floor
DOOR_SLIDE = 110.0


def _v(*xyz):
    return unreal.Vector(float(xyz[0]), float(xyz[1]), float(xyz[2]))


def _yaw_rot(yaw):
    return unreal.Rotator(0.0, 0.0, float(yaw))


def _local(cx, cy, yaw, dx, dy):
    """(x, y) of a point dx, dy in a frame at cx, cy turned yaw degrees."""
    r = math.radians(yaw)
    return cx + dx * math.cos(r) - dy * math.sin(r), cy + dx * math.sin(r) + dy * math.cos(r)


class Build(object):
    """Every actor the layout wants, spawned or corrected by label, counting changes and kinds."""

    def __init__(self, layout, existing, mats):
        self.layout, self.existing, self.mats = layout, existing, mats
        self.wanted = set()
        self.changes = 0
        self.counts = {}
        self.kinds = {}
        self.meshes = {name: c.load_or_none(path) for name, path in
                       (("cube", CUBE), ("cylinder", CYLINDER), ("sphere", SPHERE), ("cone", CONE))}

    # --- labels ----------------------------------------------------------------------------------

    def room_label(self, room_id):
        return self.layout.by_id[room_id].label if room_id in self.layout.by_id else "Outside"

    def label(self, room, kind):
        self.note_changes()
        key = (room, kind)
        n = self.counts.get(key, 0)
        self.counts[key] = n + 1
        self.kinds[kind] = self.kinds.get(kind, 0) + 1
        label = "Int_{0}_{1}_{2}".format(room, kind, n)
        self.wanted.add(label)
        self._last = (label, self.changes)
        return label

    def note_changes(self):
        """Logs the previous label if building it changed anything: a rerun should log nothing."""
        last = getattr(self, "_last", None)
        if last is not None and self.changes > last[1]:
            c.log("updated", last[0], "{0} change(s)".format(self.changes - last[1]))
        self._last = None

    def mat(self, key):
        if ":" in key:
            group, name = key.split(":", 1)
            return self.mats[group].get(name)
        return self.mats.get(key)

    # --- mesh actors -----------------------------------------------------------------------------

    def box(self, room, kind, centre, size, material, tags=None, collide=True, yaw=0.0, shape="cube", shadow=True):
        """A StaticMeshActor of an engine shape (100 cm, pivot at its centre) scaled to size. shadow False for a
        lamp's own fitting, which would otherwise throw its shape over the ceiling."""
        label = self.label(room, kind)
        mesh = self.meshes[shape]
        scale = _v(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0)
        mat = self.mat(material) if isinstance(material, str) else material
        self.changes += gen._ensure_mesh_actor(self.existing, label, mesh, mat, _v(*centre), _yaw_rot(yaw), scale,
                                               [TAG] + list(tags or []))
        actor = self.existing.get(label)
        if actor is not None:
            comp = actor.get_editor_property("static_mesh_component")
            want = "BlockAll" if collide else "NoCollision"
            if str(comp.get_collision_profile_name()) != want:
                comp.set_collision_profile_name(want)
                self.changes += 1
            self.changes += gen.set_if_different(comp, "cast_shadow", bool(shadow), label)
        return actor

    def slab(self, room, kind, x0, y0, z0, x1, y1, z1, material, tags=None, collide=True):
        return self.box(room, kind, ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5),
                        (x1 - x0, y1 - y0, z1 - z0), material, tags, collide)

    # --- other actors ----------------------------------------------------------------------------

    def located(self, room, kind, cls, loc, yaw=0.0, tags=None):
        label = self.label(room, kind)
        actor, n = gen._ensure_located(self.existing, label, cls, _v(*loc), yaw)
        self.changes += n
        if actor is not None:
            self.changes += gen._ensure_tags(actor, [TAG] + list(tags or []))
        return actor

    def prop(self, actor, name, value, context, tol=1e-3):
        if actor is None:
            return
        if isinstance(value, float):
            self.changes += gen.set_if_different(actor, name, value, context, tol)
            return
        try:
            current = actor.get_editor_property(name)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(context, name), exc)
            return
        same = str(current) == str(value) if isinstance(value, (unreal.Name, unreal.Text, str)) else current == value
        if not same:
            actor.set_editor_property(name, value)
            self.changes += 1

    def light(self, room, kind, loc, lumens, radius, rgb, shadows=False, source_length=0.0, spot=False, tags=None):
        cls = unreal.SpotLight if spot else unreal.PointLight
        rot = unreal.Rotator(0.0, -90.0, 0.0) if spot else unreal.Rotator(0.0, 0.0, 0.0)
        label = self.label(room, kind)
        actor = self.existing.get(label)
        if actor is not None and not isinstance(actor, cls):
            actor.destroy_actor()
            actor = None
        if actor is None:
            actor = c.spawn_actor(cls, _v(*loc), rot, label=label)
            if actor is None:
                c.log("FAILED", label, "spawn_actor returned None")
                return None
            self.existing[label] = actor
            self.changes += 1
        if not gen.same_vector(actor.get_actor_location(), _v(*loc), 0.5):
            actor.set_actor_location(_v(*loc), False, True)
            self.changes += 1
        if abs(actor.get_actor_rotation().pitch - rot.pitch) > 0.05:
            actor.set_actor_rotation(rot, False)
            self.changes += 1
        comp = actor.get_editor_property("spot_light_component" if spot else "point_light_component")
        for prop, value, tol in (("mobility", unreal.ComponentMobility.MOVABLE, 0), ("intensity_units", unreal.LightUnits.LUMENS, 0),
                                 ("intensity", float(lumens), 0.5), ("attenuation_radius", float(radius), 0.5),
                                 ("cast_shadows", bool(shadows), 0), ("use_temperature", False, 0),
                                 ("source_length", float(source_length), 0.1), ("source_radius", 4.0, 0.1)):
            self.changes += gen.set_if_different(comp, prop, value, label, tol or 1e-3)
        if spot:
            self.changes += gen.set_if_different(comp, "outer_cone_angle", 50.0, label, 0.01)
            self.changes += gen.set_if_different(comp, "inner_cone_angle", 25.0, label, 0.01)
        want = unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255)
        have = comp.get_editor_property("light_color")
        if (have.r, have.g, have.b) != (want.r, want.g, want.b):
            comp.set_editor_property("light_color", want)
            self.changes += 1
        self.changes += gen._ensure_tags(actor, [TAG] + list(tags or []))
        return actor


# --------------------------------------------------------------------------------------
# the shell
# --------------------------------------------------------------------------------------

def paint_of(layout, room_id):
    return "plaster:" + (layout.by_id[room_id].paint if room_id in layout.by_id else "Outside")


def build_walls(b, openings):
    layout = b.layout
    for bd in layout.all_boundaries():
        if bd.kind == "rail":
            build_rail(b, bd)
            continue
        cuts = openings.get(id(bd), (bd, []))[1]
        pieces = it.wall_pieces(bd, cuts)
        for side, owner, other in ((-1.0, bd.a, bd.b), (1.0, bd.b, bd.a)):
            room = b.room_label(owner if owner is not None else other)
            kind = "Wall" if owner is not None else "WallOut"
            floor_here = owner is not None and layout.by_id[owner].floor == bd.band
            for s0, s1, z0, z1 in pieces:
                # A piece that reaches the end of its wall runs on under the wall it meets, so corners close.
                e0 = s0 - (it.WALL * 0.5 if abs(s0 - bd.lo) < 0.01 else 0.0)
                e1 = s1 + (it.WALL * 0.5 if abs(s1 - bd.hi) < 0.01 else 0.0)
                off = side * it.WALL * 0.25
                if bd.axis == "x":
                    centre = (bd.coord + off, (e0 + e1) * 0.5, (z0 + z1) * 0.5)
                    size = (it.WALL * 0.5, e1 - e0, z1 - z0)
                else:
                    centre = ((e0 + e1) * 0.5, bd.coord + off, (z0 + z1) * 0.5)
                    size = (e1 - e0, it.WALL * 0.5, z1 - z0)
                b.box(room, kind, centre, size, paint_of(layout, owner))
                if owner is not None and floor_here and abs(z0 - bd.band * it.FLOOR_HEIGHT) < 0.01:
                    build_skirting(b, room, bd, side, s0, s1, z0)


def build_skirting(b, room, bd, side, s0, s1, z0):
    h, proud = SKIRTING
    off = side * (it.WALL * 0.5 + proud * 0.5)
    if bd.axis == "x":
        centre, size = (bd.coord + off, (s0 + s1) * 0.5, z0 + h * 0.5), (proud, s1 - s0, h)
    else:
        centre, size = ((s0 + s1) * 0.5, bd.coord + off, z0 + h * 0.5), (s1 - s0, proud, h)
    b.box(room, "Skirting", centre, size, "wood:Dark", collide=False)


def surface_material(surface):
    return {"wood": "wood_floor", "carpet": "carpet", "concrete": "concrete"}[surface]


def surface_tags(surface):
    return {"wood": ["SurfaceWood"], "carpet": ["SurfaceCarpet"], "concrete": []}[surface]


def build_slabs(b):
    layout = b.layout
    for level, x0, y0, x1, y1, above, below in layout.slabs():
        top = level * it.FLOOR_HEIGHT
        if above is not None:
            room = layout.by_id[above]
            label = b.room_label(above)
            b.slab(label, "Floor", x0, y0, top - FLOOR_TOP, x1, y1, top, surface_material(room.surface),
                   surface_tags(room.surface) + ["room:" + above])
            under = "plaster:Cream" if below is not None else "concrete"
            b.slab(label, "FloorBase", x0, y0, top - it.SLAB, x1, y1, top - FLOOR_TOP, under)
        else:
            label = b.room_label(below)
            b.slab(label, "Ceiling", x0, y0, top - it.SLAB, x1, y1, top - FLOOR_TOP, "plaster:Cream")
            b.slab(label, "Roof", x0, y0, top - FLOOR_TOP, x1, y1, top, "concrete")


def build_stairs(b):
    for stair in b.layout.stairs:
        parts = it.stair_parts(b.layout, stair)
        label = b.room_label(stair["room"])
        for step in parts["steps"]:
            x0, y0, z0, x1, y1, z1 = step
            # Solid to the floor under a 4 cm tread: the tread is what she walks on and what the footsteps hear.
            b.slab(label, "Step", x0, y0, z0, x1, y1, z1 - 4.0, "plaster:Stone", ["IntStair"])
            b.slab(label, "Tread", x0, y0, z1 - 4.0, x1, y1, z1, "wood_floor", ["SurfaceWood", "IntStair"])
        x0, y0, z0, x1, y1, z1 = parts["mid_landing"]
        b.slab(label, "Landing", x0, y0, z0, x1, y1, z1 - 4.0, "plaster:Stone", ["IntStair"])
        b.slab(label, "LandingTread", x0, y0, z1 - 4.0, x1, y1, z1, "wood_floor", ["SurfaceWood", "IntStair"])
        x0, y0, z0, x1, y1, z1 = parts["top_landing"]
        b.slab(label, "Landing", x0, y0, z0, x1, y1, z1 - FLOOR_TOP, "plaster:Cream", ["IntStair"])
        b.slab(label, "LandingTread", x0, y0, z1 - FLOOR_TOP, x1, y1, z1, "wood_floor", ["SurfaceWood", "IntStair"])
        b.slab(label, "Spine", *parts["spine"], material="plaster:Stone", tags=["IntStair"])
        for rail in parts["rails"]:
            b.slab(label, "Rail", *rail, material="prop:Iron", tags=["IntStair"])
            x0, y0, z0, x1, y1, z1 = rail
            b.slab(label, "RailCap", x0, y0 - 2.0, z1, x1, y1 + 2.0, z1 + 5.0, "prop:Brass")


def build_rail(b, bd):
    """The balustrade on a mezzanine's open edge: a dark panel wall RAIL_HEIGHT high with a brass cap, a
    hidden traversable ledge on the outer face so mantle and ledge grab find it."""
    layout = b.layout
    mezz = bd.a if layout.by_id[bd.a].overlooks == bd.b else bd.b
    host = bd.b if mezz == bd.a else bd.a
    label = b.room_label(mezz)
    z0 = bd.band * it.FLOOR_HEIGHT
    top = z0 + it.RAIL_HEIGHT
    half = it.RAIL_THICK * 0.5
    if bd.axis == "x":
        b.slab(label, "Rail", bd.coord - half, bd.lo, z0, bd.coord + half, bd.hi, top, "wood:Dark")
        b.slab(label, "RailCap", bd.coord - half - 2.0, bd.lo, top, bd.coord + half + 2.0, bd.hi, top + 5.0, "prop:Brass")
    else:
        b.slab(label, "Rail", bd.lo, bd.coord - half, z0, bd.hi, bd.coord + half, top, "wood:Dark")
        b.slab(label, "RailCap", bd.lo, bd.coord - half - 2.0, top, bd.hi, bd.coord + half + 2.0, top + 5.0, "prop:Brass")
    build_rail_ledge(b, bd, mezz, host, top + 5.0)


def rail_ledge_spot(bd, mezz, top):
    """(origin, yaw, scale, corner a, corner b) of the ledge on a rail's outer face: the generate_city
    convention (local X along the edge, local Y into the mezzanine, origin its bottom outer corner)."""
    nx, ny = bd.normal_towards(mezz)                 # into the mezzanine
    face = bd.coord - (nx if bd.axis == "x" else ny) * (it.RAIL_THICK * 0.5 + 2.0)
    ux, uy = ny, -nx                                 # local X; turning it +90 gives the inward normal
    if bd.axis == "x":
        a, bb = (face, bd.lo), (face, bd.hi)
    else:
        a, bb = (bd.lo, face), (bd.hi, face)
    if a[0] * ux + a[1] * uy > bb[0] * ux + bb[1] * uy:
        a, bb = bb, a
    ox, oy = a[0] - nx * LEDGE_OUTSET, a[1] - ny * LEDGE_OUTSET
    depth = it.RAIL_THICK + 4.0 + LEDGE_OUTSET
    yaw = math.degrees(math.atan2(uy, ux))
    return ((ox, oy, top - LEDGE_DROP), yaw, (bd.length / 100.0, depth / 100.0, LEDGE_DROP / 100.0),
            (ox, oy, top), (bb[0] - nx * LEDGE_OUTSET, bb[1] - ny * LEDGE_OUTSET, top))


def build_rail_ledge(b, bd, mezz, host, top):
    cls = gen.traversable_class()
    if cls is None:
        c.log("FAILED", "rail ledge", "BP_TraversableBlock missing")
        return
    origin, yaw, scale, _a, _b = rail_ledge_spot(bd, mezz, top)
    label = b.label(b.room_label(mezz), "Ledge")
    b.changes += gen._ensure_block(b.existing, label, cls, origin, yaw, scale, [TAG, "IntLedge"], False, True)



# --------------------------------------------------------------------------------------
# openings: doors, archways, exits, windows, signs
# --------------------------------------------------------------------------------------

def _wall_frame(bd, at, band_z):
    """(x, y) of the opening's centre on the wall line and the unit normal from side a to side b."""
    x, y = bd.point(at)
    return x, y, (1.0, 0.0) if bd.axis == "x" else (0.0, 1.0)


def build_trim(b, room, bd, at, width, z0, height, faces):
    """A dark surround on each face in faces (-1 side a, +1 side b): two jambs and a head."""
    for side in faces:
        off = side * (it.WALL * 0.5 + 1.5)
        for s in (at - width * 0.5 - TRIM * 0.5, at + width * 0.5 + TRIM * 0.5):
            if bd.axis == "x":
                centre, size = (bd.coord + off, s, z0 + (height + TRIM) * 0.5), (3.0, TRIM, height + TRIM)
            else:
                centre, size = (s, bd.coord + off, z0 + (height + TRIM) * 0.5), (TRIM, 3.0, height + TRIM)
            b.box(room, "Trim", centre, size, "wood:Dark", collide=False)
        if bd.axis == "x":
            centre, size = (bd.coord + off, at, z0 + height + TRIM * 0.5), (3.0, width + 2 * TRIM, TRIM)
        else:
            centre, size = (at, bd.coord + off, z0 + height + TRIM * 0.5), (width + 2 * TRIM, 3.0, TRIM)
        b.box(room, "Trim", centre, size, "wood:Dark", collide=False)


def _slide_sign(bd, at, cuts):
    """+1 or -1: the way along its wall the leaf can slide, into solid wall clear of any other opening."""
    for sign in (1.0, -1.0):
        lo = at - it.DOOR_WIDTH * 0.5 + sign * DOOR_SLIDE
        hi = at + it.DOOR_WIDTH * 0.5 + sign * DOOR_SLIDE
        if lo < bd.lo or hi > bd.hi:
            continue
        if any(c_[0] < hi and c_[1] > lo for c_ in cuts if abs((c_[0] + c_[1]) * 0.5 - at) > 1.0):
            continue
        return sign
    return 1.0



NAV_LINK_WIDTH = 140.0      # openings narrower than this get a nav link: the navmesh erodes a 100 cm door shut
NAV_LINK_REACH = 90.0       # cm each side of the wall line the link's ends stand


def build_nav_link(b, room, bd, at, z0):
    """A NavLinkProxy through a narrow opening, both ways, so paths and patrols plan through it. With a
    34 cm agent on 19 cm cells the navmesh leaves nothing of a 100 cm doorway. A door knows its link: a
    locked one keeps it Null (out of the navmesh) until it opens."""
    x, y = bd.point(at)
    nx, ny = (1.0, 0.0) if bd.axis == "x" else (0.0, 1.0)
    actor = b.located(room, "NavLink", unreal.NavLinkProxy, (x, y, z0), 0.0, ["IntNavLink"])
    if actor is None:
        return None
    left = _v(-nx * NAV_LINK_REACH, -ny * NAV_LINK_REACH, 10.0)
    right = _v(nx * NAV_LINK_REACH, ny * NAV_LINK_REACH, 10.0)
    b.prop(actor, "smart_link_is_relevant", False, room)
    link = unreal.NavigationLink()
    link.set_editor_property("left", left)
    link.set_editor_property("right", right)
    link.set_editor_property("direction", unreal.NavLinkDirection.BOTH_WAYS)
    current = actor.get_editor_property("point_links")
    same = len(current) == 1 and gen.same_vector(current[0].get_editor_property("left"), left, 0.5) \
        and gen.same_vector(current[0].get_editor_property("right"), right, 0.5) \
        and current[0].get_editor_property("direction") == unreal.NavLinkDirection.BOTH_WAYS
    if not same:
        actor.set_editor_property("point_links", [link])
        b.changes += 1
    return actor


def build_doors(b, openings):
    layout = b.layout
    door_cls = c.find_class("DoorActor", "/Script/Hawkeye.DoorActor")
    for door, bd, at, width in layout.door_spots():
        z0 = bd.band * it.FLOOR_HEIGHT
        room = b.room_label(bd.a)
        build_trim(b, room, bd, at, width, z0, it.DOOR_HEIGHT, (-1.0, 1.0))
        link = build_nav_link(b, room, bd, at, z0) if width < NAV_LINK_WIDTH else None

        leaf = door.get("leaf", "wood")
        if leaf == "none" or door_cls is None:
            if door_cls is None and leaf != "none":
                c.log("FAILED", "door " + door["id"], "ADoorActor not exposed; build the module")
            continue
        x, y, (nx, ny) = _wall_frame(bd, at, z0)
        yaw = it.yaw_of(nx, ny)                        # local X from side a into side b; local Y along the wall
        actor = b.located(room, "Door", door_cls, (x, y, z0), yaw, ["IntDoor", "door:" + door["id"]])
        if actor is None:
            continue
        cuts = openings.get(id(bd), (bd, []))[1]
        slide = _slide_sign(bd, at, cuts)
        # Local Y is world +Y for a wall along Y at yaw 0, world -X for a wall along X at yaw 90.
        along_sign = 1.0 if bd.axis == "x" else -1.0
        b.prop(actor, "motion", unreal.DoorMotion.SLIDE, room)
        b.prop(actor, "slide_distance", float(DOOR_SLIDE * slide * along_sign), room, 0.5)
        b.prop(actor, "open_seconds", 0.8, room, 0.01)
        b.prop(actor, "locked", bool(door.get("locked", False)), room)
        b.prop(actor, "opens_on_objective", unreal.Name(door.get("opens_on") or "None"), room)
        if link is not None and actor.get_editor_property("nav_link") != link:
            actor.set_editor_property("nav_link", link)
            b.changes += 1
        if door.get("keycard"):
            b.prop(actor, "required_keycard_id", unreal.Name(door["keycard"]), room)
        leaf_comp = actor.get_editor_property("door_mesh")
        cube = b.meshes["cube"]
        if leaf_comp.get_editor_property("static_mesh") != cube:
            leaf_comp.set_static_mesh(cube)
            b.changes += 1
        want = _v(0.05, 1.0, it.DOOR_HEIGHT / 100.0)
        if not gen.same_vector(leaf_comp.get_editor_property("relative_scale3d"), want, 1e-4):
            leaf_comp.set_editor_property("relative_scale3d", want)
            b.changes += 1
        material = b.mat("prop:Iron" if leaf == "steel" else "wood:Door")
        overrides = leaf_comp.get_editor_property("override_materials")
        if len(overrides) < 1 or overrides[0] != material:
            leaf_comp.set_material(0, material)
            b.changes += 1
        # The leaf slides away when opened; the navmesh runs under it so patrols and paths plan through doors.
        try:
            if leaf_comp.get_editor_property("can_ever_affect_navigation"):
                leaf_comp.set_editor_property("can_ever_affect_navigation", False)
                b.changes += 1
        except Exception as exc:  # noqa: BLE001
            c.log_error("door leaf navigation " + door["id"], exc)


def build_exits(b):
    layout = b.layout
    cls = c.find_class("InteriorExit", "/Script/Hawkeye.InteriorExit")
    district = c.load_or_none(layout.district)
    for item, bd, at, width in layout.exit_spots():
        room_id = item["room"]
        room = b.room_label(room_id)
        z0 = bd.band * it.FLOOR_HEIGHT
        nx, ny = bd.normal_towards(room_id)
        x, y = bd.point(at)
        build_trim(b, room, bd, at, width, z0, it.DOOR_HEIGHT, (1.0 if room_id == bd.b else -1.0,))
        if cls is None:
            c.log("FAILED", "exit " + item["id"], "AInteriorExit not exposed; build the module")
            continue
        actor = b.located(room, "Exit", cls, (x, y, z0), it.yaw_of(nx, ny), ["IntExit", "exit:" + item["id"]])
        if actor is None:
            continue
        b.prop(actor, "return_point_override", unreal.Name(item.get("return") or "None"), room)
        b.prop(actor, "fallback_return_point", unreal.Name(layout.doorstep), room)
        b.prop(actor, "exit_name", unreal.Text(item.get("name", "[Leave]")), room)
        if district is not None and gen_path(actor.get_editor_property("fallback_district")) != layout.district:
            actor.set_editor_property("fallback_district", district)
            b.changes += 1
        for part, mat in (("door", "prop:Iron"), ("door_frame", "prop:Black"), ("sign_board", "prop:Black")):
            b.changes += gen._ensure_part_material(actor, part, b.mat(mat), actor.get_actor_label())


def gen_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name()).split(".")[0]
    except Exception:  # noqa: BLE001
        text = str(value)
        return text.split(".")[0] if text != "None" else ""


def build_windows(b):
    for item, bd, at, width in b.layout.window_spots():
        room_id = item["room"]
        room = b.room_label(room_id)
        z0 = bd.band * it.FLOOR_HEIGHT + float(item.get("sill", 90.0))
        height = float(item.get("height", 150.0))
        inside = 1.0 if room_id == bd.b else -1.0
        if bd.axis == "x":
            b.box(room, "Glass", (bd.coord, at, z0 + height * 0.5), (2.0, width, height), "glass")
            b.box(room, "Sill", (bd.coord + inside * 4.0, at, z0 - 3.0), (it.WALL + 8.0, width + 2 * TRIM, 6.0),
                  "wood:Dark", collide=False)
        else:
            b.box(room, "Glass", (at, bd.coord, z0 + height * 0.5), (width, 2.0, height), "glass")
            b.box(room, "Sill", (at, bd.coord + inside * 4.0, z0 - 3.0), (width + 2 * TRIM, it.WALL + 8.0, 6.0),
                  "wood:Dark", collide=False)
        build_trim(b, room, bd, at, width, z0, height, (inside,))
        # A mullion: the window reads as a window, not a hole.
        if bd.axis == "x":
            b.box(room, "Mullion", (bd.coord, at, z0 + height * 0.5), (4.0, 5.0, height), "prop:Black", collide=False)
        else:
            b.box(room, "Mullion", (at, bd.coord, z0 + height * 0.5), (5.0, 4.0, height), "prop:Black", collide=False)


def build_exit_signs(b):
    layout = b.layout
    for sign in layout.exit_signs:
        room = layout.by_id[sign["room"]]
        axis, coord, _lo, _hi = room.side_line(sign["side"])
        inward = {"W": 1.0, "N": 1.0, "E": -1.0, "S": -1.0}[sign["side"]]
        z = int(sign.get("floor", room.floor)) * it.FLOOR_HEIGHT + it.DOOR_HEIGHT + TRIM + 16.0
        at = float(sign["at"])
        off = inward * (it.WALL * 0.5 + 3.0)
        if axis == "x":
            centre, size = (coord + off, at, z), (4.0, 36.0, 14.0)
        else:
            centre, size = (at, coord + off, z), (36.0, 4.0, 14.0)
        b.box(room.label, "ExitSign", centre, size, "lamp:ExitSign", collide=False)


# --------------------------------------------------------------------------------------
# light
# --------------------------------------------------------------------------------------

def ceiling_at(layout, room, x, y):
    """The underside of whatever is over (x, y) in room: its own ceiling, a mezzanine's floor, or a stair's
    top landing."""
    top = room.z1
    for other in layout.rooms:
        if other.overlooks == room.id and other.contains(x, y):
            top = min(top, other.z0 - it.SLAB)
    for stair in layout.stairs:
        if stair["room"] == room.id:
            x0, y0, z0, x1, y1, _z1 = it.stair_parts(layout, stair)["top_landing"]
            if x0 <= x <= x1 and y0 <= y <= y1:
                top = min(top, z0)
    return top


def fixture_spots(room, spacing):
    nx = max(1, int(round((room.x1 - room.x0) / spacing)))
    ny = max(1, int(round((room.y1 - room.y0) / spacing)))
    return [(room.x0 + (room.x1 - room.x0) * (i + 0.5) / nx, room.y0 + (room.y1 - room.y0) * (j + 0.5) / ny)
            for j in range(ny) for i in range(nx)]


def build_lights(b):
    layout = b.layout
    for room in sorted(layout.rooms, key=lambda r: r.id):
        rgb = WARM if room.tone == "warm" else COOL
        if room.light == "none":
            continue
        spacing = PENDANT_SPACING if room.light == "pendant" else TUBE_SPACING
        for x, y in fixture_spots(room, spacing):
            owner = layout.owner_at(room.floor + room.floors - 1, x, y)
            if room.floors > 1 and owner != room.id and not any(o.overlooks == room.id for o in layout.rooms if o.id == owner):
                continue
            top = ceiling_at(layout, room, x, y)
            low = top - room.z0 < 400.0
            if room.light == "pendant":
                build_pendant(b, room, x, y, top, rgb, tall=not low)
            else:
                build_tube(b, room, x, y, top, rgb)


def build_pendant(b, room, x, y, top, rgb, tall):
    drop = PENDANT_DROP_TALL if tall else PENDANT_DROP
    bulb = top - drop
    lamp_tags = LAMP_TAGS
    b.box(room.label, "PendantCord", (x, y, (top + bulb + 18.0) * 0.5), (1.5, 1.5, top - bulb - 18.0), "prop:Black",
          collide=False, shape="cylinder", shadow=False)
    b.box(room.label, "PendantShade", (x, y, bulb + 10.0), (46.0, 46.0, 26.0), "prop:Shade", lamp_tags, collide=False,
          shape="cone", shadow=False)
    b.box(room.label, "PendantBulb", (x, y, bulb - 2.0), (16.0, 16.0, 12.0), "lamp:PendantWarm", lamp_tags,
          collide=False, shape="sphere", shadow=False)
    lumens = PENDANT_LUMENS * (1.6 if tall else 1.0)
    b.light(room.label, "PendantLight", (x, y, bulb - 12.0), lumens, PENDANT_RADIUS * (1.4 if tall else 1.0), rgb,
            shadows=tall, tags=lamp_tags)


def build_tube(b, room, x, y, top, rgb):
    b.box(room.label, "TubeHousing", (x, y, top - 4.0), (130.0, 18.0, 8.0), "prop:Iron", LAMP_TAGS, collide=False,
          shadow=False)
    b.box(room.label, "Tube", (x, y, top - 9.0), (120.0, 7.0, 3.0), "lamp:TubeCool", LAMP_TAGS, collide=False, shadow=False)

    b.light(room.label, "TubeLight", (x, y, top - 16.0), TUBE_LUMENS, TUBE_RADIUS, rgb, shadows=False,
            source_length=110.0, tags=LAMP_TAGS)


# --------------------------------------------------------------------------------------
# outside: sky, post process, the street around
# --------------------------------------------------------------------------------------

def build_outside(b):
    """The district's night: moon, sky light, atmosphere, fog and PP_Global (one set of labels the city
    script also uses, so both maps grade the same), stars, facades across the street, snowy pavement."""
    labels = {name: b.label("Outside", kind) for name, kind in (
        (gen.MOON_LABEL, "Moon"), ("SkyLight", "SkyLight"), ("SkyAtmosphere", "Atmosphere"), ("HeightFog", "Fog"),
        ("PP_Global", "PostProcess"))}
    b.changes += gen.ensure_lighting_core(b.existing, labels=labels)
    for label in labels.values():
        if label in b.existing:
            b.changes += gen._ensure_tags(b.existing[label], [TAG, "IntOutside"])


    x0, y0, x1, y1 = b.layout.bounds()
    cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
    scale = STARS_RADIUS / 50.0
    b.box("Outside", "NightSky", (cx, cy, 0.0), (scale * 100.0,) * 3, b.mats["stars"], collide=False, shape="sphere",
          shadow=False)
    sky = b.existing.get("Int_Outside_NightSky_0")
    if sky is not None:
        comp = sky.get_editor_property("static_mesh_component")
        b.changes += gen.set_if_different(comp, "affect_distance_field_lighting", False, "Int_Outside_NightSky_0")

    styles = sorted(b.mats["facades"].keys())
    span_x, span_y = (x1 - x0) + 2 * BACKDROP_OUT + 3000.0, (y1 - y0) + 2 * BACKDROP_OUT + 3000.0
    fronts = (((cx, y0 - BACKDROP_OUT - 50.0), (span_x, 100.0)), ((cx, y1 + BACKDROP_OUT + 50.0), (span_x, 100.0)),
              ((x0 - BACKDROP_OUT - 50.0, cy), (100.0, span_y)), ((x1 + BACKDROP_OUT + 50.0, cy), (100.0, span_y)))
    for i, ((fx, fy), (sx, sy)) in enumerate(fronts):
        b.box("Outside", "Facade", (fx, fy, BACKDROP_HEIGHT * 0.5 - 30.0), (sx, sy, BACKDROP_HEIGHT),
              b.mats["facades"][styles[i % len(styles)]], collide=False)
    b.box("Outside", "Pavement", (cx, cy, -35.0), (span_x, span_y, 10.0), b.mats["sidewalk"], collide=False)


# --------------------------------------------------------------------------------------
# props
# --------------------------------------------------------------------------------------

# Each prop is parts in its own frame (x forward along its yaw, z up from the floor): (part kind, dx, dy,
# z bottom, sx, sy, sz, material, shape, collides, extra tags).
def _p(kind, dx, dy, z, sx, sy, sz, mat, shape="cube", collide=True, tags=None):
    return (kind, dx, dy, z, sx, sy, sz, mat, shape, collide, tags or [])


def prop_parts(tag, size):
    if tag == "desk":
        return [_p("DeskTop", 0, 0, 72, 160, 80, 4, "wood:Honey"), _p("DeskSide", 0, -76, 0, 76, 6, 72, "wood:Dark"),
                _p("DeskSide", 0, 76, 0, 76, 6, 72, "wood:Dark"), _p("DeskBack", -34, 0, 20, 6, 146, 52, "wood:Dark"),
                _p("DeskLamp", 20, 55, 76, 14, 14, 30, "prop:Brass", "cylinder", False)]
    if tag == "table":
        parts = [_p("TableTop", 0, 0, 71, 180, 90, 4, "wood:Honey")]
        parts += [_p("TableLeg", sx * 80, sy * 36, 0, 6, 6, 71, "wood:Dark") for sx in (-1, 1) for sy in (-1, 1)]
        return parts
    if tag == "chair":
        parts = [_p("ChairSeat", 0, 0, 43, 44, 44, 6, "prop:Velvet"), _p("ChairBack", -20, 0, 49, 5, 44, 44, "wood:Dark")]
        parts += [_p("ChairLeg", sx * 18, sy * 18, 0, 4, 4, 43, "wood:Dark", collide=False) for sx in (-1, 1) for sy in (-1, 1)]
        return parts
    if tag == "shelves":
        parts = [_p("ShelfSide", 0, sy * 88, 0, 40, 4, 200, "prop:Iron") for sy in (-1, 1)]
        parts += [_p("Shelf", 0, 0, z, 40, 176, 3, "prop:Iron") for z in (10, 55, 100, 145, 190)]
        parts += [_p("ShelfBox", 0, dy, z + 3, 34, 38, h, "prop:Cardboard", collide=False)
                  for dy, z, h in ((-50, 10, 30), (30, 10, 38), (-20, 55, 28), (55, 100, 35), (-60, 145, 25))]
        return parts
    if tag == "crates":
        sx, sy, sz = size or (100, 100, 80)
        return [_p("Crate", 0, 0, 0, sx, sy, sz, "prop:Crate"),
                _p("CrateSlat", sx * 0.5 + 0.5, 0, sz * 0.45, 2, sy - 6, 10, "wood:Dark", collide=False),
                _p("CrateSlat", -sx * 0.5 - 0.5, 0, sz * 0.45, 2, sy - 6, 10, "wood:Dark", collide=False),
                _p("CrateSlat", 0, sy * 0.5 + 0.5, sz * 0.45, sx - 6, 2, 10, "wood:Dark", collide=False),
                _p("CrateSlat", 0, -sy * 0.5 - 0.5, sz * 0.45, sx - 6, 2, 10, "wood:Dark", collide=False)]
    if tag == "pallet":
        parts = [_p("PalletRunner", 0, dy, 0, 120, 10, 10, "prop:Crate") for dy in (-45, 0, 45)]
        parts += [_p("PalletBoard", dx, 0, 10, 12, 100, 3, "prop:Crate") for dx in (-54, -27, 0, 27, 54)]
        parts.append(_p("PalletLoad", 0, 0, 13, 100, 80, 60, "prop:Cardboard"))
        return parts
    if tag == "display_case":
        return [_p("CaseBase", 0, 0, 0, 120, 60, 82, "prop:Black"),
                _p("CaseGlass", 0, 0, 82, 116, 56, 52, "glass"),
                _p("CaseRim", 0, 0, 82, 122, 62, 2, "prop:Brass", collide=False),
                _p("CaseGlow", 0, 0, 133, 110, 4, 1.5, "lamp:CaseGlow", collide=False, tags=LAMP_TAGS),
                _p("CaseItem", -20, 0, 84, 14, 14, 22, "prop:Purple", "cylinder", False),
                _p("CaseItem", 25, 0, 84, 20, 20, 20, "prop:Brass", "sphere", False)]
    if tag == "pedestal":
        return [_p("Pedestal", 0, 0, 0, 44, 44, 105, "prop:Cream"), _p("PedestalTop", 0, 0, 105, 52, 52, 5, "prop:Black"),
                _p("PedestalItem", 0, 0, 110, 22, 22, 36, "prop:Purple", "cylinder", False)]
    if tag == "sofa":
        return [_p("SofaBase", 0, 0, 0, 85, 200, 42, "prop:Velvet"), _p("SofaBack", -32, 0, 42, 20, 200, 40, "prop:Velvet"),
                _p("SofaArm", 5, -92, 42, 75, 16, 22, "prop:Velvet"), _p("SofaArm", 5, 92, 42, 75, 16, 22, "prop:Velvet")]
    if tag == "bed":
        return [_p("BedFrame", 0, 0, 0, 200, 100, 32, "wood:Dark"), _p("BedMattress", 0, 0, 32, 194, 94, 18, "prop:Linen"),
                _p("BedPillow", 80, 0, 50, 30, 70, 10, "prop:Linen", collide=False),
                _p("BedHead", 102, 0, 0, 6, 104, 90, "wood:Dark")]
    if tag == "counter":
        return [_p("CounterBody", 0, 0, 0, 70, 300, 100, "wood:Dark"), _p("CounterTop", -3, 0, 100, 82, 312, 5, "prop:Cream"),
                _p("CounterStripe", -36, 0, 70, 2, 300, 12, "prop:Purple", collide=False),
                _p("CounterBell", 0, 60, 105, 10, 10, 6, "prop:Brass", "sphere", False)]
    if tag == "stage":
        sx, sy, sz = size or (250, 600, 40)
        return [_p("Stage", 0, 0, 0, sx, sy, sz - 4, "wood:Dark"),
                _p("StageFloor", 0, 0, sz - 4, sx, sy, 4, "wood_floor", tags=["SurfaceWood"]),
                _p("StageSkirt", -sx * 0.5 - 1, 0, 0, 2, sy, sz - 4, "prop:Purple", collide=False)]
    if tag == "lectern":
        return [_p("Lectern", 0, 0, 0, 40, 50, 105, "wood:Dark"), _p("LecternTop", 4, 0, 105, 50, 60, 5, "wood:Honey"),
                _p("LecternBadge", -21, 0, 70, 2, 24, 24, "prop:Purple", collide=False)]
    if tag == "bench":
        return [_p("Bench", 0, 0, 0, 45, 160, 45, "wood:Honey")]
    if tag == "rug":
        sx, sy, _sz = size or (300, 200, 1)
        return [_p("Rug", 0, 0, 0, sx, sy, 1, "prop:Velvet", collide=False)]
    if tag == "painting":
        return [_p("PaintingFrame", 2, 0, 110, 4, 124, 94, "prop:Brass", collide=False),
                _p("Painting", 4.5, 0, 117, 1, 110, 80, "prop:Purple", collide=False),
                _p("PaintingBand", 5.2, 0, 140, 0.6, 110, 12, "prop:Cream", collide=False)]
    if tag == "plant":
        return [_p("PlantPot", 0, 0, 0, 40, 40, 45, "prop:Black", "cylinder"),
                _p("PlantLeaves", 0, 0, 40, 70, 70, 80, "prop:Leaf", "sphere", False)]
    raise ValueError("no parts for prop tag " + tag)


def build_props(b):
    layout = b.layout
    block_cls = gen.traversable_class()
    for prop in layout.props:
        room = layout.by_id[prop["room"]]
        cx, cy = room.point(*prop["at"])
        yaw = float(prop.get("yaw", 0.0))
        floor = room.z0 + float(prop.get("z", 0.0))
        size = prop.get("size")
        if prop.get("traversable") and block_cls is not None:
            build_climbable_crate(b, room, cx, cy, floor, yaw, size or (100, 100, 80), block_cls)
            continue
        for kind, dx, dy, z, sx, sy, sz, mat, shape, collide, tags in prop_parts(prop["tag"], size):
            x, y = _local(cx, cy, yaw, dx, dy)
            b.box(room.label, kind, (x, y, floor + z + sz * 0.5), (sx, sy, sz), mat, ["IntProp", "prop:" + prop["tag"]] + tags,
                  collide, yaw, shape)
        if prop["tag"] == "display_case":
            b.light(room.label, "CaseLight", (cx, cy, floor + 128.0), CASE_LUMENS, 260.0, WARM, tags=LAMP_TAGS)
        if prop["tag"] == "stage":
            sz = (size or (250, 600, 40))[2]
            b.light(room.label, "StageWash", (cx, cy, room.z1 - 30.0), STAGE_LUMENS, 1200.0,
                    m.INTERIOR_LAMPS["StageWash"][0], shadows=True, spot=True, tags=LAMP_TAGS)
            del sz


def build_climbable_crate(b, room, cx, cy, floor, yaw, size, cls):
    """A crate stack the sample's vault and mantle find: a hidden BP_TraversableBlock carrying the collision
    and the ledges (its construction script paints its own grid material over any other), and the crate
    drawn over it with no collision of its own."""
    sx, sy, sz = (float(v) for v in size)
    ox, oy = _local(cx, cy, yaw, -sx * 0.5, -sy * 0.5)
    label = b.label(room.label, "CrateBlock")
    b.changes += gen._ensure_block(b.existing, label, cls, (ox, oy, floor), yaw, (sx / 100.0, sy / 100.0, sz / 100.0),
                                   [TAG, "IntProp", "prop:crates", "IntClimb"], False, False)
    for kind, dx, dy, z, px, py, pz, mat, shape, _collide, _tags in prop_parts("crates", (sx, sy, sz)):
        x, y = _local(cx, cy, yaw, dx, dy)
        b.box(room.label, kind, (x, y, floor + z + pz * 0.5), (px, py, pz), mat, ["IntProp", "prop:crates"], False, yaw,
              shape)
    # A seam half way up a tall stack: two crates, not one tall block.
    if sz > 150.0:
        b.box(room.label, "CrateSeam", (cx, cy, floor + sz * 0.5), (sx + 1.0, sy + 1.0, 2.0), "wood:Dark", ["IntProp"],
              False, yaw)



# --------------------------------------------------------------------------------------
# play: start, navigation, thugs, pickups, anchors
# --------------------------------------------------------------------------------------

def entrance_transform(layout):
    """((x, y, z), yaw) 150 cm inside the entrance exit, facing into the room."""
    for item, bd, at, _w in layout.exit_spots():
        if item.get("entrance"):
            nx, ny = bd.normal_towards(item["room"])
            x, y = bd.point(at)
            return (x + nx * 150.0, y + ny * 150.0, bd.band * it.FLOOR_HEIGHT + 100.0), it.yaw_of(nx, ny)
    return None, 0.0


def build_play(b):
    layout = b.layout
    loc, yaw = entrance_transform(layout)
    if loc is not None:
        b.located("Lobby" if "lobby" in layout.by_id else "Outside", "PlayerStart", unreal.PlayerStart, loc, yaw)
    x0, y0, x1, y1 = layout.bounds()
    top = layout.bands * it.FLOOR_HEIGHT + 200.0
    nav_cls = c.find_class("NavMeshBoundsVolume", "/Script/NavigationSystem.NavMeshBoundsVolume")
    centre = ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (top - 100.0) * 0.5)
    nav = b.located("Outside", "NavBounds", nav_cls, centre)
    if nav is not None:
        scale = _v((x1 - x0 + 400.0) / 200.0, (y1 - y0 + 400.0) / 200.0, (top + 100.0) / 200.0)
        if not gen.same_vector(nav.get_actor_scale3d(), scale, 1e-3):
            nav.set_actor_scale3d(scale)
            b.changes += 1
    build_thugs(b)
    build_pickups(b)
    build_anchors(b)


def build_thugs(b):
    """Every enemy (layout.enemies(): the "enemies" list and the old "patrols"): his TargetPoints, the
    Blueprint for his type at his start, his weapon, route, wait, crew and the keycards he carries."""
    layout = b.layout
    if not hasattr(unreal, "ThugWeapon"):
        c.log("FAILED", "interior thugs", "AThugCharacter not exposed; build the module")
        return
    pickup_cls = c.find_class("PickupActor", "/Script/Hawkeye.PickupActor")
    classes = {}
    for enemy in layout.enemies():
        bp_path, bp_name = ENEMY_BLUEPRINTS[enemy["type"]]
        if enemy.get("blueprint"):
            # A chapter's own look (the grotto's elves): the type still picks his weapon.
            bp_path, bp_name = enemy["blueprint"].rsplit("/", 1)
        if bp_name not in classes:
            classes[bp_name] = c.load_generated_class(bp_path, bp_name)
        cls = classes[bp_name]
        if cls is None:
            c.log("FAILED", "enemy " + enemy["id"], bp_name + " missing; run create_world_blueprints.py and create_enemies.py")
            continue
        points = []
        for (room_id, _dx, _dy, facing, pitch), (x, y, z, _yaw, _pitch) in zip(enemy["patrol"], layout.enemy_route(enemy)):
            tags = ["IntPatrol", "patrol:" + enemy["id"]] + ([PATROL_FACING_TAG] if facing is not None else [])
            yaw = facing if facing is not None else 0.0
            point = b.located(b.room_label(room_id), "Patrol", unreal.TargetPoint, (x, y, z + PATROL_POINT_HEIGHT), yaw, tags)
            # The pitch of his gaze while he waits there (AThugAIController's LookPitch).
            if point is not None and abs(point.get_actor_rotation().pitch - pitch) > 0.05:
                point.set_actor_rotation(unreal.Rotator(0.0, pitch, yaw), False)
                b.changes += 1
            points.append(point)
        (x, y, z), yaw = layout.enemy_start(enemy)
        room = layout.by_id[enemy["room"]]
        label = b.label(room.label, "Thug")
        actor, n = gen._ensure_located(b.existing, label, cls, _v(x, y, z + THUG_HALF_HEIGHT + 2.0), yaw)
        b.changes += n
        if actor is None:
            continue
        group = enemy["alert_group"]
        # The thug's own "Thug" tag first: takedowns look for it.
        b.changes += gen._ensure_tags(actor, ["Thug", TAG, "IntThug", "enemy:" + enemy["id"]] + (["alert:" + group] if group else []))
        b.changes += gen._ensure_thug_props(actor, enemy["type"], [p for p in points if p is not None])
        b.changes += gen.set_if_different(actor, "patrol_wait_seconds", enemy["wait"], label, 0.01)
        b.prop(actor, "alert_group", unreal.Name(group or "None"), label)
        # Indoors nobody watches all round: a calm archer is a sentry with a cone like the rest.
        b.prop(actor, "archer_sees_all_round", False, label)
        carried = []
        for card in enemy["keycards"]:
            if pickup_cls is None:
                c.log("FAILED", "keycard " + card, "APickupActor not exposed; build the module")
                break
            pickup = b.located(room.label, "Keycard", pickup_cls, (x, y, z), 0.0,
                               ["IntPickup", "carried:" + enemy["id"], "keycard:" + card])
            if pickup is not None:
                style_keycard(b, pickup, card, room.label)
                carried.append(pickup)
        have = [p.get_actor_label() if p else "" for p in actor.get_editor_property("carried_pickups")]
        if have != [p.get_actor_label() for p in carried]:
            actor.set_editor_property("carried_pickups", carried)
            b.changes += 1


def build_pickups(b):
    layout = b.layout
    cls = c.find_class("PickupActor", "/Script/Hawkeye.PickupActor")
    if cls is None:
        c.log("FAILED", "interior pickups", "APickupActor not exposed; build the module")
        return
    for item in layout.pickups:
        room = layout.by_id[item["room"]]
        x, y = room.point(*item["at"])
        actor = b.located(room.label, "Keycard", cls, (x, y, room.z0 + float(item.get("z", 0.0))), 0.0,
                          ["IntPickup", "pickup:" + item["id"]])
        if actor is not None:
            style_keycard(b, actor, item.get("keycard", "red"), room.label)


def style_keycard(b, actor, keycard, room_label):
    """A keycard pickup: a purple card with a brass stripe, hovering low."""
    cube = b.meshes["cube"]
    b.prop(actor, "pickup_type", unreal.PickupType.KEYCARD, room_label)
    b.prop(actor, "keycard_id", unreal.Name(keycard), room_label)
    b.prop(actor, "prompt_override", unreal.Text("[E] Take [keycard]"), room_label)
    b.changes += gen.set_if_different(actor, "hover_height", 14.0, room_label, 0.01)
    for part_name, scale, offset, mat in (("part1", (0.09, 0.055, 0.006), (0, 0, 0), "prop:Purple"),
                                          ("part2", (0.092, 0.014, 0.007), (0, 1.2, 0), "prop:Brass")):
        part = actor.get_editor_property(part_name)
        if part.get_editor_property("static_mesh") != cube:
            part.set_static_mesh(cube)
            b.changes += 1
        if not gen.same_vector(part.get_editor_property("relative_scale3d"), _v(*scale), 1e-5):
            part.set_editor_property("relative_scale3d", _v(*scale))
            b.changes += 1
        if not gen.same_vector(part.get_editor_property("relative_location"), _v(*offset), 0.01):
            part.set_editor_property("relative_location", _v(*offset))
            b.changes += 1
        material = b.mat(mat)
        overrides = part.get_editor_property("override_materials")
        if len(overrides) < 1 or overrides[0] != material:
            part.set_material(0, material)
            b.changes += 1


def anchor_spots(layout):
    """[(room, (x, y, z), yaw)] of each grapple anchor on a rail's top, +X into the mezzanine."""
    out = []
    for anchor in layout.anchors:
        room = layout.by_id[anchor["room"]]
        axis, coord, _lo, _hi = room.side_line(anchor["side"])
        inward = {"W": (1.0, 0.0), "E": (-1.0, 0.0), "N": (0.0, 1.0), "S": (0.0, -1.0)}[anchor["side"]]
        at = float(anchor["at"])
        x, y = (coord, at) if axis == "x" else (at, coord)
        out.append((room, (x, y, room.z0 + it.RAIL_HEIGHT + 5.0), it.yaw_of(*inward)))
    return out


def build_anchors(b):
    cls = c.load_generated_class(gen.ANCHOR_BP_PATH, gen.ANCHOR_BP_NAME)
    if cls is None:
        c.log("FAILED", "interior anchors", "BP_GrappleAnchor missing")
        return
    for room, loc, yaw in anchor_spots(b.layout):
        actor = b.located(room.label, "Anchor", cls, loc, yaw, ["IntAnchor"])
        if actor is None:
            continue
        landing = actor.get_editor_property("landing_point")
        want = _v(ANCHOR_LANDING[0], 0.0, -(it.RAIL_HEIGHT + 5.0))
        if landing is not None and not gen.same_vector(landing.get_editor_property("relative_location"), want, 0.05):
            landing.set_editor_property("relative_location", want)
            b.changes += 1


# --------------------------------------------------------------------------------------
# the game mode and the map
# --------------------------------------------------------------------------------------

def ensure_game_mode():
    """BP_GameMode_Interior: no chapter, no snow, the indoor camera arm, a navmesh built at start."""
    parent = c.load_generated_class(PLAYER_PATH, "BP_HawkeyeGameMode")
    full = c.asset_path(PLAYER_PATH, GAME_MODE)
    if parent is None:
        c.log("skipped", full, "run create_blueprints first")
        return None
    bp = c.load_or_none(full)
    changed = []
    if bp is None:
        factory = c.new_factory("BlueprintFactory")
        c.set_props(factory, [("parent_class", parent)], GAME_MODE + " factory")
        bp, _created = c.create_asset(GAME_MODE, PLAYER_PATH, unreal.Blueprint, factory, quiet=True)
        if bp is None:
            return None
        changed.append("created")
        c.compile_blueprint(bp)
    cdo = c.blueprint_cdo(bp)
    for prop, value in (("outdoor_weather", False), ("interior_camera", True), ("build_navigation_at_start", True)):
        if cdo is not None and cdo.get_editor_property(prop) != value:
            c.set_props(cdo, [(prop, value)], GAME_MODE)
            changed.append(prop)
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("created" if "created" in changed else "updated", full, ", ".join(changed))
    else:
        c.log("exists", full)
    return c.load_generated_class(PLAYER_PATH, GAME_MODE)


def open_or_create(map_path):
    subsystem = c.level_editor_subsystem()
    if c.exists(map_path):
        return bool(subsystem.load_level(map_path)), False
    c.ensure_directory("/Game/Maps")
    try:
        ok = subsystem.new_level(map_path, False)
    except TypeError:
        ok = subsystem.new_level(map_path)
    return bool(ok), bool(ok)


def materials():
    look = m.ensure_city_look()
    mats = m.ensure_interior_materials()
    mats["facades"] = look["facades"]
    mats["stars"] = look["stars"]
    mats["sidewalk"] = look["sidewalk"]
    return mats


def build(layout_path):
    layout = it.parse(layout_path)
    game_mode = ensure_game_mode()
    mats = materials()
    ok, created = open_or_create(layout.map_path)
    if not ok:
        c.log("FAILED", layout.map_path, "could not open or create the map")
        return None
    existing = gen.actors_by_label()
    b = Build(layout, existing, mats)
    openings = layout.openings()
    build_walls(b, openings)
    build_slabs(b)
    build_stairs(b)
    build_doors(b, openings)
    build_exits(b)
    build_windows(b)
    build_exit_signs(b)
    build_lights(b)
    build_props(b)
    build_play(b)
    build_outside(b)
    b.note_changes()
    for label, actor in sorted(existing.items()):

        if label.startswith("Int_") and label not in b.wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            b.changes += 1
    ws = c.world_settings()
    if ws is not None and game_mode is not None and ws.get_editor_property("default_game_mode") != game_mode:
        ws.set_editor_property("default_game_mode", game_mode)
        b.changes += 1
    summary = ", ".join("{0} {1}".format(n, k) for k, n in sorted(b.kinds.items()))
    if created or b.changes:
        c.level_editor_subsystem().save_current_level()
        c.log("created" if created else "updated", layout.map_path,
              "{0} actors, {1} change(s): {2}".format(len(b.wanted), b.changes, summary))
    else:
        c.log("exists", layout.map_path, "{0} actors, nothing changed; level not saved".format(len(b.wanted)))
    unreal.log("[Hawkeye] interior {0}: {1} labelled actors ({2})".format(layout.name, len(b.wanted), summary))
    return layout


def layout_paths():
    only = os.environ.get("HAWKEYE_INTERIOR", "").strip()
    paths = sorted(glob.glob(os.path.join(LAYOUT_DIR, "*.json")))
    if only:
        paths = [p for p in paths if os.path.splitext(os.path.basename(p))[0] == only]
    return paths


def run():
    built = []
    for path in layout_paths():
        try:
            layout = build(path)
        except it.LayoutError as exc:
            for error in exc.errors:
                c.log("FAILED", os.path.basename(path), error)
            continue
        if layout is not None:
            built.append(layout)
    return built


if __name__ == "__main__":
    run()
    c.print_summary("interiors")
