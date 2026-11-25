"""The enemies' head as numbers: a sculpt-by-script base head (skull, brow, nose, lips, chin, jaw, neck, ears, brows,
hair and headwear shells), from a `looks.FaceSpec`.

numpy only, no bpy, so it runs (and is tested) outside Blender: `python Tools/Blender/test_headshape.py`.
`headbuild.py` turns these arrays into Blender objects. Everything is deterministic.

Frame: the Blender frame the body stands in (X his left, -Y his front, Z up, metres). The head is a structured grid:
`ROWS` rings from the neck's base up to the crown (denser over the face), `SEGMENTS` points round each (denser at the
front), then an apex vertex at the crown. A ring is a superellipse with its own half width, front and back depth,
centre and squareness, so a ring set is the skull, the jaw, the chin and the neck in one surface and a face is
heights painted on it (`displacement`): brow ridge, sockets, cheekbones, nose, lips, chin. Being a grid, the hairline,
a cap's edge and the UVs (u round the head, v up it, seam at the back) are just indices.

Landmarks follow ordinary adult proportions (eyes at half the head's height, nose base a third of the way up from
the chin, mouth a quarter, brow ridge at 0.585, ears between brow and nose base, the head 22.5 cm from chin to crown on
a 181 cm man). No landmark or number comes from a person.
"""

import math

import numpy as np

CHIN_Z = 1.585          # chin's lowest point (the pack's head box starts at 1.58)
HEAD_H = 0.225          # chin to crown
CROWN_Z = CHIN_Z + HEAD_H
NECK_BASE_Z = 1.44      # the neck tube starts here, inside the collar
SEGMENTS = 96
NECK_ROWS = 10
FACE_ROWS = 58          # head rows from the chin up to T_DENSE
UPPER_ROWS = 22         # from T_DENSE to T_TOP
T_DENSE = 0.64
T_TOP = 0.992
T_CAP = 0.74            # above this the rings close like a dome
NECK_Y = 0.030          # where the neck's centre is, absent a measurement
SOCKET_DEPTH = 0.0055    # the eye socket's dip below the face plane at eye_sink 1 (metres)
EYE_SET_BACK = -0.0035  # how far the eyeball's front stands in front of the socket's floor (metres)


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def gauss(dx, dz, sx, sz):
    return np.exp(-0.5 * ((np.asarray(dx) / sx) ** 2 + (np.asarray(dz) / sz) ** 2))


def _pchip_end(h0, h1, d0, d1):
    m = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1)
    if np.sign(m) != np.sign(d0):
        return 0.0
    if np.sign(d0) != np.sign(d1) and abs(m) > 3 * abs(d0):
        return 3 * d0
    return m


def pchip(x, xs, ys):
    """Monotone cubic through (xs, ys) (Fritsch-Carlson), clamped outside: smooth profiles with no overshoot."""
    xs = np.asarray(xs, np.float64)
    ys = np.asarray(ys, np.float64)
    h = np.diff(xs)
    d = np.diff(ys) / h
    m = np.zeros(len(xs))
    if len(xs) == 2:
        m[:] = d[0]
    else:
        for i in range(1, len(xs) - 1):
            if d[i - 1] * d[i] > 0:
                w1 = 2 * h[i] + h[i - 1]
                w2 = h[i] + 2 * h[i - 1]
                m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
        m[0] = _pchip_end(h[0], h[1], d[0], d[1])
        m[-1] = _pchip_end(h[-1], h[-2], d[-1], d[-2])
    x = np.clip(np.asarray(x, np.float64), xs[0], xs[-1])
    i = np.clip(np.searchsorted(xs, x, side="right") - 1, 0, len(xs) - 2)
    t = (x - xs[i]) / h[i]
    t2, t3 = t * t, t * t * t
    return ((2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h[i] * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1]
            + (t3 - t2) * h[i] * m[i + 1])


# --------------------------------------------------------------------------------------------------
# rows, columns, rings
# --------------------------------------------------------------------------------------------------


def row_heights():
    """z of every ring, bottom (neck base) to top. Neck rows crowd toward the chin (the under-jaw bends there); face
    rows are 2.5 mm apart up to the brow, 3.8 mm above."""
    w = np.linspace(0.0, 1.0, NECK_ROWS, endpoint=False)
    neck = CHIN_Z - (CHIN_Z - NECK_BASE_Z) * (1.0 - w) ** 2.0
    t = np.concatenate([np.linspace(0.0, T_DENSE, FACE_ROWS, endpoint=False), np.linspace(T_DENSE, T_TOP, UPPER_ROWS)])
    return np.concatenate([neck, CHIN_Z + HEAD_H * t])


def row_count():
    return NECK_ROWS + FACE_ROWS + UPPER_ROWS


