"""The enemies' head as a hand-authored control cage (claude-docs/blender-pipeline.md, "Enemy heads, second pass").

numpy only, no bpy, so it runs (and is tested) outside Blender: `python Tools/Blender/test_headcage.py`.
`headmesh.py` subdivides the cage twice in Blender and bakes the textures; `headbuild.py` skins the result.

The cage is about 420 vertices, quad-dominant, mirrored on X, with the topology a face needs:

* a lattice of 19 horizontal rings (neck base to crown) of 16 vertices, whose rows are chosen to pass through the
  landmarks (menton, chin, mouth, nose base, nose tip, the eyes' lower, middle and upper lines, the brow, the
  forehead) and whose columns run from the front midline round to the back midline (seam);
* round each eye, three nested loops (the orbit, the lid fold, the lid margin) and a sealed pocket behind the margin
  so the lids have thickness over the eyeball; the eyeball itself is a separate mesh (`eye_arrays` in headshape);
* round the mouth, three nested loops (the lips' outer border, the lip line) and a sealed pocket behind the line, so
  the upper and lower lip meet along a real slit;
* a nose with a bridge, a tip, wings and two nostril cavities (an inset on the underside quad and a sealed pocket);
* the chin, the jaw, a neck narrower than the jaw.

Ears are their own small cage (`ear_cage`): helix, antihelix, concha, tragus and lobe on a thick plate.

Frame: the Blender frame the body stands in (X his left, -Y his front, Z up), in METRES on output; the tables below
are written in centimetres, with `yf` forward (the front is +yf; the build negates it). Heights are from the
chin's lowest point (the menton), which the build seats at `CHIN_Z`.

Every number is a landmark of an ordinary adult male head, written as a named constant or a row of a table; none is
taken from a person. Variation (`looks.FaceSpec`) is a landmark-driven deformation of this cage (`deform`), not a
height field.
"""

import math

import numpy as np

CHIN_Z = 1.563          # metres: where the menton sits in the body's frame
HEAD_H_CM = 22.5        # menton to vertex
# Proportions (centimetres from the menton). Eyes at half the head's height; the brow ridge just above them; the
# nose base a third of the way up from the chin to the brow line plus; the mouth a third of the way from the nose base
# to the chin; ears from the brow to the nose base.
EYE_Z = 0.5 * HEAD_H_CM                  # 11.25
BROW_Z = 13.45
NOSE_BASE_Z = 6.2
MOUTH_Z = NOSE_BASE_Z - (NOSE_BASE_Z - 0.0) / 3.0     # a third of the way down from the nose base to the chin: 4.13
EAR_TOP_Z = 12.6
EAR_BOTTOM_Z = 6.2
EYE_X = 3.1                              # the iris centre, so 6.2 apart (one eye-width and a bit between the eyes)
EYE_R = 1.32                             # eyeball radius
MOUTH_HALF_W = 2.55
NECK_HALF_W = 4.6
NECK_HALF_D = 5.8
V_MIN = -12.3                            # the lowest ring (the neck's base, inside the collar)
V_HEAD = 0.86                            # the head's UVs fill v 0..V_HEAD; the ears' strip is above
SEGMENTS = 16                            # vertices round each lattice ring

ROW_NAMES = ["n12", "n7", "n4", "ju", "men", "chin", "m2", "m1", "m0", "sub", "tip", "nb", "e2", "e1", "e0", "f1", "f2",
             "f3", "t1", "t2"]
