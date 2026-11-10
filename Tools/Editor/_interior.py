"""Interior layouts: read and check a layout description, and work out the geometry generate_interior.py
builds from it. Pure Python with no unreal import, so Tools/Editor/test_interior.py (Tools/test-interior.ps1)
tests every rule here without an editor.

A layout is JSON (Tools/Interiors/<name>.json), in cm, in the interior map's own frame: +X east, +Y south,
so a room's N side is its y0 edge (the district's convention: East 11th Street has a smaller Y than East
6th). Floor k's floor is at z = k * FLOOR_HEIGHT; every slab is SLAB thick with its top at that height.

    {
      "name": "Sample",                       -> /Game/Maps/L_Int_Sample
      "display_name": "[Auction house]",
      "district": "/Game/Maps/L_District_EastVillage",
      "doorstep": "City_InteriorReturn_Sample", the district point the front exit comes back out at
      "rooms":   [{"id", "floor", "rect": [x0, y0, x1, y1], "floors": 1, "surface": "wood|carpet|concrete",
                   "paint": "Cream", "light": "pendant|fluorescent|none", "tone": "warm|cool",
                   "overlooks": "<room>", "rail": ["E"]}],
      "doors":   [{"id", "rooms": [a, b], "floor": k, "at": <coordinate along the wall>, "width": 100,
                   "leaf": "none|wood|steel", "locked": false, "keycard": "<id>", "opens_on": "<objective id>"}],
      "exits":   [{"id", "room", "side": "N", "floor": k, "at", "return": "<district label>" or null,
                   "name": "[Leave]", "entrance": true}],
      "windows": [{"room", "side", "floor", "at", "width": 120, "sill": 90, "height": 150}],
      "stairs":  [{"id", "room", "landing": 170, "flight": 130}],
      "props":   [{"room", "tag", "at": [dx, dy], "yaw": 0, "size": [x, y, z]}],     at is from the room's x0, y0
      "enemies": [{"id", "type": "FISTS|BAT|PISTOL|BOW|SHIELD", "room", "at": [dx, dy], "facing": 90,
                   "patrol": [[room, dx, dy], [room, dx, dy, yaw, pitch], ...], "wait": 2.0, "alert_group": "<crew>",
                   "blueprint": "/Game/Blueprints/AI/BP_Thug_Elf"}],
      "keycards": [{"keycard": "<id>", "carrier": "<enemy id>"}],   a keycard a thug carries and drops
      "patrols": [{"id", "weapon": "FISTS|BAT|PISTOL", "wait": 2.0, "points": [[room, dx, dy], ...]}],   the old
                   form of a patrolling enemy, read as one that starts on its first point
      "pickups": [{"id", "room", "kind": "keycard", "keycard": "<id>", "at": [dx, dy], "z": 76}],
      "anchors": [{"room", "side", "at"}],           grapple anchors on a room's rail edge
      "exit_signs": [{"room", "side", "floor", "at"}]  green signs over openings, besides the exits' own
    }

An enemy stands at "at" in his room (room-relative, on its floor; the first patrol point when there is no
"at"), turned "facing" degrees (0 east, 90 south; toward his first leg when left out). With a "patrol" of
two points or more he walks round its points in order, waiting "wait" s at each: from "at" to the first,
or, with no "at", from the first to the second. A point with a fourth number is one he turns to that yaw
at while he waits, and a fifth pitches his gaze (below 0 is down: a gunner at a gallery rail looking down
into the hall; his sight cone is 35 degrees round his gaze). With no patrol he holds his post. "alert_group" names his crew: his squad alert reaches only thugs of the same group (and a thug with
none only thugs with none). "type" picks the Blueprint: BP_Thug for FISTS, BAT and PISTOL, BP_Archer for
BOW, BP_Thug_Heavy for SHIELD; "blueprint" names another (a chapter's look: the grotto's elves), the type still
picking his weapon. A keycard's carrier drops it where he goes down (takedown or fight).

A locked door with "opens_on" opens by itself when that objective of the running chapter completes (ADoorActor::
OpensOnObjective: the grotto's workshop door, opened by its keypad's examine); it needs no keycard and a leaf.

A room may span several floors ("floors": 2 is a double-height hall or a stair well). A room with
"overlooks" is a mezzanine inside that room's upper floor (a gallery over a hall); the edges listed in
"rail" face the open volume and get a balustrade instead of a wall.

The walls come from a coordinate-compressed grid: every room edge's x and y cut the plan into cells, each
cell on each floor band has one owner (a mezzanine wins over its host), and a wall stands on every cell
edge whose owners differ. So shared walls are built once, and a wall exists exactly where two spaces
meet. Walls are WALL thick, centred on the line.
"""

import json
import math