def angles(segments=SEGMENTS):
    """Angle from the front, in (-pi, pi]: the back of the head (the seam) first, then round past the right side to
    the front and the left side (increasing: front toward his left, +X). Denser at the front."""
    u = -1.0 + 2.0 * np.arange(segments) / segments
    return math.pi * (0.55 * u + 0.45 * u ** 3)


def t_of(z):
    return (np.asarray(z, np.float64) - CHIN_Z) / HEAD_H


def Z(t):
    return CHIN_Z + HEAD_H * t


def profile(face, z, neck_y=NECK_Y):
    """The ring at each height z: dict of half width W, centre yc, front depth F, back depth B, squareness p."""
    z = np.asarray(z, np.float64)
    t = t_of(z)
    nr = face.neck_radius
    nrx = 0.80 * nr         # a neck is narrower side to side than front to back
    hw, hd, jw = face.head_width, face.head_depth, face.jaw_width
    sq = face.jaw_square
    gj = 0.056 * jw
    ts = [-0.70, -0.10, 0.02, 0.10, 0.20, 0.32, 0.45, 0.60, T_CAP]
    W = pchip(t, ts, [nrx, nrx, max(nrx * 1.05, 0.050 * jw), max(nrx * 1.10, 0.057 * jw), 0.0640 * (1.0 + 0.7 * (jw - 1.0)),
                      0.0705 * hw, 0.0735 * hw * (1.0 + 0.03 * (face.cheek - 1.0)), 0.0750 * hw, 0.0765 * hw])
    chin_y = -(0.079 + 0.007 * (face.chin - 1.0))
    neck_front = neck_y - nr
    # The face's plane from the chin to the brow: monotone, so no ring carries a groove across its whole width (an
    # earlier profile dipped 8 mm at the mouth and read as a band across every cheek); the mouth is in `displacement`.
    ts_f = [-0.70, -0.14, -0.05, 0.0, 0.05, 0.12, 0.22, 0.30, 0.40, 0.50, 0.60, 0.70, T_CAP]
    yf = pchip(t, ts_f, [neck_front, neck_front, neck_front + 0.45 * (chin_y - neck_front), 0.82 * chin_y, chin_y,
                         min(chin_y + 0.0020, -0.0765), -0.0788, -0.0806, -0.0829, -0.0852, -0.0875, -0.0899,
                         -0.0899])
    neck_back = neck_y + nr
    ts_b = [-0.70, -0.14, 0.0, 0.10, 0.25, 0.40, 0.55, T_CAP]
    yb = pchip(t, ts_b, [neck_back, neck_back, neck_back + 0.002, max(neck_back + 0.002, 0.084), 0.097, 0.103, 0.106,
                         0.106])
    depth = 1.0 + (hd - 1.0) * smoothstep(-0.10, 0.05, t)     # head_depth scales the head, not the neck
    mid = 0.5 * (yf + yb)
    yf = mid + (yf - mid) * depth
    yb = mid + (yb - mid) * depth
    cap = np.where(t > T_CAP, np.sqrt(np.clip(1.0 - ((t - T_CAP) / (1.0 - T_CAP)) ** 2, 0.0, 1.0)), 1.0)
    yc = 0.5 * (yf + yb)
    p = pchip(t, [-0.70, -0.10, 0.0, 0.08, 0.22, 0.40, 0.60, T_CAP],
              [2.0, 2.0, 1.8, 1.8 + 0.40 * sq, 1.85 + 0.30 * sq, 2.0, 2.0, 2.0])
    return {"t": t, "z": z, "W": W * cap, "yc": yc, "F": (yc - yf) * cap, "B": (yb - yc) * cap, "p": p}


def ring_xy(prof, theta):
    """(R, S) x and y of each ring at the given angles."""
    a = -math.pi / 2 + np.asarray(theta, np.float64)
    c = np.cos(a)[None, :]
    s = np.sin(a)[None, :]
    e = (2.0 / prof["p"])[:, None]
    x = prof["W"][:, None] * np.sign(c) * np.abs(c) ** e
    depth = np.where(s < 0, prof["F"][:, None], prof["B"][:, None])
    y = prof["yc"][:, None] + depth * np.sign(s) * np.abs(s) ** e
    return x, y


def ring_normals_xy(prof, x, y):
    """Unit outward normals in the xy plane of each ring point ((R, S) each)."""
    p = prof["p"][:, None]
    w = np.maximum(prof["W"][:, None], 1e-6)
    dy = y - prof["yc"][:, None]
    depth = np.maximum(np.where(dy < 0, prof["F"][:, None], prof["B"][:, None]), 1e-6)
    nx = np.sign(x) * np.abs(x / w) ** (p - 1.0) / w
    ny = np.sign(dy) * np.abs(dy / depth) ** (p - 1.0) / depth
    n = np.sqrt(nx * nx + ny * ny)
    n[n < 1e-9] = 1.0
    return nx / n, ny / n