R = {name: i for i, name in enumerate(ROW_NAMES)}
# row: (z, half width, front yf of the face's plane, back yf, squareness p)
ROW_TABLE = {
    "n12": (-12.3, 4.6, 2.8, -8.8, 2.2),
    "n7": (-7.0, 4.6, 2.8, -8.8, 2.2),
    "n4": (-4.0, 4.7, 3.0, -8.9, 2.2),
    "ju": (-1.6, 4.9, 4.6, -9.1, 2.2),
    "men": (0.0, 4.7, 6.9, -9.5, 2.2),
    "chin": (1.2, 5.0, 7.7, -9.8, 2.3),
    "m2": (2.45, 5.5, 7.8, -10.05, 2.4),
    "m1": (4.1, 5.8, 8.3, -10.25, 2.4),
    "m0": (5.6, 6.1, 8.35, -10.35, 2.2),
    "sub": (6.2, 6.3, 8.35, -10.4, 2.2),
    "tip": (6.9, 6.5, 8.35, -10.45, 2.3),
    "nb": (7.7, 7.0, 8.4, -10.5, 2.4),
    "e2": (9.05, 7.3, 8.55, -10.5, 2.9),
    "e1": (11.25, 7.3, 8.6, -10.55, 3.0),
    "e0": (13.45, 7.5, 8.9, -10.55, 2.8),
    "f1": (15.1, 7.6, 8.95, -10.45, 2.6),
}
DOME = {"f2": 16.8, "f3": 18.5, "t1": 20.0, "t2": 21.5}     # rows scaled from f1 toward the crown
APEX_Z = HEAD_H_CM
# Lattice column x as a fraction of the row's half width, for j = 1..4 (j = 0 the front midline; 5 the side; 6, 7 the
# back's sides by angle; 8 the back midline).
COLUMN_FRACTION = (0.0, 0.16, 0.43, 0.70, 0.90)
BACK_ANGLES = {5: 90.0, 6: 120.0, 7: 150.0, 8: 180.0}
# Rows whose columns are placed by hand (x per column j = 1..4), the eye's, nose's and mouth's rows.
ROW_X = {
    "men": (1.3, 2.4, 3.4, 4.3),
    "chin": (1.3, 2.5, 3.8, 4.6),
    "m2": (1.2, 2.6, 3.9, 5.1),
    "m1": (1.3, 3.5, 4.9, 5.7),
    "m0": (1.25, 2.65, 4.2, 5.6),
    "sub": (0.65, 2.0, 4.0, 5.7),
    "tip": (0.8, 2.1, 4.1, 5.8),
    "nb": (1.3, 2.8, 4.6, 6.1),
    "e2": (1.35, 3.0, 4.9, 6.4),
    "e1": (0.9, 3.1, 5.55, 6.9),
    "e0": (1.35, 3.1, 5.0, 6.9),
}
# Hand-placed heights of lattice nodes (row, j): z overrides, in cm (the eye's tilt, the mouth's corners).
NODE_Z = {
    ("e0", 1): 13.15, ("e0", 3): 13.2, ("e1", 3): 11.5, ("e2", 3): 9.3, ("e2", 2): 8.95, ("e2", 1): 9.2,
    ("e1", 4): 11.6, ("e0", 4): 13.3, ("m2", 2): 2.75, ("m0", 2): 5.35, ("m0", 3): 5.3,
}
# Relief: (dx, dy, dz) of a lattice node from where the row's plane puts it, in cm (dy forward). Left side's j >= 0.
RELIEF = {
    # forehead, brow ridge, glabella
    ("e0", 0): (0, 0.35, 0), ("e0", 1): (0, 0.75, 0), ("e0", 2): (0, 0.80, 0), ("e0", 3): (0, 0.45, 0),
    ("e0", 4): (0, -0.10, 0), ("f1", 0): (0, 0.05, 0), ("f1", 1): (0, 0.08, 0), ("f1", 2): (0, 0.12, 0),
    # the eye's orbit (ring C)
    ("e1", 0): (0, -0.25, 0), ("e1", 1): (0, -0.60, 0), ("e1", 3): (0, 0.45, 0), ("e2", 3): (0, 0.95, 0),
    ("e2", 2): (0, 0.70, 0), ("e2", 1): (0, 0.4, 0), ("e2", 0): (0, 1.7, 0), ("e1", 4): (0, -0.05, 0),
    # the nose: bridge, dorsum, tip, wings, base, columella
    ("nb", 0): (0, 3.0, 0), ("nb", 1): (0, 2.4, 0), ("nb", 2): (0, 0.60, 0), ("nb", 3): (0, 0.55, 0),
    ("tip", 0): (0, 3.6, 0), ("tip", 1): (0, 2.9, 0), ("tip", 2): (0, 2.2, 0), ("tip", 3): (0, 0.5, 0),
    ("sub", 0): (0, 0.9, 0), ("sub", 1): (0, 0.7, 0), ("sub", 2): (0, 0.25, 0), ("sub", 3): (0, 0.5, 0),
    ("e2", 4): (0, 0.0, 0),
    # the cheek, the groove beside the nose and mouth, the jaw
    ("m0", 0): (0, 0.0, 0), ("m0", 1): (0, 0.25, 0), ("m0", 2): (0, -0.30, 0), ("m0", 3): (0, 0.5, 0),
    ("m1", 2): (0, -0.40, 0), ("m1", 3): (0, 0.4, 0),
    ("m2", 0): (0, -0.90, 0), ("m2", 1): (0, -0.80, 0), ("m2", 2): (0, -0.35, 0), ("m2", 3): (0, 0.25, 0),
    ("chin", 0): (0, 0.60, 0), ("chin", 1): (0, 0.40, 0), ("chin", 2): (0, 0.15, 0),
    ("men", 0): (0, 0.10, 0), ("men", 1): (0, 0.0, 0),
    ("ju", 0): (0, 0.0, 0),
    ("n4", 4): (0, 0.35, 0), ("n4", 3): (0, 0.20, 0), ("n7", 4): (0, 0.20, 0),
}

# Loops written as (x, z, dy-from-plane) per node for the left side. The eye ring order: inner mid, inner top, top,
# outer top, outer mid, outer bottom, bottom, inner bottom (the same order as ring C's lattice nodes).
EYE_B = [(1.35, 11.25), (1.65, 12.4), (3.1, 12.6), (4.8, 12.2), (5.0, 11.4), (4.6, 10.1), (3.1, 9.9), (1.65, 10.1)]
EYE_B_DY = [-0.85, -0.70, 0.10, -0.75, -0.95, -0.60, -0.15, -0.55]       # from the cornea's pole
EYE_A = [(1.65, 11.2), (2.15, 11.79), (3.1, 11.86), (4.1, 11.72), (4.55, 11.4), (4.15, 10.72), (3.1, 10.64), (2.15, 10.73)]
LID_SPHERE_R = 1.42
POLE_OFFSET = 0.35                       # the cornea's pole in front of the face's plane at the eye, cm
# Mouth loops, left half: top centre, top 1, top 2, corner, bottom 2, bottom 1, bottom centre; (x, z, dy).
MOUTH_L1 = [(0.0, 4.95, 0.62), (0.95, 5.1, 0.60), (2.0, 4.65, 0.34), (2.85, 4.10, 0.05), (2.0, 3.45, 0.38),
            (1.0, 3.05, 0.68), (0.0, 3.0, 0.72)]
MOUTH_L0 = [(0.0, 4.17, 0.38), (1.0, 4.19, 0.36), (2.0, 4.13, 0.2), (2.55, 4.08, -0.05), (2.0, 4.05, 0.2),
            (1.0, 4.02, 0.38), (0.0, 4.03, 0.40)]