FLOOR_HEIGHT = 330.0
SLAB = 20.0
WALL = 20.0
DOOR_WIDTH = 100.0
DOOR_HEIGHT = 220.0
MIN_JAMB = 15.0             # cm of wall each side of an opening, from the end of the wall it is in
OPENING_GAP = 20.0          # cm of wall between two openings in one wall
RAIL_HEIGHT = 100.0
RAIL_THICK = 15.0
MAX_RISER = 18.0            # cm; CharacterMovement steps 45, the navmesh agent 35: a stair has to be well under both
MIN_TREAD = 26.0
STAIR_GAP = 40.0            # cm between the two flights (the spine wall)
SIDES = ("N", "S", "W", "E")
SURFACES = ("wood", "carpet", "concrete")
LEAVES = ("none", "wood", "steel")
WEAPONS = ("FISTS", "BAT", "PISTOL")
ENEMY_TYPES = ("FISTS", "BAT", "PISTOL", "BOW", "SHIELD")
POINT_MARGIN = 40.0         # cm an enemy's start and patrol points stay inside their room's walls
PROP_TAGS = ("desk", "table", "chair", "shelves", "crates", "pallet", "display_case", "pedestal", "sofa", "bed",
             "counter", "stage", "lectern", "bench", "rug", "painting", "plant")


class LayoutError(ValueError):
    """A layout that breaks a rule. ``errors`` lists every broken rule, not just the first."""

    def __init__(self, errors):
        ValueError.__init__(self, "; ".join(errors))
        self.errors = list(errors)


class Room(object):
    def __init__(self, raw):
        self.id = raw["id"]
        self.floor = int(raw.get("floor", 0))
        self.x0, self.y0, self.x1, self.y1 = (float(v) for v in raw["rect"])
        self.floors = int(raw.get("floors", 1))
        self.surface = raw.get("surface", "wood")
        self.paint = raw.get("paint", "Cream")
        self.light = raw.get("light", "pendant")
        self.tone = raw.get("tone", "warm")
        self.overlooks = raw.get("overlooks")
        self.rail = list(raw.get("rail", []))
        self.label = raw.get("label", self.id.capitalize())

    @property
    def bands(self):
        return range(self.floor, self.floor + self.floors)

    @property
    def z0(self):
        return self.floor * FLOOR_HEIGHT

    @property
    def z1(self):
        return (self.floor + self.floors) * FLOOR_HEIGHT - SLAB

    def contains(self, x, y, margin=0.0):
        return self.x0 + margin <= x <= self.x1 - margin and self.y0 + margin <= y <= self.y1 - margin

    def side_line(self, side):
        """(axis, coordinate, lo, hi) of one side: axis 'x' is a line x = coordinate running along y."""
        return {"W": ("x", self.x0, self.y0, self.y1), "E": ("x", self.x1, self.y0, self.y1),
                "N": ("y", self.y0, self.x0, self.x1), "S": ("y", self.y1, self.x0, self.x1)}[side]

    def point(self, dx, dy):
        return self.x0 + float(dx), self.y0 + float(dy)


class Boundary(object):
    """Where two owners meet on one floor band. axis 'x': the line x = coord over y in [lo, hi], a on the
    -X side and b on the +X side; axis 'y': y = coord over x, a on the -Y (north) side."""

    def __init__(self, band, axis, coord, lo, hi, a, b, kind):
        self.band, self.axis, self.coord, self.lo, self.hi = band, axis, coord, lo, hi
        self.a, self.b, self.kind = a, b, kind

    @property
    def length(self):
        return self.hi - self.lo

    def rooms(self):
        return {r for r in (self.a, self.b) if r is not None}

    def point(self, s):
        """World (x, y) at s along the line."""
        return (self.coord, s) if self.axis == "x" else (s, self.coord)

    def normal_towards(self, room_id):
        """Unit (nx, ny) pointing from the line into room_id's side."""
        sign = 1.0 if room_id == self.b else -1.0
        return (sign, 0.0) if self.axis == "x" else (0.0, sign)

    def __repr__(self):
        return "Boundary(band={0}, {1}={2}, {3}..{4}, {5}|{6}, {7})".format(
            self.band, self.axis, self.coord, self.lo, self.hi, self.a, self.b, self.kind)