# --------------------------------------------------------------------------------------------------
# the face, painted on the rings
# --------------------------------------------------------------------------------------------------


class Landmarks:
    """Heights and widths the features hang from."""

    def __init__(self, face):
        self.eye_x = 0.0315 * face.eye_spacing
        self.eye = Z(0.50)
        self.brow = Z(0.585)
        self.nasion = Z(0.53)
        self.sub = Z(0.335)
        self.tip = Z(0.335) + 0.0125
        self.mouth = Z(0.255)
        self.ul = self.mouth + 0.0055
        self.ll = self.mouth - 0.0070
        self.corner = 0.0205 * face.mouth_width
        self.eye_w = 0.0132 * face.eye_size      # half width of the lid opening
        self.eye_h = 0.0050 * face.eye_size      # half height
        self.eye_h_top = 0.0040 * face.eye_size  # the upper lid covers the top of the iris
        self.eye_h_bot = 0.0053 * face.eye_size
        self.ball_r = 0.0132 * face.eye_size


def aperture(face, ax, z):
    """(mask, radius) of the almond the eyeball shows through: 1 inside, 0 outside, with the elliptical radius."""
    lm = Landmarks(face)
    u = (ax - lm.eye_x) / lm.eye_w
    zc = lm.eye + 0.0022 * np.clip(u, -1.0, 1.0)      # the outer corner sits higher
    dz = z - zc
    v = dz / np.where(dz > 0, lm.eye_h_top, lm.eye_h_bot)
    r = np.sqrt(u * u + v * v)
    return 1.0 - smoothstep(0.80, 1.0, r), r


def _segment_distance(ax, z, p0, p1):
    d = np.array(p1) - np.array(p0)
    q = np.stack([ax - p0[0], z - p0[1]], axis=-1)
    t = np.clip((q @ d) / float(d @ d), 0.0, 1.0)
    proj = np.array(p0) + t[..., None] * d
    return np.sqrt((ax - proj[..., 0]) ** 2 + (z - proj[..., 1]) ** 2), t