MOUTH_POCKET = 0.9
MOUTH_L2 = [("m0", 0), ("m0", 1), ("m0", 2), ("m1", 2), ("m2", 2), ("m2", 1), ("m2", 0)]
NOSTRIL_INSET = 0.58
NOSTRIL_DEPTH = 0.75


def _interp_rows(z):
    """(W, yF, yB, p) of the plane at height z: linear between the rows of ROW_TABLE and the dome above f1."""
    names = [n for n in ROW_NAMES if n in ROW_TABLE]
    zs = np.array([ROW_TABLE[n][0] for n in names])
    cols = np.array([ROW_TABLE[n][1:] for n in names])
    return tuple(float(np.interp(z, zs, cols[:, k])) for k in range(4))


def row_params(name):
    """(z, W, yF, yB, p) of a lattice row, the dome rows scaled from f1."""
    if name in ROW_TABLE:
        return ROW_TABLE[name]
    z = DOME[name]
    z0, w, yf, yb, p = ROW_TABLE["f1"]
    yc = 0.5 * (yf + yb)
    s = math.sqrt(max(1.0 - ((z - 15.0) / (APEX_Z - 15.0)) ** 1.9, 0.0))
    return (z, w * s, yc + (yf - yc) * s, yc + (yb - yc) * s, p)


def plane_y(x, z):
    """yf of the front of the face's plane at (x, z) (cm), the superellipse of the row's ring."""
    if z > ROW_TABLE["f1"][0]:
        # above the forehead: the dome
        rows = [row_params(n) for n in ("f1", "f2", "f3", "t1", "t2")]
        zs = np.array([r[0] for r in rows])
        w = float(np.interp(z, zs, [r[1] for r in rows]))
        yf = float(np.interp(z, zs, [r[2] for r in rows]))
        yb = float(np.interp(z, zs, [r[3] for r in rows]))
        p = rows[0][4]
    else:
        w, yf, yb, p = _interp_rows(z)
    yc = 0.5 * (yf + yb)
    f = yf - yc
    t = min(abs(x) / max(w, 1e-6), 0.999)
    return yc + f * (1.0 - t ** p) ** (1.0 / p)


def _ring_point(name, j):
    """(x, yf, z) of lattice node (row, j) on the row's ring, before relief."""
    z, w, yf, yb, p = row_params(name)
    yc = 0.5 * (yf + yb)
    f, b = yf - yc, yc - yb
    if j == 0:
        return 0.0, yf, z
    if j == 8:
        return 0.0, yb, z
    if j <= 4:
        if name in ROW_X:
            x = ROW_X[name][j - 1]
        else:
            x = w * COLUMN_FRACTION[j]
        t = min(abs(x) / max(w, 1e-6), 0.999)
        return x, yc + f * (1.0 - t ** p) ** (1.0 / p), z
    a = math.radians(BACK_ANGLES[j])
    e = 2.0 / p
    x = w * abs(math.sin(a)) ** e
    c = math.cos(a)
    y = yc + f * c ** e if c >= 0 else yc - b * abs(c) ** e
    return x, y, z


class Cage:
    """Vertices (an (n, 3) array, metres, Blender frame), faces (tuples of vertex indices, wound outward), a UV per
    face corner, creases per edge, and named vertex groups (weights per vertex)."""

    def __init__(self):
        self.verts = []
        self.index = {}
        self.faces = []
        self.uv_face = []        # per face: list of (u, v), same order as the face's corners
        self.creases = {}        # (a, b) sorted -> 0..1
        self.groups = {}
        self.names = []
        self.eye_info = []

    def vid(self, key, xyz):
        if key in self.index:
            return self.index[key]
        self.index[key] = len(self.verts)
        self.verts.append(tuple(xyz))
        self.names.append(key)
        return self.index[key]


def lattice_key(r, j, s):
    return ("L", r, j, 1 if j in (0, 8) else s)


def build_half_positions():
    """{(kind keys): (x, yf, z)} for every left-side vertex of the cage, in cm."""
    P = {}
    for name in ROW_NAMES:
        r = R[name]
        for j in range(0, 9):
            x, y, z = _ring_point(name, j)
            if (name, j) in NODE_Z:
                z = NODE_Z[(name, j)]
                # keep the node on the row's surface at its new height
                zz, w, yf, yb, p = row_params(name)
                if j <= 4 and j != 0:
                    t = min(abs(x) / max(w, 1e-6), 0.999)
                    yc = 0.5 * (yf + yb)
                    y = yc + (yf - yc) * (1.0 - t ** p) ** (1.0 / p)
            dx, dy, dz = RELIEF.get((name, j), (0, 0, 0))
            P[("L", r, j)] = (x + dx, y + dy, z + dz)
    # the eye block's hand-placed ring (the orbit): absolute x, z (ROW_X, NODE_Z) with the relief above.
    return P