class Layout(object):
    def __init__(self, raw):
        self.raw = raw
        self.name = raw["name"]
        self.display_name = raw.get("display_name", "[Interior]")
        self.district = raw.get("district", "/Game/Maps/L_District_EastVillage")
        self.doorstep = raw.get("doorstep", "City_InteriorReturn_" + self.name)
        self.rooms = [Room(r) for r in raw.get("rooms", [])]
        self.by_id = {r.id: r for r in self.rooms}
        self.doors = list(raw.get("doors", []))
        self.exits = list(raw.get("exits", []))
        self.windows = list(raw.get("windows", []))
        self.stairs = list(raw.get("stairs", []))
        self.props = list(raw.get("props", []))
        self.patrols = list(raw.get("patrols", []))
        self.raw_enemies = list(raw.get("enemies", []))
        self.keycards = list(raw.get("keycards", []))
        self.pickups = list(raw.get("pickups", []))
        self.anchors = list(raw.get("anchors", []))
        self.exit_signs = list(raw.get("exit_signs", []))
        self._grid = None
        self._bounds = {}

    def enemies(self):
        """Every enemy, normalised: {"id", "type", "room", "at": (dx, dy), "facing": yaw or None, "patrol":
        [(room, dx, dy, yaw or None, pitch)] in the order he walks them, "wait", "alert_group", "keycards": [ids
        he carries]}. The old "patrols" come after the "enemies", each starting on its first point."""
        out = []
        for raw in self.raw_enemies:
            patrol = [(p[0], float(p[1]), float(p[2]), float(p[3]) if len(p) > 3 else None, float(p[4]) if len(p) > 4 else 0.0)
                      for p in raw.get("patrol", [])]
            at = raw.get("at")
            if at is None and patrol:
                # He starts on his first point, so his first walk is to the second.
                at = (patrol[0][1], patrol[0][2])
                patrol = patrol[1:] + patrol[:1]
            out.append({"id": raw.get("id"), "type": raw.get("type", "FISTS"), "room": raw.get("room"),
                        "at": (float(at[0]), float(at[1])) if at is not None else None,
                        "facing": float(raw["facing"]) if raw.get("facing") is not None else None,
                        "patrol": patrol, "wait": float(raw.get("wait", 2.0)),
                        "alert_group": raw.get("alert_group") or "", "keycards": [],
                        "blueprint": raw.get("blueprint") or ""})
        for raw in self.patrols:
            points = [(p[0], float(p[1]), float(p[2]), None, 0.0) for p in raw.get("points", [])]
            first = points[0] if points else (None, 0.0, 0.0, None, 0.0)
            # He starts on his first point: walking out and back is the second point first.
            out.append({"id": raw.get("id"), "type": raw.get("weapon", "FISTS"), "room": first[0], "at": (first[1], first[2]),
                        "facing": None, "patrol": points[1:] + points[:1] if len(points) > 1 else [],
                        "wait": float(raw.get("wait", 2.0)), "alert_group": raw.get("alert_group") or "", "keycards": [],
                        "blueprint": ""})
        by_id = {e["id"]: e for e in out}
        for card in self.keycards:
            if card.get("carrier") in by_id:
                by_id[card["carrier"]]["keycards"].append(card.get("keycard"))
        return out

    def enemy_start(self, enemy):
        """((x, y, floor z), yaw) where an enemy starts, in the map's frame."""
        room = self.by_id[enemy["room"]]
        x, y = room.point(*enemy["at"])
        yaw = enemy["facing"]
        if yaw is None:
            nxt = None
            for room_id, dx, dy, _yaw, _pitch in enemy["patrol"]:
                px, py = self.by_id[room_id].point(dx, dy)
                if abs(px - x) > 1.0 or abs(py - y) > 1.0:
                    nxt = (px, py)
                    break
            yaw = math.degrees(math.atan2(nxt[1] - y, nxt[0] - x)) if nxt is not None else 0.0
        return (x, y, room.z0), yaw

    def enemy_route(self, enemy):
        """[(x, y, floor z, yaw or None, pitch)] of the points he walks, in order; empty for a post."""
        route = []
        for room_id, dx, dy, yaw, pitch in enemy["patrol"]:
            room = self.by_id[room_id]
            x, y = room.point(dx, dy)
            route.append((x, y, room.z0, yaw, pitch))
        return route

    @property
    def map_path(self):
        return "/Game/Maps/L_Int_" + self.name

    @property
    def bands(self):
        return max(b for r in self.rooms for b in r.bands) + 1 if self.rooms else 0

    def bounds(self):
        return (min(r.x0 for r in self.rooms), min(r.y0 for r in self.rooms),
                max(r.x1 for r in self.rooms), max(r.y1 for r in self.rooms))

    # --- the owner grid -------------------------------------------------------------------------

    def grid(self):
        """(xs, ys, owners): owners[band][i][j] owns cell [xs[i], xs[i+1]] x [ys[j], ys[j+1]]."""
        if self._grid is None:
            xs = sorted({v for r in self.rooms for v in (r.x0, r.x1)})
            ys = sorted({v for r in self.rooms for v in (r.y0, r.y1)})
            owners = []
            for band in range(self.bands):
                rows = []
                for i in range(len(xs) - 1):
                    col = []
                    cx = (xs[i] + xs[i + 1]) * 0.5
                    for j in range(len(ys) - 1):
                        col.append(self.owner_at(band, cx, (ys[j] + ys[j + 1]) * 0.5))
                    rows.append(col)
                owners.append(rows)
            self._grid = (xs, ys, owners)
        return self._grid

    def owner_at(self, band, x, y):
        """The room whose space (x, y) is in on band, a mezzanine before the room it overlooks; None outside."""
        found = [r for r in self.rooms if band in r.bands and r.contains(x, y)]
        if not found:
            return None
        found.sort(key=lambda r: (0 if r.overlooks else 1, r.id))
        return found[0].id

    def edge_kind(self, a, b):
        if a is None or b is None:
            return "exterior"
        ra, rb = self.by_id[a], self.by_id[b]
        if ra.overlooks == b or rb.overlooks == a:
            return "rail"
        return "wall"

    def boundaries(self, band):
        """Every Boundary on band, merged along each line where the owner pair stays the same. The same
        objects every call, so openings can be keyed on them."""
        if band not in self._bounds:
            self._bounds[band] = self._build_boundaries(band)
        return self._bounds[band]

    def _build_boundaries(self, band):
        xs, ys, owners = self.grid()

        cells = owners[band] if 0 <= band < len(owners) else None
        nx, ny = len(xs) - 1, len(ys) - 1

        def own(i, j):
            return cells[i][j] if cells is not None and 0 <= i < nx and 0 <= j < ny else None


        out = []
        for i in range(nx + 1):          # vertical lines x = xs[i]
            run = None
            for j in range(ny):
                a, b = own(i - 1, j), own(i, j)
                key = (a, b) if a != b else None
                if run is not None and (key is None or key != run[0] or ys[j] != run[2]):
                    out.append(Boundary(band, "x", xs[i], run[1], run[2], run[0][0], run[0][1],
                                        self.edge_kind(*run[0])))
                    run = None
                if key is not None:
                    run = [key, run[1] if run else ys[j], ys[j + 1]] if run else [key, ys[j], ys[j + 1]]
            if run is not None:
                out.append(Boundary(band, "x", xs[i], run[1], run[2], run[0][0], run[0][1], self.edge_kind(*run[0])))
        for j in range(ny + 1):          # horizontal lines y = ys[j]
            run = None
            for i in range(nx):
                a, b = own(i, j - 1), own(i, j)
                key = (a, b) if a != b else None
                if run is not None and (key is None or key != run[0] or xs[i] != run[2]):
                    out.append(Boundary(band, "y", ys[j], run[1], run[2], run[0][0], run[0][1],
                                        self.edge_kind(*run[0])))
                    run = None
                if key is not None:
                    run = [key, run[1] if run else xs[i], xs[i + 1]] if run else [key, xs[i], xs[i + 1]]
            if run is not None:
                out.append(Boundary(band, "y", ys[j], run[1], run[2], run[0][0], run[0][1], self.edge_kind(*run[0])))
        return out

    def all_boundaries(self):
        return [b for band in range(self.bands) for b in self.boundaries(band)]

    def adjacent(self, a, b, band=None):
        """True when rooms a and b share a wall (not a rail) on band (any band when None)."""
        bands = [band] if band is not None else range(self.bands)
        return any(bd.rooms() == {a, b} and bd.kind == "wall" for k in bands for bd in self.boundaries(k))

    # --- slabs -----------------------------------------------------------------------------------

    def slabs(self):
        """[(level, x0, y0, x1, y1, above, below)]: a slab with its top at level * FLOOR_HEIGHT wherever the
        owner changes from band level - 1 to band level (a floor, a roof, a mezzanine's floor), merged into
        rectangles. A stair well's intermediate landings are the stair's own (stair_parts)."""
        xs, ys, owners = self.grid()
        nx, ny = len(xs) - 1, len(ys) - 1

        def own(band, i, j):
            return owners[band][i][j] if 0 <= band < len(owners) else None

        out = []
        for level in range(self.bands + 1):
            keys = [[None] * ny for _ in range(nx)]
            for i in range(nx):
                for j in range(ny):
                    below, above = own(level - 1, i, j), own(level, i, j)
                    if below != above:
                        keys[i][j] = (above, below)
            used = [[False] * ny for _ in range(nx)]
            for j in range(ny):
                for i in range(nx):
                    key = keys[i][j]
                    if key is None or used[i][j]:
                        continue
                    i1 = i
                    while i1 + 1 < nx and keys[i1 + 1][j] == key and not used[i1 + 1][j]:
                        i1 += 1
                    j1 = j
                    while j1 + 1 < ny and all(keys[k][j1 + 1] == key and not used[k][j1 + 1] for k in range(i, i1 + 1)):
                        j1 += 1
                    for k in range(i, i1 + 1):
                        for m in range(j, j1 + 1):
                            used[k][m] = True
                    out.append((level, xs[i], ys[j], xs[i1 + 1], ys[j1 + 1], key[0], key[1]))
        return out

    # --- openings ---------------------------------------------------------------------------------

    def _find_boundary(self, band, rooms, kind, at, errors, what):
        """The boundary on band between rooms (a set; one room and None for exterior) containing at (or the
        longest when at is None)."""
        matches = [bd for bd in self.boundaries(band) if bd.kind == kind and set((bd.a, bd.b)) == rooms]
        if not matches:
            errors.append("{0}: no {1} between {2} on floor {3}".format(
                what, kind, " and ".join(sorted(str(r) for r in rooms)), band))
            return None
        if at is None:
            return max(matches, key=lambda bd: bd.length)
        for bd in matches:
            if bd.lo <= at <= bd.hi:
                return bd
        errors.append("{0}: at {1} is on none of the walls between {2} on floor {3}".format(
            what, at, " and ".join(sorted(str(r) for r in rooms)), band))
        return None

    def _check_fit(self, bd, at, width, errors, what):
        lo, hi = bd.lo + MIN_JAMB + width * 0.5, bd.hi - MIN_JAMB - width * 0.5
        if lo > hi:
            errors.append("{0}: a {1:.0f} cm opening does not fit a {2:.0f} cm wall with {3:.0f} cm each side".format(
                what, width, bd.length, MIN_JAMB))
            return None
        if at is None:
            return (bd.lo + bd.hi) * 0.5
        if not lo <= at <= hi:
            errors.append("{0}: at {1:.0f} is within {2:.0f} cm of the end of its wall ({3:.0f} to {4:.0f})".format(
                what, at, MIN_JAMB, bd.lo, bd.hi))
            return None
        return float(at)

    def door_spots(self, errors=None):
        """[(door dict, Boundary, centre along, width)] for every door, in order. Rules broken go in errors."""
        errors = errors if errors is not None else []
        out = []
        for door in self.doors:
            what = "door " + door.get("id", "?")
            a, b = door.get("rooms", [None, None])
            if a not in self.by_id or b not in self.by_id:
                errors.append("{0}: unknown room in {1}".format(what, door.get("rooms")))
                continue
            band = door.get("floor")
            if band is None:
                band = max(self.by_id[a].floor, self.by_id[b].floor)
            if door.get("leaf", "wood") not in LEAVES:
                errors.append("{0}: leaf {1} is not one of {2}".format(what, door.get("leaf"), ", ".join(LEAVES)))
            width = float(door.get("width", DOOR_WIDTH))
            if width < DOOR_WIDTH:
                errors.append("{0}: {1:.0f} cm is narrower than a {2:.0f} cm door".format(what, width, DOOR_WIDTH))
            if door.get("leaf", "wood") != "none" and width != DOOR_WIDTH:
                errors.append("{0}: a door with a leaf is {1:.0f} cm wide".format(what, DOOR_WIDTH))
            bd = self._find_boundary(band, {a, b}, "wall", door.get("at"), errors, what)
            if bd is None:
                continue
            at = self._check_fit(bd, door.get("at"), width, errors, what)
            if at is not None:
                out.append((door, bd, at, width))
        return out

    def exterior_spots(self, items, default_width, errors, noun):
        """[(item, Boundary, centre along, width)] for exits or windows: each on its room's side, outside."""
        out = []
        for item in items:
            what = "{0} {1}".format(noun, item.get("id", item.get("room", "?")))
            room = self.by_id.get(item.get("room"))
            if room is None:
                errors.append("{0}: unknown room {1}".format(what, item.get("room")))
                continue
            side = item.get("side")
            if side not in SIDES:
                errors.append("{0}: side {1} is not one of N S W E".format(what, side))
                continue
            band = int(item.get("floor", room.floor))
            if band not in room.bands:
                errors.append("{0}: floor {1} is not one of {2}'s".format(what, band, room.id))
                continue
            axis, coord, lo, hi = room.side_line(side)
            width = float(item.get("width", default_width))
            at = item.get("at")
            at = (lo + hi) * 0.5 if at is None else float(at)
            match = [bd for bd in self.boundaries(band) if bd.kind == "exterior" and bd.axis == axis
                     and abs(bd.coord - coord) < 0.01 and room.id in bd.rooms() and bd.lo <= at <= bd.hi]
            if not match:
                errors.append("{0}: {1} side of {2} at {3:.0f} is not an outside wall on floor {4}".format(
                    what, side, room.id, at, band))
                continue
            at = self._check_fit(match[0], at, width, errors, what)
            if at is not None:
                out.append((item, match[0], at, width))
        return out

    def exit_spots(self, errors=None):
        errors = errors if errors is not None else []
        return self.exterior_spots(self.exits, DOOR_WIDTH, errors, "exit")

    def window_spots(self, errors=None):
        errors = errors if errors is not None else []
        return self.exterior_spots(self.windows, 120.0, errors, "window")

    def openings(self, errors=None):
        """{id(Boundary): [(s0, s1, z0, z1, kind, item)]} every door, exit and window cut in its wall."""
        errors = errors if errors is not None else []
        out = {}

        def add(bd, at, width, z0, z1, kind, item):
            out.setdefault(id(bd), (bd, []))[1].append((at - width * 0.5, at + width * 0.5, z0, z1, kind, item))

        for door, bd, at, width in self.door_spots(errors):
            z = bd.band * FLOOR_HEIGHT
            add(bd, at, width, z, z + DOOR_HEIGHT, "door", door)
        for item, bd, at, width in self.exit_spots(errors):
            z = bd.band * FLOOR_HEIGHT
            add(bd, at, width, z, z + DOOR_HEIGHT, "exit", item)
        for item, bd, at, width in self.window_spots(errors):
            z = bd.band * FLOOR_HEIGHT + float(item.get("sill", 90.0))
            add(bd, at, width, z, z + float(item.get("height", 150.0)), "window", item)
        for bd, cuts in out.values():
            cuts.sort(key=lambda c_: c_[0])
            for first, second in zip(cuts, cuts[1:]):
                if second[0] - first[1] < OPENING_GAP and first[3] > second[2] and second[3] > first[2]:
                    errors.append("{0} and {1} are closer than {2:.0f} cm in one wall".format(
                        _name(first), _name(second), OPENING_GAP))
        return out

    # --- validation --------------------------------------------------------------------------------

    def validate(self):
        errors = []
        seen = set()
        for room in self.rooms:
            if room.id in seen:
                errors.append("room {0}: id used twice".format(room.id))
            seen.add(room.id)
            if room.x1 - room.x0 < 150.0 or room.y1 - room.y0 < 150.0:
                errors.append("room {0}: smaller than 150 cm across".format(room.id))
            if room.surface not in SURFACES:
                errors.append("room {0}: surface {1} is not one of {2}".format(room.id, room.surface, ", ".join(SURFACES)))
            if room.overlooks is not None:
                host = self.by_id.get(room.overlooks)
                if host is None:
                    errors.append("room {0}: overlooks unknown room {1}".format(room.id, room.overlooks))
                elif not (host.x0 <= room.x0 and room.x1 <= host.x1 and host.y0 <= room.y0 and room.y1 <= host.y1
                          and set(room.bands) <= set(host.bands) and room.floor > host.floor):
                    errors.append("room {0}: a mezzanine must sit inside {1}'s upper floors".format(room.id, host.id))
                for side in room.rail:
                    if side not in SIDES:
                        errors.append("room {0}: rail side {1} is not one of N S W E".format(room.id, side))
        for i, a in enumerate(self.rooms):
            for b in self.rooms[i + 1:]:
                if a.overlooks == b.id or b.overlooks == a.id:
                    continue
                shared = set(a.bands) & set(b.bands)
                if shared and a.x0 < b.x1 and b.x0 < a.x1 and a.y0 < b.y1 and b.y0 < a.y1:
                    errors.append("rooms {0} and {1} overlap on floor {2}".format(a.id, b.id, min(shared)))
        if errors:
            return errors
        self.openings(errors)
        for stair in self.stairs:
            try:
                stair_parts(self, stair)
            except LayoutError as exc:
                errors.extend(exc.errors)
        for prop in self.props:
            room = self.by_id.get(prop.get("room"))
            if room is None:
                errors.append("prop {0}: unknown room {1}".format(prop.get("tag"), prop.get("room")))
            elif prop.get("tag") not in PROP_TAGS:
                errors.append("prop in {0}: tag {1} is not one of {2}".format(room.id, prop.get("tag"), ", ".join(PROP_TAGS)))
            elif not room.contains(*room.point(*prop.get("at", (0, 0)))):
                errors.append("prop {0} in {1}: at {2} is outside the room".format(prop.get("tag"), room.id, prop.get("at")))
        for patrol in self.patrols:
            if patrol.get("weapon", "FISTS") not in WEAPONS:
                errors.append("patrol {0}: weapon {1} is not one of {2}".format(patrol.get("id"), patrol.get("weapon"),
                                                                                ", ".join(WEAPONS)))
            if len(patrol.get("points", [])) < 2:
                errors.append("patrol {0}: needs two points or more".format(patrol.get("id")))
            for point in patrol.get("points", []):
                room = self.by_id.get(point[0])
                if room is None or not room.contains(*room.point(point[1], point[2]), margin=40.0):
                    errors.append("patrol {0}: point {1} is not 40 cm inside its room".format(patrol.get("id"), point))
        errors.extend(self.enemy_errors())
        for anchor in self.anchors:
            room = self.by_id.get(anchor.get("room"))
            if room is None or anchor.get("side") not in room.rail:
                errors.append("anchor on {0} {1}: not a rail edge".format(anchor.get("room"), anchor.get("side")))
        entrances = [e for e in self.exits if e.get("entrance")]
        if len(entrances) != 1:
            errors.append("exactly one exit must be the entrance (has {0})".format(len(entrances)))
        return errors


    def enemy_errors(self):
        errors = []
        seen = set()
        for raw in self.raw_enemies:
            what = "enemy {0}".format(raw.get("id", "?"))
            if not raw.get("id"):
                errors.append("enemy: every enemy needs an id")
            elif raw["id"] in seen:
                errors.append("{0}: id used twice".format(what))
            seen.add(raw.get("id"))
            if raw.get("type", "FISTS") not in ENEMY_TYPES:
                errors.append("{0}: type {1} is not one of {2}".format(what, raw.get("type"), ", ".join(ENEMY_TYPES)))
            room = self.by_id.get(raw.get("room"))
            if room is None:
                errors.append("{0}: unknown room {1}".format(what, raw.get("room")))
                continue
            patrol = raw.get("patrol", [])
            at = raw.get("at") or (patrol[0][1:3] if patrol and len(patrol[0]) >= 3 else None)
            if at is None or not room.contains(*room.point(at[0], at[1]), margin=POINT_MARGIN):
                errors.append("{0}: start {1} is not {2:.0f} cm inside {3}".format(what, at, POINT_MARGIN, room.id))
            if len(patrol) == 1:
                errors.append("{0}: a patrol needs two points or more (an enemy with none holds his post)".format(what))
            for point in patrol:
                other = self.by_id.get(point[0]) if point else None
                if other is None or len(point) < 3 or not other.contains(*other.point(point[1], point[2]), margin=POINT_MARGIN):
                    errors.append("{0}: patrol point {1} is not {2:.0f} cm inside its room".format(what, point, POINT_MARGIN))
            for point in patrol:
                if len(point) > 4 and not -60.0 <= float(point[4]) <= 30.0:
                    errors.append("{0}: patrol point {1} pitches its gaze {2} (between -60 and 30)".format(what, point, point[4]))
            group = raw.get("alert_group", "")
            if group and not all(ch.isalnum() or ch == "_" for ch in group):
                errors.append("{0}: alert group {1} is letters, digits and _ only".format(what, group))
            blueprint = raw.get("blueprint")
            if blueprint and (not str(blueprint).startswith("/Game/") or "." in str(blueprint)):
                errors.append("{0}: blueprint {1} is a /Game/ package path".format(what, blueprint))
        for patrol in self.patrols:
            if patrol.get("id") in seen:
                errors.append("patrol {0}: id used by an enemy too".format(patrol.get("id")))
            seen.add(patrol.get("id"))
        for door in self.doors:
            if door.get("opens_on") and (not door.get("locked") or door.get("leaf", "wood") == "none"):
                errors.append("door {0}: opens_on needs a locked door with a leaf".format(door.get("id")))
        carried = set()
        locks = {d.get("keycard") for d in self.doors if d.get("locked")}
        for card in self.keycards:
            what = "keycard {0}".format(card.get("keycard", "?"))
            if not card.get("keycard"):
                errors.append("keycard: needs a keycard id")
            if card.get("carrier") not in seen:
                errors.append("{0}: carrier {1} is not an enemy".format(what, card.get("carrier")))
            if card.get("keycard") in carried:
                errors.append("{0}: carried twice".format(what))
            carried.add(card.get("keycard"))
            if card.get("keycard") not in locks:
                errors.append("{0}: opens no locked door".format(what))
        return errors