def displacement(face, ax, z):
    """(front, side): metres out along the ring normal at |x| = ax, height z. `front` features belong on the face
    plane (brow, eyes, nose, mouth, chin); `side` ones also show on the cheek and jaw as it turns away."""
    f = face
    lm = Landmarks(f)
    age = f.age
    nw, mw = f.nose_width, f.mouth_width
    ex, ze = lm.eye_x, lm.eye
    front = np.zeros_like(ax)
    side = np.zeros_like(ax)
    # brow ridge and the glabella between the brows, the forehead's gentle dome
    zb = lm.brow - 0.0030 * np.clip((ax - 0.028) / 0.028, 0.0, 1.0)
    front += 0.0095 * f.brow * gauss(ax - 0.031, z - zb, 0.024, 0.0070)
    front += 0.0050 * f.brow * gauss(ax, z - (lm.brow - 0.0045), 0.010, 0.0105)
    front += 0.0020 * gauss(ax - 0.024, z - Z(0.69), 0.034, 0.030)
    # eye socket, the lid opening the eyeball shows through, the lid's rim and crease, the tear trough and the bag
    front -= SOCKET_DEPTH * f.eye_sink * gauss(ax - ex, z - ze, 0.0205, 0.0135)
    ap, r = aperture(f, ax, z)
    front -= 0.0140 * ap
    front += 0.0032 * np.exp(-(((r - 1.05) / 0.20) ** 2))
    front += 0.0030 * gauss(ax - ex, z - (ze + 0.0075), 0.0135, 0.0040)       # the upper lid's fold
    front -= 0.0026 * gauss(ax - ex, z - (ze + 0.0120), 0.013, 0.0025)
    front -= (0.0010 + 0.0030 * age) * gauss(ax - ex * 0.92, z - (ze - 0.0150), 0.013, 0.0035)
    front += (0.0012 + 0.0028 * age) * gauss(ax - ex * 1.02, z - (ze - 0.0225), 0.014, 0.0045)
    # cheek fullness: rounds the step from the cheekbone's width down to the jaw's (flesh over the masseter)
    side += 0.0020 * (0.6 + 0.4 * f.cheek) * (1.0 - 0.7 * f.cheek_hollow) * gauss(ax - 0.062, z - Z(0.32), 0.018, 0.060)
    # cheekbone, the hollow under it, the temple
    side += 0.0028 * f.cheek * gauss(ax - 0.0545, z - Z(0.40), 0.014, 0.022)
    front += 0.0013 * f.cheek * gauss(ax - 0.042, z - (Z(0.41) + 0.35 * (ax - 0.040)), 0.014, 0.020)
    front -= 0.0045 * f.cheek_hollow * (0.6 + 0.8 * age) * gauss(ax - 0.044, z - (Z(0.275) + 0.30 * (ax - 0.040)), 0.018, 0.022)
    side -= 0.0030 * f.cheek_hollow * gauss(ax - 0.060, z - Z(0.30), 0.010, 0.018)
    side -= 0.0020 * gauss(ax - 0.0735, z - Z(0.58), 0.008, 0.022)
    # nose: dorsum from the nasion to the tip, wings, nostrils, a hump or a dip along the bridge
    proj = 0.027 * f.nose_proj
    rise = smoothstep(lm.nasion + 0.004, lm.tip, z)
    fall = smoothstep(lm.sub - 0.0035, lm.tip - 0.0015, z)
    width = 0.0038 + (0.0068 * nw - 0.0038) * smoothstep(lm.nasion - 0.010, lm.tip + 0.004, z)
    dorsum = proj * rise ** 1.15 * fall + 0.0035 * f.nose_bridge * gauss(0.0, z - 0.5 * (lm.nasion + lm.tip), 1.0,
                                                                          0.012)
    front += dorsum * np.exp(-0.5 * (ax / width) ** 2)
    front += 0.0045 * f.nose_proj * gauss(ax, z - (lm.tip - 0.0020), 0.0058 * nw, 0.0065)           # the tip's ball
    front += 0.0100 * f.nose_proj * gauss(ax - 0.0115 * nw, z - (lm.sub + 0.0055), 0.0048 * nw, 0.0055)
    front -= 0.0028 * gauss(ax - 0.0082 * nw, z - (lm.sub + 0.0015), 0.0040, 0.0032)
    # nasolabial fold, philtrum
    dist, tt = _segment_distance(ax, z, (0.0185 * nw + 0.004, lm.sub + 0.004), (lm.corner + 0.006, lm.mouth - 0.006))
    front -= (0.0010 + 0.0035 * age) * np.exp(-0.5 * (dist / 0.0032) ** 2) * smoothstep(0.0, 0.15, tt) \
        * smoothstep(1.05, 0.8, tt)
    front -= 0.0016 * gauss(ax, z - (lm.sub - 0.0085), 0.0042, 0.0070)
    front += 0.0010 * gauss(ax - 0.0075, z - (lm.sub - 0.009), 0.0022, 0.0060)
    # the muzzle: mouth and the cheeks beside it stand forward of the plane of the face, so the front is round
    front += 0.0015 * gauss(ax, z - Z(0.27), 0.030, 0.045)
    # lips, the line between them, the corners, the groove under the lower lip, the chin
    front += 0.0058 * f.lips * gauss(ax, z - lm.ul, 0.0135 * mw, 0.0045)
    front += 0.0075 * f.lips * gauss(ax, z - lm.ll, 0.0130 * mw, 0.0052)
    front -= 0.0050 * gauss(ax, z - lm.mouth, lm.corner, 0.0022)
    front -= 0.0030 * gauss(ax - lm.corner, z - lm.mouth, 0.0045, 0.0045)
    front -= 0.0022 * gauss(ax, z - (lm.ll - 0.0105), 0.0140, 0.0036)
    front += 0.0130 * f.chin * gauss(ax, z - Z(0.075), 0.0185, 0.0140)
    # jaw angle and the masseter
    side += 0.0050 * (f.jaw_width - 1.0 + 0.2 * f.jaw_square) * gauss(ax - 0.0585, z - Z(0.115), 0.011, 0.020)
    side += 0.0025 * f.jaw_square * gauss(ax - 0.0655, z - Z(0.22), 0.010, 0.030)
    return front, side


def smooth_grid(P, iters=1, factor=0.3):
    """Laplacian smoothing over the grid (columns wrap, rows are clamped)."""
    P = P.copy()
    for _ in range(iters):
        left = np.roll(P, 1, axis=1)
        right = np.roll(P, -1, axis=1)
        up = np.concatenate([P[1:], P[-1:]], axis=0)
        down = np.concatenate([P[:1], P[:-1]], axis=0)
        avg = (left + right + up + down) / 4.0
        P = P + factor * (avg - P)
    return P


def grid_normals(P, apex):
    """Outward vertex normals of the grid ((R, S, 3)), from the surface's own tangents."""
    dt = np.roll(P, -1, axis=1) - np.roll(P, 1, axis=1)
    top = np.concatenate([P[1:], apex[None, None, :].repeat(P.shape[1], axis=1)], axis=0)
    bottom = np.concatenate([P[:1], P[:-1]], axis=0)
    dz = top - bottom
    n = np.cross(dt, dz)
    length = np.linalg.norm(n, axis=2)[:, :, None]
    length[length < 1e-12] = 1.0
    return n / length