class Builder:
    def __init__(self, face=None):
        self.cage = Cage()
        self.P = build_half_positions()
        self.face = face
        self.eye_c = np.array([EYE_X, EYE_Z])
        self.uvs = {}

    # --- vertices -------------------------------------------------------------------------------------------
    def put(self, key, pos, side):
        x, y, z = pos
        key = key + (1 if (abs(x) < 1e-9) else side,)
        return self.cage.vid(key, (x * side if abs(x) > 1e-9 else 0.0, -y, z))

    def lat(self, r, j, side=1):
        key = ("L", r, j)
        x, y, z = self.P[key]
        return self.put(key, (x, y, z), side if j not in (0, 8) else 1)

    def add_face(self, ids, uvs=None):
        self.cage.faces.append(tuple(ids))
        self.cage.uv_face.append(uvs)

    def crease(self, a, b, w):
        self.cage.creases[(min(a, b), max(a, b))] = w

    def build(self):
        c = self.cage
        eye_cells = {(R["e2"], 1), (R["e2"], 2), (R["e1"], 1), (R["e1"], 2)}
        mouth_cells = {(R["m2"], 0), (R["m2"], 1), (R["m1"], 0), (R["m1"], 1)}
        nostril_cell = (R["sub"], 1)
        removed = {(R["e1"], 2), (R["m1"], 0), (R["m1"], 1)}
        # lattice vertices (both sides)
        for r in range(len(ROW_NAMES)):
            for j in range(0, 9):
                if (r, j) in removed:
                    continue
                for s in (1, -1):
                    if j in (0, 8) and s == -1:
                        continue
                    self.lat(r, j, s)
        # lattice faces
        for s in (1, -1):
            for r in range(len(ROW_NAMES) - 1):
                for j in range(8):
                    if (r, j) in eye_cells or (r, j) == nostril_cell or (r, j) in mouth_cells:
                        continue
                    ids = [self.lat(r, j, s), self.lat(r, j + 1, s), self.lat(r + 1, j + 1, s), self.lat(r + 1, j, s)]
                    if s == -1:
                        ids = ids[::-1]
                    self.add_face(ids)
        self.build_crown()
        for s in (1, -1):
            self.build_eye(s)
            self.build_nostril(s)
        self.build_mouth()
        return c

    # --- the crown ---------------------------------------------------------------------------------------------
    def build_crown(self):
        r = len(ROW_NAMES) - 1
        z, w, yf, yb, p = row_params("t2")
        yc = 0.5 * (yf + yb)
        apex = self.cage.vid(("apex",), (0.0, -yc, APEX_Z))
        ring = [self.lat(r, j, 1) for j in range(0, 9)] + [self.lat(r, j, -1) for j in range(7, 0, -1)]
        for k in range(len(ring)):
            self.add_face((ring[k], ring[(k + 1) % len(ring)], apex))
        # the neck's base is closed (inside the collar; the balaclava's voxel shell needs a closed head)
        base = [self.lat(0, j, 1) for j in range(0, 9)] + [self.lat(0, j, -1) for j in range(7, 0, -1)]
        self.add_face(tuple(base[::-1]))

    # --- the eye -----------------------------------------------------------------------------------------------
    def eye_ring_c(self):
        e0, e1, e2 = R["e0"], R["e1"], R["e2"]
        return [(e1, 1), (e0, 1), (e0, 2), (e0, 3), (e1, 3), (e2, 3), (e2, 2), (e2, 1)]

    def eye_pole(self):
        """yf of the cornea's pole: in front of the face's plane at the iris."""
        return plane_y(EYE_X, EYE_Z) + POLE_OFFSET

    def build_eye(self, s):
        cage = self.cage
        pole = self.eye_pole()
        yb = pole - EYE_R
        cc = [self.lat(r, j, s) for r, j in self.eye_ring_c()]
        b_ids, a_ids, ain_ids = [], [], []
        for k, (x, z) in enumerate(EYE_B):
            b_ids.append(self.put(("B", k), (x, pole + EYE_B_DY[k], z), s))
        for k, (x, z) in enumerate(EYE_A):
            dx, dz = x - EYE_X, z - EYE_Z
            y = yb + math.sqrt(max(LID_SPHERE_R ** 2 - dx * dx - dz * dz, 0.12))
            a_ids.append(self.put(("A", k), (x, y, z), s))
        for k, (x, z) in enumerate(EYE_A):
            dx, dz = x - EYE_X, z - EYE_Z
            y = yb + math.sqrt(max(LID_SPHERE_R ** 2 - dx * dx - dz * dz, 0.12)) - 0.55
            a_ids_in = self.put(("Ain", k), (EYE_X + dx * 0.82, y, EYE_Z + dz * 0.82), s)
            ain_ids.append(a_ids_in)
        front = np.array([0.0, -1.0, 0.0])
        self.bridge(cc, b_ids, front)
        self.bridge(b_ids, a_ids, front)
        self.bridge(a_ids, ain_ids, None, interior=True)
        self.cap(ain_ids, front)
        for k in range(8):
            self.crease(a_ids[k], a_ids[(k + 1) % 8], 0.9)
            self.crease(ain_ids[k], ain_ids[(k + 1) % 8], 0.5)
        cage.eye_info.append((s, pole, yb))

    # --- the mouth ---------------------------------------------------------------------------------------------
    def mouth_ring(self, prefix, spec=None, pocket=0.0):
        """The 12 vertices round the mouth (top centre, then round the left side, bottom centre, the right side)."""
        names = ["t0", "t1", "t2", "c", "b2", "b1", "b0"]
        ids = []
        order = [(0, 1), (1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (5, -1), (4, -1), (3, -1), (2, -1), (1, -1)]
        for k, s in order:
            if prefix == "L2":
                row, j = MOUTH_L2[k]
                ids.append(self.lat(R[row], j, s))
            else:
                x, z, dy = spec[k]
                y = plane_y(x, z) + dy - pocket
                zc = MOUTH_Z + (z - MOUTH_Z) * (0.12 if pocket else 1.0)
                ids.append(self.put((prefix, names[k]), (x, y, zc), s))
        return ids

    def build_mouth(self):
        front = np.array([0.0, -1.0, 0.0])
        l2 = self.mouth_ring("L2")
        l1 = self.mouth_ring("L1", MOUTH_L1)
        l0 = self.mouth_ring("L0", MOUTH_L0)
        l0b = self.mouth_ring("L0b", MOUTH_L0, pocket=MOUTH_POCKET)
        self.bridge(l2, l1, front)
        self.bridge(l1, l0, front)
        self.bridge(l0, l0b, None, interior=True)
        self.cap(l0b, front)
        for k in range(12):
            self.crease(l0[k], l0[(k + 1) % 12], 1.0)
            self.crease(l1[k], l1[(k + 1) % 12], 0.45)
            self.crease(l0b[k], l0b[(k + 1) % 12], 0.5)

    # --- the nostrils --------------------------------------------------------------------------------------------
    def build_nostril(self, s):
        corners = [(R["tip"], 1), (R["tip"], 2), (R["sub"], 2), (R["sub"], 1)]
        ids = [self.lat(r, j, s) for r, j in corners]
        pts = np.array([self.cage.verts[i] for i in ids])
        centre = pts.mean(axis=0)
        n = np.cross(pts[1] - pts[0], pts[3] - pts[0])
        n = n / np.linalg.norm(n)
        if n[1] > 0:
            n = -n                                    # pointing out of the nose (front/down)
        inset = [self.cage.vid(("N1", k, s), centre + (p - centre) * NOSTRIL_INSET) for k, p in enumerate(pts)]
        pocket = [self.cage.vid(("N2", k, s), centre + (p - centre) * NOSTRIL_INSET * 0.8 - n * NOSTRIL_DEPTH)
                  for k, p in enumerate(pts)]
        self.bridge(ids, inset, n)
        self.bridge(inset, pocket, None, interior=True)
        self.cap(pocket, n)
        for k in range(4):
            self.crease(inset[k], inset[(k + 1) % 4], 0.6)
            self.crease(pocket[k], pocket[(k + 1) % 4], 0.5)

    # --- helpers ------------------------------------------------------------------------------------------------
    def normal_of(self, ids):
        p = np.array([self.cage.verts[i] for i in ids])
        n = np.zeros(3)
        for k in range(len(p)):
            a, b = p[k], p[(k + 1) % len(p)]
            n += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])])
        return n

    def bridge(self, a, b, toward, interior=False):
        """Quads between two loops of equal length (same order round), all wound the same way: toward `toward` (a
        direction), or, for an interior wall, toward the loops' common axis (decided by the majority of the quads)."""
        n = len(a)
        pa = np.array([self.cage.verts[i] for i in a])
        pb = np.array([self.cage.verts[i] for i in b])
        axis = 0.5 * (pa.mean(axis=0) + pb.mean(axis=0))
        quads = [[a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]] for k in range(n)]
        score = 0.0
        for k, ids in enumerate(quads):
            nrm = self.normal_of(ids)
            nrm = nrm / max(np.linalg.norm(nrm), 1e-12)
            if interior:
                centroid = np.array([self.cage.verts[i] for i in ids]).mean(axis=0)
                target = axis - centroid
                tangent = pa[(k + 1) % n] - pa[k]
                tangent /= max(np.linalg.norm(tangent), 1e-9)
                target = target - float(np.dot(target, tangent)) * tangent
                target /= max(np.linalg.norm(target), 1e-9)
            else:
                target = toward
            score += float(np.dot(nrm, target))
        for ids in quads:
            self.add_face(ids[::-1] if score < 0 else ids)

    def cap(self, loop, toward):
        n = self.normal_of(loop)
        ids = list(loop)
        if float(np.dot(n, toward)) < 0:
            ids = ids[::-1]
        self.add_face(ids)