def _name(cut):
    item = cut[5]
    return "{0} {1}".format(cut[4], item.get("id", item.get("room", "?")))


def parse(source):
    """A Layout from a JSON string, a dict, or a file path. Raises LayoutError listing every broken rule."""
    if isinstance(source, dict):
        raw = source
    elif isinstance(source, str) and source.lstrip().startswith("{"):
        raw = json.loads(source)
    else:
        with open(source, "r") as handle:
            raw = json.load(handle)
    missing = [key for key in ("name", "rooms") if key not in raw]
    if missing:
        raise LayoutError(["layout: missing " + ", ".join(missing)])
    try:
        layout = Layout(raw)
    except (KeyError, TypeError, ValueError) as exc:
        raise LayoutError(["layout: {0}: {1}".format(type(exc).__name__, exc)])
    errors = layout.validate()
    if errors:
        raise LayoutError(errors)
    return layout


# --- walls, pieces -----------------------------------------------------------------------------------

def wall_pieces(bd, cuts):
    """[(s0, s1, z0, z1)] solid pieces of the wall on bd with cuts [(s0, s1, z0, z1, ...)] removed. A rail
    edge has no wall. The wall fills its band from the floor to the next floor's top."""
    if bd.kind == "rail":
        return []
    z0, z1 = bd.band * FLOOR_HEIGHT, (bd.band + 1) * FLOOR_HEIGHT
    pieces = []
    cursor = bd.lo
    for s0, s1, c0, c1 in ((c_[0], c_[1], c_[2], c_[3]) for c_ in sorted(cuts, key=lambda c_: c_[0])):
        if s0 > cursor:
            pieces.append((cursor, s0, z0, z1))
        if c0 > z0:
            pieces.append((s0, s1, z0, c0))
        if c1 < z1:
            pieces.append((s0, s1, c1, z1))
        cursor = max(cursor, s1)
    if cursor < bd.hi:
        pieces.append((cursor, bd.hi, z0, z1))
    return [p for p in pieces if p[1] - p[0] > 0.5 and p[3] - p[2] > 0.5]