def grid_loop_uvs(faces, rows, segs, v_rows, neck_cap=False):
    """UVs per face corner for faces over a (rows, segs) grid plus an apex vertex (index rows * segs, and the neck
    cap's centre after it): u = column / segs, v = v_rows[row]. A face that crosses the seam takes u = 1 on column 0."""
    apex = rows * segs
    uv = np.zeros((sum(len(f) for f in faces), 2))
    k = 0
    for f in faces:
        cols = [i % segs for i in f if i < apex]
        wraps = bool(cols) and max(cols) == segs - 1 and min(cols) == 0
        for idx in f:
            if idx == apex:
                uv[k] = (0.5, 1.0)
            elif neck_cap and idx == apex + 1:
                uv[k] = (0.5, 0.0)
            else:
                r, c = divmod(idx, segs)
                uv[k] = ((segs if (wraps and c == 0) else c) / segs, v_rows[r])
            k += 1
    return uv


class HeadGrid:
    """The base head: positions P (R, S, 3), the apex (3,), normals N (R, S, 3), the rows' heights, the angles, the
    eyeball placement, and the quads' vertex indices."""

    def __init__(self, face, neck_y=NECK_Y, segments=SEGMENTS):
        self.face = face
        self.neck_y = neck_y
        self.theta = angles(segments)
        self.z = row_heights()
        self.prof = profile(face, self.z, neck_y)
        x, y = ring_xy(self.prof, self.theta)
        z = np.broadcast_to(self.z[:, None], x.shape)
        nx, ny = ring_normals_xy(self.prof, x, y)
        front, side = displacement(face, np.abs(x), z)
        mf = smoothstep(-0.10, -0.50, ny)
        ms = smoothstep(0.75, 0.0, ny)
        d = front * mf + side * ms
        d = d * smoothstep(CHIN_Z - 0.03, CHIN_Z + 0.01, z)      # the neck has no face
        P = np.stack([x + nx * d, y + ny * d, z], axis=2)
        self.P = smooth_grid(P, iters=2, factor=0.35)
        top_prof = profile(face, np.array([CROWN_Z]), neck_y)
        self.apex = np.array([0.0, float(top_prof["yc"][0]), CROWN_Z])
        self.N = grid_normals(self.P, self.apex)
        self.rows, self.segs = self.P.shape[0], self.P.shape[1]

    # indices: ring r, column c -> r * S + c; then the apex (R * S) and the neck cap's centre (R * S + 1)
    def vertices(self):
        centre = self.P[0].mean(axis=0)
        return np.concatenate([self.P.reshape(-1, 3), self.apex[None, :], centre[None, :]], axis=0)

    def quads(self):
        """Faces by vertex index, wound outward (rows up, columns increasing), the crown's fan and the neck's cap."""
        out = cap_faces(self.rows, self.segs)
        S = self.segs
        out += [(self.rows * S + 1, (c + 1) % S, c) for c in range(S)]
        return out

    def loop_uvs(self, faces):
        """(sum of face sizes, 2): u, v per face corner in `faces` order. u runs round the head (a face past the seam
        takes u = 1), v up it by ring index; the apex is (0.5, 1), the neck's cap centre (0.5, 0)."""
        return grid_loop_uvs(faces, self.rows, self.segs, np.arange(self.rows) / (self.rows - 1.0), neck_cap=True)

    def row_of_t(self, t):
        """Fractional row of height t (head heights from the chin)."""
        return np.interp(Z(np.asarray(t, np.float64)), self.z, np.arange(self.rows))

    def column_of_theta(self, theta):
        return np.interp(np.asarray(theta, np.float64), self.theta, np.arange(self.segs), period=2 * math.pi)

    def eyes(self):
        """[(centre xyz, radius)] left (+X) then right: the ball's front stands EYE_SET_BACK in front of the socket
        floor. The floor is the ring's front at the eye's height plus the socket's depth."""
        lm = Landmarks(self.face)
        out = []
        for sx in (1.0, -1.0):
            ax = np.array([lm.eye_x])
            zz = np.array([lm.eye])
            prof = profile(self.face, zz, self.neck_y)
            inside = 1.0 - (ax / prof["W"]) ** prof["p"]
            y_face = prof["yc"] - prof["F"] * np.maximum(inside, 0.0) ** (1.0 / prof["p"])
            floor = float(y_face[0])
            out.append((np.array([sx * lm.eye_x, floor - EYE_SET_BACK + lm.ball_r, lm.eye]), lm.ball_r))
        return out

    def surface_at(self, x, z):
        """Front surface point and normal at (x, z) for arrays x, z (front half only): ((n, 3), (n, 3))."""
        x = np.asarray(x, np.float64)
        z = np.asarray(z, np.float64)
        r = np.interp(z, self.z, np.arange(self.rows))
        r0 = np.clip(np.floor(r).astype(int), 0, self.rows - 2)
        fr = r - r0
        front = np.abs(self.theta) < math.pi / 2          # columns whose x increases with the angle
        cols = np.nonzero(front)[0]
        pts = np.zeros((len(x), 3))
        nrm = np.zeros((len(x), 3))
        for i in range(len(x)):
            acc_p = np.zeros(3)
            acc_n = np.zeros(3)
            for rr, wt in ((r0[i], 1.0 - fr[i]), (r0[i] + 1, fr[i])):
                xs = self.P[rr, cols, 0]
                order = np.argsort(xs)
                for k in range(3):
                    acc_p[k] += wt * np.interp(x[i], xs[order], self.P[rr, cols, k][order])
                    acc_n[k] += wt * np.interp(x[i], xs[order], self.N[rr, cols, k][order])
            pts[i] = acc_p
            nrm[i] = acc_n / max(np.linalg.norm(acc_n), 1e-9)
        return pts, nrm