# --------------------------------------------------------------------------------------------------
# landmark-driven deformation (looks.FaceSpec)
# --------------------------------------------------------------------------------------------------


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def falloff(p, centre, radius, core=0.4):
    """1 within `core` of the radius from the centre, falling smoothly to 0 at `radius` (cm), on the 3D distance (x
    mirrored)."""
    d = np.sqrt((np.abs(p[:, 0]) - abs(centre[0])) ** 2 + (p[:, 1] - centre[1]) ** 2 + (p[:, 2] - centre[2]) ** 2)
    return 1.0 - smoothstep(core * radius, radius, d)


def _w_eye(p, centre_x, size=3.4):
    """Weight (0..1) of the eye's own region: 1 within ~1.7 cm of the iris centre in x and z, 0 beyond `size`."""
    d = np.sqrt((np.abs(p[:, 0]) - centre_x) ** 2 + (p[:, 2] - EYE_Z) ** 2)
    return 1.0 - smoothstep(1.7, size, d)


def deform(face, V, extra, neck_y=0.03):
    """Landmark-driven deformation of the cage. V: (n, 3) vertices in cm in the cage's frame (x, y_blender, z; the
    front is -y); extra: (m, 3) points that follow (the eyeballs' centres). Returns (V, extra, ball_radius_cm).
    A named landmark moves a neighbourhood of vertices (smooth falloff); nothing is a height field."""
    f = face
    P = np.concatenate([V, extra], axis=0).astype(np.float64)
    x = P[:, 0].copy()
    yf = -P[:, 1]
    z = P[:, 2].copy()
    yc0 = -1.0

    def pts():
        return np.stack([x, yf, z], axis=1)

    # the skull's width and length
    above = smoothstep(2.0, 9.0, z)
    x *= 1.0 + (f.head_width - 1.0) * (0.35 + 0.65 * above)
    yf = yc0 + (yf - yc0) * (1.0 + (f.head_depth - 1.0) * smoothstep(0.0, 6.0, z))
    # the jaw: its angle's width, squareness, and the chin's size and length
    wj = smoothstep(-2.0, 1.2, z) * (1.0 - smoothstep(5.5, 9.5, z))
    x *= 1.0 + (f.jaw_width - 1.0) * 0.95 * wj
    sq = f.jaw_square - 0.4
    low = (1.0 - smoothstep(2.5, 6.0, z)) * smoothstep(-1.4, 0.8, z)
    x *= 1.0 + 0.10 * sq * low * smoothstep(1.0, 3.5, np.abs(x))
    chin_c = falloff(pts(), (0.0, 7.6, 1.0), 4.6)
    yf += (0.85 * (f.chin - 1.0) + 0.35 * sq) * chin_c
    z -= 0.80 * (f.chin - 1.0) * chin_c * (1.0 - smoothstep(1.0, 3.2, z))
    # the brow ridge and the temples' brow bone
    brow = falloff(pts(), (3.0, 9.0, BROW_Z), 4.2)
    yf += 0.55 * (f.brow - 0.6) * brow
    z -= 0.10 * (f.brow - 0.6) * brow
    # the eyes: spacing, size, how deep they sit
    ex = EYE_X
    w_eye = _w_eye(pts(), ex, 3.6)
    x += np.sign(x) * (f.eye_spacing - 1.0) * ex * w_eye
    ex_s = ex * f.eye_spacing
    w_core = _w_eye(pts(), ex_s, 3.0)
    sgn = np.sign(x)
    x = sgn * (ex_s + (np.abs(x) - ex_s) * (1.0 + (f.eye_size - 1.0) * w_core))
    z = EYE_Z + (z - EYE_Z) * (1.0 + (f.eye_size - 1.0) * w_core)
    yf -= 0.30 * (f.eye_sink - 1.0) * _w_eye(pts(), ex_s, 3.0)
    # the cheekbone and the hollow beneath it
    cheek = falloff(pts(), (4.8, 7.6, 8.4), 2.9)
    yf += 0.60 * (f.cheek - 1.0) * cheek
    x += np.sign(x) * 0.18 * (f.cheek - 1.0) * cheek
    hollow = falloff(pts(), (4.7, 6.2, 4.6), 2.8)
    x -= np.sign(x) * 0.45 * (f.cheek_hollow - 0.3) * hollow
    yf -= 0.50 * (f.cheek_hollow - 0.3) * hollow
    # the nose: how far the tip stands out, the width of the wings, the bridge's hump or dip
    nose = falloff(pts(), (0.0, 10.0, 7.2), 3.4)
    yf += 2.8 * (f.nose_proj - 1.0) * nose
    z -= 0.35 * (f.nose_proj - 1.0) * nose
    wing = falloff(pts(), (0.0, 9.0, 6.9), 3.3)
    x *= 1.0 + (f.nose_width - 1.0) * 0.95 * wing
    bridge = falloff(pts(), (0.0, 9.0, 9.6), 2.8)
    yf += 0.60 * f.nose_bridge * bridge
    # the lips: fullness (thickness and forward), the mouth's width
    mouth = falloff(pts(), (0.0, 8.5, MOUTH_Z), 4.2)
    z = MOUTH_Z + (z - MOUTH_Z) * (1.0 + (f.lips - 1.0) * 0.55 * mouth)
    yf += 0.40 * (f.lips - 1.0) * mouth
    mw = falloff(pts(), (0.0, 8.0, MOUTH_Z), 6.0)
    x *= 1.0 + (f.mouth_width - 1.0) * mw
    # age: bags under the eyes
    bags = falloff(pts(), (3.0, 8.6, EYE_Z - 1.3), 1.6)
    yf -= 0.22 * f.age * bags
    # the neck: its width against the jaw, and where the body's neck is
    nr = f.neck_radius
    nk = 1.0 - smoothstep(-6.0, 0.0, z)       # 1 on the neck, 0 at the chin
    neck_c = -(neck_y * 100.0)                # yf of the neck's centre
    scale = nr / 0.058
    x *= 1.0 + (scale - 1.0) * nk
    yf = neck_c + (yf - neck_c) * (1.0 + (scale - 1.0) * nk)
    yf += (neck_c + 3.0) * smoothstep(0.5, -4.0, z)
    P = np.stack([x, -yf, z], axis=1)
    n = len(V)
    return P[:n], P[n:], EYE_R * f.eye_size