# --- stairs --------------------------------------------------------------------------------------------

def stair_parts(layout, stair):
    """The geometry of a switchback stair in its well, as boxes (x0, y0, z0, x1, y1, z1).

    The well is a room two floors high whose long side runs along Y. From its N end: a landing zone
    ``landing`` deep at floor level (under the top landing), flight 1 up the west half to a mid landing
    half a floor up across the S end, flight 2 back north up the east half to the top landing, a slab
    across the N end at the upper floor. Every riser is FLOOR_HEIGHT / 2 / n with n the fewest steps
    keeping it at or under MAX_RISER; every tread is the run divided by n and must be MIN_TREAD or more.

    Returns {"steps": [...], "mid_landing": box, "top_landing": box, "spine": box, "rails": [...],
    "riser": r, "tread": t, "n": n}. Raises LayoutError when the well is too small.
    """
    room = layout.by_id.get(stair.get("room"))
    what = "stair " + stair.get("id", "?")
    if room is None:
        raise LayoutError(["{0}: unknown room {1}".format(what, stair.get("room"))])
    if room.floors < 2:
        raise LayoutError(["{0}: {1} is one floor high; a stair well is two".format(what, room.id)])
    landing = float(stair.get("landing", 170.0))
    flight = float(stair.get("flight", 130.0))
    width = room.x1 - room.x0
    length = room.y1 - room.y0
    run = length - 2.0 * landing
    half = FLOOR_HEIGHT * 0.5
    n = int(math.ceil(half / MAX_RISER - 1e-9))
    riser = half / n
    tread = run / n if n else 0.0
    errors = []
    if 2.0 * flight + STAIR_GAP > width + 0.01:
        errors.append("{0}: two {1:.0f} cm flights and the {2:.0f} cm spine need {3:.0f} cm, {4} is {5:.0f} wide".format(
            what, flight, STAIR_GAP, 2 * flight + STAIR_GAP, room.id, width))
    if tread < MIN_TREAD:
        errors.append("{0}: {1} steps in a {2:.0f} cm run are {3:.1f} cm treads, under {4:.0f}".format(
            what, n, run, tread, MIN_TREAD))
    if landing < DOOR_WIDTH + 2 * MIN_JAMB:
        errors.append("{0}: a {1:.0f} cm landing cannot take a door".format(what, landing))
    if errors:
        raise LayoutError(errors)
    z0 = room.z0
    x0, x1, y0 = room.x0, room.x1, room.y0
    west = (x0, x0 + flight)
    east = (x1 - flight, x1)
    y_run0, y_run1 = y0 + landing, y0 + landing + run
    steps = []
    for i in range(n):
        top = z0 + (i + 1) * riser
        steps.append((west[0], y_run0 + i * tread, z0, west[1], y_run0 + (i + 1) * tread, top))
    for i in range(n):
        top = z0 + half + (i + 1) * riser
        steps.append((east[0], y_run1 - (i + 1) * tread, z0, east[1], y_run1 - i * tread, top))
    mid = (x0, y_run1, z0, x1, room.y1, z0 + half)
    top_z = z0 + FLOOR_HEIGHT
    top_landing = (x0, y0, top_z - SLAB, x1, y_run0, top_z)
    spine = (west[1], y_run0, z0, east[0], y_run1, top_z + RAIL_HEIGHT)
    # The top landing's edge over flight 1, and the mid landing's open side toward the spine's end.
    rails = [(west[0], y_run0 - RAIL_THICK * 0.5, top_z, west[1], y_run0 + RAIL_THICK * 0.5, top_z + RAIL_HEIGHT)]
    return {"steps": steps, "mid_landing": mid, "top_landing": top_landing, "spine": spine, "rails": rails,
            "riser": riser, "tread": tread, "n": n, "run": run}


def stair_climb_ok(parts, max_step=MAX_RISER):
    """True when walking up from the floor, flight 1, the mid landing, flight 2 and onto the top landing
    never meets a rise of more than max_step."""
    n = parts["n"]
    steps = parts["steps"]
    prev = steps[0][2]
    for top in [s[5] for s in steps[:n]] + [parts["mid_landing"][5]] + [s[5] for s in steps[n:]] +             [parts["top_landing"][5]]:
        if top - prev > max_step + 1e-6 or top < prev - 1e-6:
            return False
        prev = top
    return True


# --- points ---------------------------------------------------------------------------------------------

def room_floor_point(layout, room_id, dx, dy):
    """(x, y, z) on room_id's floor, dx, dy from its x0, y0."""
    room = layout.by_id[room_id]
    x, y = room.point(dx, dy)
    return x, y, room.z0


def yaw_of(nx, ny):
    return math.degrees(math.atan2(ny, nx))