# --------------------------------------------------------------------------------------------------
# ears
# --------------------------------------------------------------------------------------------------

EAR_RINGS = 14
EAR_SEG = 16


def ear_arrays(face, grid, side):
    """An ear as a closed lens-shaped solid standing off the head, `side` +1 his left: (vertices, faces, anchor). Overlaps
    the skull by a few mm at its front edge; the concha and the helix are heights on its outer face. Also returns
    the anchor point (the ear's front edge at the head), for the UVs."""
    es = face.ear_size
    z_c = Z(0.47)
    length = 0.062 * es
    prof = profile(face, np.array([z_c]), grid.neck_y)
    x_h = float(prof["W"][0]) - 0.004
    y_front = float(prof["yc"][0]) - 0.004
    phi = math.radians(face.ear_flare)
    u = np.array([side * math.sin(phi), math.cos(phi)])
    n = np.array([side * math.cos(phi), -math.sin(phi)])
    s = np.linspace(0.03, 0.97, EAR_RINGS)
    a = 0.0165 * es * np.sqrt(np.clip(1.0 - ((s - 0.55) / 0.52) ** 2, 0.0, 1.0)) \
        * (0.78 + 0.22 * smoothstep(0.0, 0.4, s))
    b = 0.0052 * es * (0.7 + 0.3 * (1.0 - s))
    th = np.linspace(0.0, 2.0 * math.pi, EAR_SEG, endpoint=False)
    verts = []
    for i in range(EAR_RINGS):
        for j in range(EAR_SEG):
            ct, st = math.cos(th[j]), math.sin(th[j])
            lateral = 0.0
            if st > 0.0:
                concha = -0.0045 * math.exp(-0.5 * (((ct + 0.1) / 0.45) ** 2 + ((s[i] - 0.5) / 0.17) ** 2))
                helix = 0.0036 * float(smoothstep(0.70, 0.95, abs(ct)))
                lateral = (concha + helix) * st
            centre_xy = np.array([side * x_h, y_front]) + u * a[i]
            p_xy = centre_xy + u * a[i] * ct + n * (b[i] * st + lateral)
            verts.append((p_xy[0], p_xy[1], z_c - length / 2 + s[i] * length))
    faces = []
    for i in range(EAR_RINGS - 1):
        for j in range(EAR_SEG):
            j1 = (j + 1) % EAR_SEG
            faces.append((i * EAR_SEG + j, i * EAR_SEG + j1, (i + 1) * EAR_SEG + j1, (i + 1) * EAR_SEG + j))
    bottom = len(verts)
    top = bottom + 1
    verts.append(tuple(np.mean(verts[:EAR_SEG], axis=0)))
    verts.append(tuple(np.mean(verts[(EAR_RINGS - 1) * EAR_SEG:EAR_RINGS * EAR_SEG], axis=0)))
    faces += [(bottom, (j + 1) % EAR_SEG, j) for j in range(EAR_SEG)]
    base = (EAR_RINGS - 1) * EAR_SEG
    faces += [(top, base + j, base + (j + 1) % EAR_SEG) for j in range(EAR_SEG)]
    return np.array(verts, np.float64), faces, (x_h * side, y_front, z_c)


# --------------------------------------------------------------------------------------------------
# brows, hair and headwear shells (all on the grid)
# --------------------------------------------------------------------------------------------------

BROW_ROWS = 14
BROW_COLS = 4