def row_v(r):
    z = row_params(ROW_NAMES[r])[0]
    return V_HEAD * (z - V_MIN) / (APEX_Z - V_MIN)


class CageData:
    """The cage in world metres (after `build_cage`)."""

    def __init__(self):
        self.verts = None
        self.faces = None
        self.uvs = None           # per face corner, (sum of corners, 2), in face order
        self.creases = []         # (a, b, weight)
        self.groups = {}
        self.eyes = []            # [(centre (3,), radius)] left (+X) then right
        self.names = []


def build_cage(face, neck_y=0.03):
    """The cage for a FaceSpec: CageData (metres, the body's frame, the menton at CHIN_Z), deformed by the spec."""
    b = Builder(face)
    cage = b.build()
    V = np.array(cage.verts, np.float64)
    pole = b.eye_pole()
    extra = np.array([[EYE_X, -pole + EYE_R, EYE_Z], [-EYE_X, -pole + EYE_R, EYE_Z]], np.float64)
    V, extra, ball_r = deform(face, V, extra, neck_y)
    out = CageData()
    out.verts = np.array(V) * 0.01 + np.array([0.0, 0.0, CHIN_Z])
    out.faces = [tuple(f) for f in cage.faces]
    out.creases = [(a, c, w) for (a, c), w in cage.creases.items()]
    out.eyes = [(np.array(e) * 0.01 + np.array([0.0, 0.0, CHIN_Z]), ball_r * 0.01) for e in extra]
    out.uvs = cage_uvs(cage, V)
    out.groups = cage_groups(cage)
    out.names = cage.names
    out.ear_uv = ear_uv_info(cage, V)
    return out