def brow_arrays(face, grid, side):
    """A brow card on the skin: P (BROW_ROWS, BROW_COLS, 3), N likewise, and u, v per vertex. Rows run from the inner
    end out; columns bottom to top across the hair."""
    lm = Landmarks(face)
    ax = np.linspace(0.009, 0.0545, BROW_ROWS) * face.eye_spacing ** 0.5
    arch = lm.brow + 0.0015 - 0.0040 * np.clip((ax - 0.030) / 0.025, 0.0, 1.0) ** 1.5 + 0.0012 * (ax < 0.018)
    thick = (0.0080 + 0.0030 * face.brow) * (1.0 - 0.55 * smoothstep(0.02, 0.054, ax))
    cols = np.linspace(-0.5, 0.5, BROW_COLS)
    P = np.zeros((BROW_ROWS, BROW_COLS, 3))
    N = np.zeros_like(P)
    for j, c in enumerate(cols):
        pts, nrm = grid.surface_at(side * ax, arch + c * thick)
        P[:, j] = pts + nrm * 0.0012
        N[:, j] = nrm
    u = np.repeat(np.linspace(0.0, 1.0, BROW_ROWS)[:, None], BROW_COLS, axis=1)
    v = np.repeat(np.linspace(0.0, 1.0, BROW_COLS)[None, :], BROW_ROWS, axis=0)
    return P, N, u, v


def hairline_t(face, theta, style=None):
    """Height (head heights) the hair starts at, round the head (theta from the front): the forehead, the temples
    drawn up, the sideburn down past the ear, the nape low. hair_fade pulls the sides and nape up (a skin fade)."""
    th = np.abs(np.asarray(theta, np.float64))
    deg = np.degrees(th)
    front = face.hairline
    pts_deg = [0.0, 35.0, 62.0, 80.0, 100.0, 125.0, 150.0, 180.0]
    base = [front, front - 0.02, 0.70, 0.60, 0.47, 0.30, 0.15, 0.10]
    faded = [front, front - 0.03, 0.74, 0.70, 0.66, 0.55, 0.42, 0.36]
    k = face.hair_fade
    return np.interp(deg, pts_deg, [b + k * (f - b) for b, f in zip(base, faded)])


def cap_grid(grid, edge_t, thickness, rows=14, feather=0.14, tilt=None):
    """A shell over the top of the head from a height edge_t(theta) (head heights, array over columns) to the crown.
    thickness: metres, scalar or (rows,) profile from the edge (0) to the crown (1); feathered to 0 at the edge.
    Returns (P (rows, S, 3), N (rows, S, 3), apex (3,), s (rows,)) with s the 0..1 distance up from the edge."""
    S = grid.segs
    top_row = grid.rows - 1.0
    r_edge = grid.row_of_t(np.clip(edge_t, 0.02, T_TOP - 0.02))
    s = np.linspace(0.0, 1.0, rows)
    P = np.zeros((rows, S, 3))
    N = np.zeros((rows, S, 3))
    cols = np.arange(S)
    for i, si in enumerate(s):
        r = r_edge + si * (top_row - r_edge)
        r0 = np.clip(np.floor(r).astype(int), 0, grid.rows - 2)
        fr = (r - r0)[:, None]
        p = grid.P[r0, cols] * (1 - fr) + grid.P[r0 + 1, cols] * fr
        n = grid.N[r0, cols] * (1 - fr) + grid.N[r0 + 1, cols] * fr
        n /= np.maximum(np.linalg.norm(n, axis=1)[:, None], 1e-9)
        th = thickness(si) if callable(thickness) else thickness
        lift = th * float(smoothstep(0.0, feather, si))
        P[i] = p + n * lift
        N[i] = n
    apex_n = grid.N[-1].mean(axis=0)
    apex_n /= max(np.linalg.norm(apex_n), 1e-9)
    th_top = thickness(1.0) if callable(thickness) else thickness
    return P, N, grid.apex + apex_n * th_top, s


def cap_faces(rows, segs):
    """Quads of a cap grid (rows ring from the edge up) and the fan to the apex vertex (index rows * segs)."""
    out = []
    for r in range(rows - 1):
        for c in range(segs):
            c1 = (c + 1) % segs
            out.append((r * segs + c, r * segs + c1, (r + 1) * segs + c1, (r + 1) * segs + c))
    apex = rows * segs
    base = (rows - 1) * segs
    out += [(base + c, base + (c + 1) % segs, apex) for c in range(segs)]
    return out


def hair_thickness(style):
    """Metres of hair over the scalp at its thickest, by style."""
    return {"buzz": 0.0028, "crop": 0.0075, "short": 0.0125}.get(style, 0.0)


def beanie_edge_t(theta):
    """Where a beanie's rolled edge sits: low on the brow, over the ears, lower at the nape."""
    deg = np.degrees(np.abs(np.asarray(theta, np.float64)))
    return np.interp(deg, [0.0, 50.0, 90.0, 130.0, 180.0], [0.615, 0.56, 0.37, 0.34, 0.40])


def cap_edge_t(theta):
    """Where a baseball cap's crown ends: above the brow, clear of the ears (the brim is built on it)."""
    deg = np.degrees(np.abs(np.asarray(theta, np.float64)))
    return np.interp(deg, [0.0, 50.0, 90.0, 130.0, 180.0], [0.66, 0.65, 0.63, 0.57, 0.52])


def brim_arrays(grid, edge_P, edge_N, theta, half_arc_deg=62.0, length=0.082, cols=6, rows=17):
    """The brim's points: (rows, cols, 3) and up normals, from the crown's edge at the front out and down."""
    th = np.radians(np.linspace(-half_arc_deg, half_arc_deg, rows))
    base_cols = grid.column_of_theta(th)
    c0 = np.floor(base_cols).astype(int) % grid.segs
    c1 = (c0 + 1) % grid.segs
    f = (base_cols - np.floor(base_cols))[:, None]
    P0 = edge_P[0, c0] * (1 - f) + edge_P[0, c1] * f
    N0 = edge_N[0, c0] * (1 - f) + edge_N[0, c1] * f
    horiz = np.stack([N0[:, 0], N0[:, 1], np.zeros(rows)], axis=1)
    horiz /= np.maximum(np.linalg.norm(horiz, axis=1)[:, None], 1e-9)
    ext = length * np.sqrt(np.clip(1.0 - (np.degrees(th) / half_arc_deg) ** 2, 0.0, 1.0)) ** 0.8 + 0.004
    P = np.zeros((rows, cols, 3))
    for j in range(cols):
        r = j / (cols - 1)
        drop = 0.016 * r ** 2 - 0.004 * r
        P[:, j] = P0 + horiz * (ext * r)[:, None] + np.array([0.0, 0.0, -1.0]) * drop
    up = np.zeros_like(P)
    up[:, :, 2] = 1.0
    return P, up


# --------------------------------------------------------------------------------------------------
# eyeballs
# --------------------------------------------------------------------------------------------------

EYE_RINGS = 16
EYE_SEGS = 20
CORNEA_DEG = 33.75       # the iris and cornea cover this angle from the front pole (a ring boundary at 16 rings)
CORNEA_BULGE = 0.0011    # metres the cornea's dome stands proud of the ball


def eye_arrays(centre, radius, rings=EYE_RINGS, segs=EYE_SEGS):
    """An eyeball with its pole at the front (-Y): (vertices, faces, uv per vertex, iris face mask). The front cap
    within CORNEA_DEG is the iris slot, a dome CORNEA_BULGE proud of the sclera: the engine's light puts the highlight
    on it. UVs are the ball seen from the front (planar), so one round texture serves."""
    centre = np.asarray(centre, np.float64)
    phis = math.pi * np.arange(1, rings) / rings
    thetas = 2.0 * math.pi * np.arange(segs) / segs
    cornea = math.radians(CORNEA_DEG)
    verts = [centre + (radius + CORNEA_BULGE) * np.array([0.0, -1.0, 0.0])]
    uvs = [(0.5, 0.5)]
    for phi in phis:
        r = radius + CORNEA_BULGE * float(smoothstep(cornea + 1e-6, cornea * 0.35, phi))
        for th in thetas:
            d = np.array([math.sin(phi) * math.cos(th), -math.cos(phi), math.sin(phi) * math.sin(th)])
            verts.append(centre + r * d)
            uvs.append((0.5 + 0.5 * math.sin(phi) * math.cos(th), 0.5 + 0.5 * math.sin(phi) * math.sin(th)))
    back = len(verts)
    verts.append(centre + radius * np.array([0.0, 1.0, 0.0]))
    uvs.append((0.5, 0.5))

    def ring_start(k):
        return 1 + k * segs      # first vertex of the k-th (0 based) ring between the poles

    faces = [(0, ring_start(0) + (j + 1) % segs, ring_start(0) + j) for j in range(segs)]
    for k in range(rings - 2):
        for j in range(segs):
            j1 = (j + 1) % segs
            faces.append((ring_start(k) + j, ring_start(k) + j1, ring_start(k + 1) + j1, ring_start(k + 1) + j))
    last = ring_start(rings - 2)
    faces += [(back, last + j, last + (j + 1) % segs) for j in range(segs)]
    iris_rings = int(round(cornea / (math.pi / rings)))     # rings 1..iris_rings lie within the cornea angle
    mask = []
    for f in faces:
        ring_of = [0 if v == 0 else (rings if v == back else 1 + (v - 1) // segs) for v in f]
        mask.append(max(ring_of) <= iris_rings)
    faces = [tuple(reversed(f)) for f in faces]       # wound outward
    return np.array(verts), faces, np.array(uvs), np.array(mask, bool)