def cage_uvs(cage, V):
    """A UV per face corner: u round the head (front midline at 0.5, back midline the seam at 0 and 1), v up it; the
    eye's, mouth's and nostrils' inner loops by the (x, z) plane mapped through their surrounding loop."""
    idx = cage.index
    e2 = R["e2"]
    pts = np.array([V[idx[("L", e2, j, 1)]] for j in range(9)])
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    arc = np.concatenate([[0.0], np.cumsum(seg)])
    col_u = 0.5 * arc / arc[-1]

    def lat_uv(r, j, side):
        if j == 0:
            return 0.5, row_v(r)
        if j == 8:
            return (1.0 if side > 0 else 0.0), row_v(r)
        return 0.5 + side * col_u[j], row_v(r)

    def fit(keys):
        A, T = [], []
        for k in keys:
            p = V[idx[k]]
            A.append([p[0], p[2], 1.0])
            T.append(lat_uv(k[1], k[2], k[3]))
        M, *_ = np.linalg.lstsq(np.array(A), np.array(T), rcond=None)
        return M

    maps = {}
    for s in (1, -1):
        maps[("eye", s)] = fit([("L", r, j, s) for r, j in [(R["e1"], 1), (R["e0"], 1), (R["e0"], 2), (R["e0"], 3),
                                                            (R["e1"], 3), (R["e2"], 3), (R["e2"], 2), (R["e2"], 1)]])
    mouth_keys = []
    for k, s in [(0, 1), (1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (5, -1), (4, -1), (3, -1), (2, -1), (1, -1)]:
        row, j = MOUTH_L2[k]
        mouth_keys.append(("L", R[row], j, 1 if j == 0 else s))
    maps["mouth"] = fit(mouth_keys)
    corners = [(R["tip"], 1), (R["tip"], 2), (R["sub"], 2), (R["sub"], 1)]

    def key_uv(key, face_side):
        kind = key[0]
        p = V[idx[key]]
        if kind == "L":
            return lat_uv(key[1], key[2], key[3] if key[2] not in (0, 8) else face_side)
        if kind == "apex":
            return None
        if kind in ("N1", "N2"):
            s = key[2]
            cuv = np.array([lat_uv(r, j, s) for r, j in corners])
            cu = cuv.mean(axis=0)
            return tuple(cu + (cuv[key[1]] - cu) * NOSTRIL_INSET)
        if kind in ("A", "B", "Ain"):
            m = maps[("eye", key[2])]
        else:
            m = maps["mouth"]
        u, v = np.array([p[0], p[2], 1.0]) @ m
        return float(u), float(v)

    uvs = []
    for face in cage.faces:
        cx = float(np.mean([V[i][0] for i in face]))
        side = 1 if cx >= 0 else -1
        corner_uv = [key_uv(cage.names[i], side) for i in face]
        if any(c is None for c in corner_uv):
            mid = float(np.mean([c[0] for c in corner_uv if c is not None]))
            corner_uv = [c if c is not None else (mid, V_HEAD) for c in corner_uv]
        uvs.extend(corner_uv)
    return np.array(uvs, np.float64)


def cage_groups(cage):
    """Vertex groups (weights per cage vertex): 'lips' (the lip loops), 'inner' (pockets and cavities, drawn dark),
    'ridge' (the brow ridge's row)."""
    n = len(cage.verts)
    groups = {"lips": np.zeros(n), "inner": np.zeros(n), "ridge": np.zeros(n), "lash": np.zeros(n)}
    for i, key in enumerate(cage.names):
        kind = key[0]
        if kind in ("L1", "L0"):
            groups["lips"][i] = 1.0
        if kind == "A":
            groups["lash"][i] = 1.0
        if kind in ("L0b", "N2", "Ain"):
            groups["inner"][i] = 1.0
        if kind == "N1":
            groups["inner"][i] = 0.7
        if kind == "L" and key[1] == R["e0"] and key[2] in (1, 2, 3):
            groups["ridge"][i] = 1.0
    return groups


# --------------------------------------------------------------------------------------------------
# ears
# --------------------------------------------------------------------------------------------------

EAR_NU = 11            # rows up the ear (lobe to the helix's top)
EAR_NV = 8             # columns across (the root to the helix's rim)
EAR_WIDTH = 3.9        # cm at ear_size 1
EAR_LENGTH = 6.2       # cm at ear_size 1
EAR_STRIP_U = (0.06, 0.56)   # where the left and right ears' outer sheets start in u
EAR_STRIP_V = 0.885         # and in v
EAR_RELIEF = 2.1       # the ear's relief over its plate, times the table's numbers


def _gauss2(v, u, v0, u0, sv, su):
    return np.exp(-(((v - v0) / sv) ** 2 + ((u - u0) / su) ** 2))


def ear_relief(u, v):
    """Height (cm) of the ear's outer face over its plate at (u up the ear, v across from the root): the helix's rim,
    the scapha, the antihelix's ridge and its fork, the concha's bowl, the tragus and antitragus, the lobe."""
    edge = np.minimum(1.0 - v, np.minimum(u * 1.3, (1.0 - u) * 1.3))
    helix = 0.42 * np.exp(-(((edge - 0.075) / 0.055) ** 2)) * (u > 0.10)
    scapha = -0.10 * np.exp(-(((edge - 0.22) / 0.07) ** 2)) * smoothstep(0.2, 0.4, u)
    ridge_v = 0.55 + 0.18 * (0.62 - u)
    anti = 0.24 * np.exp(-(((v - ridge_v) / 0.075) ** 2)) * smoothstep(0.26, 0.40, u) * (1.0 - smoothstep(0.86, 0.99, u))
    fork = 0.16 * np.exp(-(((v - (0.40 + 0.5 * (u - 0.86))) / 0.07) ** 2)) * smoothstep(0.80, 0.9, u)
    concha = -0.62 * _gauss2(v, u, 0.30, 0.52, 0.17, 0.15)
    tragus = 0.52 * _gauss2(v, u, 0.07, 0.52, 0.075, 0.075)
    antitragus = 0.34 * _gauss2(v, u, 0.12, 0.34, 0.085, 0.060)
    lobe = 0.20 * _gauss2(v, u, 0.30, 0.10, 0.28, 0.14)
    return EAR_RELIEF * (helix + scapha + anti + fork + concha + tragus + antitragus + lobe)


def ear_thickness(u, v):
    """The plate's thickness behind the outer face (cm): a thin rim, a fleshy lobe, the concha's wall."""
    edge = np.minimum(1.0 - v, np.minimum(u * 1.3, (1.0 - u) * 1.3))
    return 0.42 + 0.50 * smoothstep(0.0, 0.25, edge) + 0.30 * np.exp(-(((u - 0.10) / 0.12) ** 2))


def ear_cage(face, anchor, side, uv_anchor, uv_per_cm, anchor_rows=None):
    """The ear's cage: (verts (m, 3) metres, faces, corner_uv, creases). `anchor` is the root's front edge at the
    ear's middle height (metres, on the skull's side); the ear stands off by `ear_flare` degrees, is `ear_size` times
    the neutral ear, and leans back at its top. UVs map into the head's own texture around the ear (the bake paints it
    there): `uv_anchor` the head UV of the anchor, `uv_per_cm` the head's du per cm."""
    es = face.ear_size
    phi = math.radians(face.ear_flare)
    ax, ay, az = (float(a) * 100.0 for a in anchor)
    rows_x = None if anchor_rows is None else np.asarray(anchor_rows, np.float64) * 100.0
    L = EAR_LENGTH * es
    W = EAR_WIDTH * es
    t = np.array([side * math.sin(phi), math.cos(phi)])
    nrm = np.array([side * math.cos(phi), -math.sin(phi)])
    us = np.linspace(0.0, 1.0, EAR_NU)
    vs = np.linspace(0.0, 1.0, EAR_NV)
    width = np.interp(us, [0.0, 0.07, 0.2, 0.4, 0.6, 0.8, 0.93, 1.0], [0.25, 0.60, 0.80, 0.96, 1.0, 0.98, 0.88, 0.45])
    verts, uv = [], []
    for sheet in (0, 1):
        for i, u in enumerate(us):
            for j, v in enumerate(vs):
                d = float(ear_relief(u, v))
                if sheet == 1:
                    d = -float(ear_thickness(u, v)) + 0.18 * float(ear_relief(u, v))
                z = az + (u - 0.52) * L
                lean = 0.5 * (u - 0.5) * es              # the top leans back
                x0 = ax if rows_x is None else float(rows_x[i])
                x = x0 + t[0] * v * W * width[i] + nrm[0] * d - side * 0.9 * (1.0 - v)
                y = ay + t[1] * v * W * width[i] + nrm[1] * d + lean
                verts.append((x, y, z))
                # a strip of the texture above the head's: this ear's outer sheet, then its back sheet beside it
                u0 = EAR_STRIP_U[0 if side > 0 else 1] + (0.14 if sheet else 0.0)
                uv.append((u0 + v * 0.10 * width[i], EAR_STRIP_V + u * 0.10))
    n = EAR_NU * EAR_NV

    def vi(sheet, i, j):
        return sheet * n + i * EAR_NV + j

    faces, corner_uv = [], []

    def add(ids):
        faces.append(tuple(ids))
        corner_uv.extend(uv[i] for i in ids)

    for i in range(EAR_NU - 1):
        for j in range(EAR_NV - 1):
            front = [vi(0, i, j), vi(0, i, j + 1), vi(0, i + 1, j + 1), vi(0, i + 1, j)]
            back = [vi(1, i, j), vi(1, i + 1, j), vi(1, i + 1, j + 1), vi(1, i, j + 1)]
            add(front if side > 0 else front[::-1])
            add(back if side > 0 else back[::-1])
    # the rim wall joins the sheets round the boundary
    loop = [(i, 0) for i in range(EAR_NU)] + [(EAR_NU - 1, j) for j in range(1, EAR_NV)] \
        + [(i, EAR_NV - 1) for i in range(EAR_NU - 2, -1, -1)] + [(0, j) for j in range(EAR_NV - 2, 0, -1)]
    for k in range(len(loop)):
        a, b = loop[k], loop[(k + 1) % len(loop)]
        quad = [vi(0, *a), vi(1, *a), vi(1, *b), vi(0, *b)]
        quad = quad if side > 0 else quad[::-1]
        faces.append(tuple(quad))
        corner_uv.extend(uv[vi(0, *a)] if q in (vi(0, *a), vi(1, *a)) else uv[vi(0, *b)] for q in quad)
    out = np.array(verts, np.float64)
    out = np.stack([out[:, 0] * 0.01, out[:, 1] * 0.01, out[:, 2] * 0.01], axis=1)
    return out, faces, np.array(corner_uv, np.float64)


def ear_uv_info(cage, V):
    """(u of the side column, du per cm round the head) for the ears' UVs."""
    idx = cage.index
    e2 = R["e2"]
    pts = np.array([V[idx[("L", e2, j, 1)]] for j in range(9)])
    arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    return 0.5 + 0.5 * arc[5] / arc[-1], 0.5 / arc[-1]
