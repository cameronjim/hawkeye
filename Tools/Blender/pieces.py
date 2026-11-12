"""Garment pieces more than one character wears (claude-docs/blender-pipeline.md): the cloth shell builder, combat
boots, trainers, gloves, a stand collar, a belt, laces. Each returns (object, weights) or a list of them; the build
scripts skin and join them."""

import math

import bmesh
import numpy as np

import costume as cs

TORSO_AXIS = (0.0, 0.02)
LEG_REACH = 0.13   # rays onto one leg's garment start this far out: the other leg's is 0.16+ away


def leg_axis(body, side):
    """(x, y) of a leg's bone line at height z: thigh -> calf -> foot heads."""
    pts = [body.bone_head(b + "_" + side) for b in ("thigh", "calf", "foot")]
    zs = np.array([p[2] for p in pts])[::-1]
    xs = np.array([p[0] for p in pts])[::-1]
    ys = np.array([p[1] for p in pts])[::-1]
    return lambda z: (float(np.interp(z, zs, xs)), float(np.interp(z, zs, ys)))


def arm_axis(body, side):
    """(y, z) of an arm's bone line at |x|: upperarm -> lowerarm -> hand heads (T-pose, along X)."""
    pts = [body.bone_head(b + "_" + side) for b in ("upperarm", "lowerarm", "hand")]
    xs = np.array([abs(p[0]) for p in pts])
    return lambda x: (float(np.interp(abs(x), xs, [p[1] for p in pts])),
                      float(np.interp(abs(x), xs, [p[2] for p in pts])))


def extents(body, z, band=0.015, families=("torso", "leg", "foot")):
    """(xmin, xmax, ymin, ymax) of the body's verts of these families near height z."""
    fam = sum(body.family[f] for f in families)
    sel = (np.abs(body.co[:, 2] - z) < band) & (fam > 0.5)
    pts = body.co[sel]
    return pts[:, 0].min(), pts[:, 0].max(), pts[:, 1].min(), pts[:, 1].max()


def cap_holes(obj):
    with cs.Edit(obj) as bm:
        edges = [e for e in bm.edges if e.is_boundary]
        if edges:
            bmesh.ops.holes_fill(bm, edges=edges, sides=0)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])


def shell(body, name, offset, hangs=(), extras=(), voxel=0.008, smooth=6, keep=None):
    """A smooth closed cloth volume around the body.

    offset: per body vertex (array) or scalar, metres out along the normal.
    hangs: (vertex mask over the body, axis_fn, z_top, z_bottom, fill, taper) for cs.hang.
    extras: more closed objects to merge in (a skirt), consumed.
    keep: optional body-vertex mask; the rest is cut away first (and the holes capped) so the remesh is small.
    """
    obj = body.offset_copy(name, offset)
    co = cs.get_co(obj)
    for mask, axis_fn, z_top, z_bottom, fill, taper in hangs:
        co = cs.hang(co, mask, axis_fn, z_top, z_bottom, fill=fill, taper=taper)
    cs.set_co(obj, co)
    if keep is not None:
        cs.delete_verts(obj, ~np.asarray(keep, bool))
        cap_holes(obj)
    if extras:
        obj = cs.join([obj] + list(extras), name)
    obj = cs.voxel_remesh(obj, voxel)
    if smooth:
        cs.taubin(obj, smooth)
    return obj


def iron(body, obj, torso_iters=30, limb_iters=8, z_range=(0.9, 1.47)):
    """Smooth the muscle out of a cloth shell: hard over the torso (pecs, abs, lats, spine), lightly on the limbs.
    Taubin steps, so the volume stays."""
    co = cs.get_co(obj)
    fam = body.families_of(body.transfer(co))
    torso = (fam["torso"] > 0.4) & (co[:, 2] > z_range[0]) & (co[:, 2] < z_range[1])
    cs.taubin(obj, torso_iters, mask=torso)
    cs.taubin(obj, limb_iters, mask=(fam["arm"] + fam["leg"]) > 0.4)


def keep_largest(obj):
    """Drop loose islands (voxel crumbs), keeping the biggest by vertex count."""
    with cs.Edit(obj) as bm:
        seen = set()
        islands = []
        for v in bm.verts:
            if v.index in seen:
                continue
            stack = [v]
            island = []
            seen.add(v.index)
            while stack:
                cur = stack.pop()
                island.append(cur)
                for e in cur.link_edges:
                    o = e.other_vert(cur)
                    if o.index not in seen:
                        seen.add(o.index)
                        stack.append(o)
            islands.append(island)
        islands.sort(key=len, reverse=True)
        doomed = [v for isl in islands[1:] for v in isl]
        if doomed:
            bmesh.ops.delete(bm, geom=doomed, context="VERTS")


def keep_islands(obj, min_verts):
    """Drop loose islands smaller than min_verts."""
    with cs.Edit(obj) as bm:
        seen = set()
        doomed = []
        for v in bm.verts:
            if v.index in seen:
                continue
            stack = [v]
            island = []
            seen.add(v.index)
            while stack:
                cur = stack.pop()
                island.append(cur)
                for e in cur.link_edges:
                    o = e.other_vert(cur)
                    if o.index not in seen:
                        seen.add(o.index)
                        stack.append(o)
            if len(island) < min_verts:
                doomed += island
        if doomed:
            bmesh.ops.delete(bm, geom=doomed, context="VERTS")


# --------------------------------------------------------------------------------------------------
# footwear
# --------------------------------------------------------------------------------------------------


def _hull(points):
    """2D convex hull (monotone chain), counter-clockwise."""
    pts = sorted(set(map(tuple, np.round(points, 5))))
    if len(pts) < 3:
        return np.array(pts)

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return np.array(lower[:-1] + upper[:-1])


def _resample_loop(loop, n):
    loop = np.asarray(loop, np.float64)
    closed = np.vstack([loop, loop[:1]])
    seg = np.linalg.norm(np.diff(closed, axis=0), axis=1)
    t = np.concatenate([[0], np.cumsum(seg)])
    s = np.linspace(0, t[-1], n, endpoint=False)
    return np.stack([np.interp(s, t, closed[:, 0]), np.interp(s, t, closed[:, 1])], axis=1)


def sole(name, co, heel_y, top_z=0.024, toe_lift=0.008, heel_drop=0.0, grow=0.006, rim=0.0, n=48):
    """A sole under a shoe/boot upper (co: that foot's upper verts): the outline of its lowest verts (convex hull,
    grown by `grow`), a flat bottom with a heel step (rear of heel_y stands heel_drop lower than the forefoot), toe
    spring, a rounded top edge."""
    low = co[co[:, 2] < 0.035]
    hull = _hull(low[:, :2])
    c = hull.mean(axis=0)
    ring = _resample_loop(hull, n)
    d = ring - c
    ring = ring + d / np.linalg.norm(d, axis=1)[:, None] * grow
    y = ring[:, 1]
    # Forefoot bottom sits at 0; the heel at -heel_drop; the toe curls up by toe_lift.
    toe_y = y.min()
    bottom = np.where(y > heel_y, 0.0, heel_drop) + toe_lift * cs.smoothstep(toe_y + 0.06, toe_y, y)
    rings = []
    for z_off, scale in ((None, 0.97), (None, 1.0), ("top", 1.0), ("top_in", 0.985)):
        r = c + (ring - c) * scale
        if z_off is None:
            zz = bottom + (0.0 if scale == 0.97 else 0.004)
        elif z_off == "top":
            zz = np.full(n, top_z) + rim
        else:
            zz = np.full(n, top_z + 0.002)
        rings.append(np.column_stack([r, zz]))
    return cs.loft(name, rings, closed=True, cap_start=True, cap_end=True)


def _side(obj, sx):
    co = cs.get_co(obj)
    return co[np.sign(co[:, 0]) == sx]


def lace_path(surface, axis_points, directions, half_width=0.011, lift=0.003):
    """Eyelet pairs down the front of a shoe: rays from outside onto the surface at each axis point.
    Returns (centres, normals, left eyelets, right eyelets)."""
    P, N = [], []
    for a, d in zip(axis_points, directions):
        p, n = surface.project_from_axis(a, d)
        if p is None:
            continue
        P.append(p)
        N.append(n)
    P, N = np.array(P), np.array(N)
    T = np.gradient(P, axis=0)
    T /= np.linalg.norm(T, axis=1)[:, None]
    S = np.cross(N, T)
    S /= np.linalg.norm(S, axis=1)[:, None]
    L = P - S * half_width + N * lift
    R = P + S * half_width + N * lift
    return P, N, L, R


def laces(name, L, R, N, radius=0.0022, bow=True):
    """Criss-cross laces between eyelet pairs, and eyelets: one object."""
    parts = []
    for i in range(len(L) - 1):
        for a, b in ((L[i], R[i + 1]), (R[i], L[i + 1])):
            mid = (a + b) / 2 + N[i] * 0.0025
            parts.append(cs.tube(name + "_x", [a, mid, b], radius, segments=6, cap=True, squash=0.6))
    for i in range(len(L)):
        for e in (L[i], R[i]):
            parts.append(cs.tube(name + "_eye", [e - N[i] * 0.002, e + N[i] * 0.0015], 0.0032, segments=8, cap=True))
    if bow:
        top = (L[-1] + R[-1]) / 2 + N[-1] * 0.004
        side = (R[-1] - L[-1])
        side /= np.linalg.norm(side)
        for s in (-1, 1):
            loop = [top, top + side * s * 0.018 + N[-1] * 0.006, top + side * s * 0.03 - np.array([0, 0, 0.012]),
                    top + side * s * 0.012 - np.array([0, 0, 0.004])]
            parts.append(cs.tube(name + "_bow", loop, radius, segments=6, cap=True, squash=0.6))
    return cs.join(parts, name)


def footwear_upper(body, name, top_z, foot_offset, shaft_offset, voxel=0.006, smooth=8, hang_shaft=True,
                   base_z=0.018):
    """Both uppers: the body's feet (and shins up to top_z) pushed out, toes merged by the remesh, a shaft that
    hangs a little from the calf, flat underneath at base_z (the sole goes below)."""
    fam = body.family
    off = np.where(fam["foot"] > 0.5, foot_offset, shaft_offset)
    hangs = []
    if hang_shaft:
        for side in ("l", "r"):
            mask = (np.sign(body.co[:, 0]) == (1 if side == "l" else -1)) & ((fam["leg"] + fam["foot"]) > 0.5)
            hangs.append((mask, leg_axis(body, side), top_z + 0.03, 0.20, 0.92, 1.0))
    keep = body.co[:, 2] < top_z + 0.06
    obj = shell(body, name, off, hangs=hangs, voxel=voxel, smooth=smooth, keep=keep)
    co = cs.get_co(obj)
    co[:, 2] = np.maximum(co[:, 2], base_z)
    cs.set_co(obj, co)
    cs.taubin(obj, 2)
    cs.cut_and_delete(obj, (0, 0, top_z), (0, 0, 1), lambda c, n: c[:, 2] > top_z)
    cs.cut_and_delete(obj, (0, 0, base_z + 0.0005), (0, 0, 1),
                      lambda c, n: (c[:, 2] < base_z + 0.0005) & (n[:, 2] < -0.7))
    keep_islands(obj, 200)
    return obj


def combat_boots(body, mats, top_z=0.34, prefix="Boot"):
    """Tall lace-up combat boots: leather upper with a toe cap, rubber sole with a heel, laces up the front, a strap
    and buckle at the top, a pull tab at the back. Returns [(object, weights)]."""
    upper = footwear_upper(body, prefix + "Upper", top_z, foot_offset=0.013, shaft_offset=0.017, base_z=0.026)
    cs.decimate(upper, tris=5000)
    # Toe cap and heel counter: separate leather panels (cut on clean planes, raised a little).
    cs.bisect(upper, (0, -0.075, 0), (0, 1, 0))
    cs.bisect(upper, (0, 0, 0.055), (0, 0.6, 1))
    cs.bisect(upper, (0, 0.105, 0), (0, 1, 0))
    c, n = cs.face_data(upper)
    cs.set_material(upper, mats["boot"])
    toe = c[:, 1] < -0.075
    heel = (c[:, 1] > 0.105) & (c[:, 2] < 0.09)
    cs.set_material(upper, mats["boot_panel"], toe | heel)
    cs.raise_faces(upper, toe | heel, 0.0015)
    out = [(upper, body.transfer(cs.get_co(upper)))]
    surf = cs.Surface(upper)
    for side, sx in (("l", 1), ("r", -1)):
        foot = body.bone_head("foot_" + side)
        ball = body.bone_head("ball_" + side)
        ax = leg_axis(body, side)
        s = sole(prefix + "Sole_" + side, _side(upper, sx), heel_y=foot[1] - 0.025, top_z=0.029, toe_lift=0.006,
                 heel_drop=0.009, grow=0.005)
        # Heel block: the rear of the sole a little deeper, and lug ridges cut into the tread edge.
        cs.set_material(s, mats["sole"])
        out.append((s, body.transfer(cs.get_co(s))))
        # Laces from the instep (just behind the toe cap) up the front of the shaft.
        pts, dirs = [], []
        for t in np.linspace(0.0, 1.0, 9):
            if t < 0.3:
                u = t / 0.3
                a = np.array([ball[0], ball[1] + 0.035 + u * 0.06, 0.045 + u * 0.06])
                d = np.array([0.0, -0.55, 0.85])
            else:
                u = (t - 0.3) / 0.7
                z = 0.11 + u * (top_z - 0.045 - 0.11)
                x, y = ax(z)
                a = np.array([x, y, z])
                d = np.array([0.0, -1.0, 0.15 * (1 - u)])
            pts.append(a)
            dirs.append(d / np.linalg.norm(d))
        P, N, L, R = lace_path(surf, pts, dirs)
        lace = laces(prefix + "Laces_" + side, L, R, N)
        cs.set_material(lace, mats["lace"])
        out.append((lace, body.transfer(cs.get_co(lace))))
        # Strap round the top of the shaft, with a buckle on the outside.
        x, y = ax(top_z - 0.035)
        P2, N2 = cs.band_around(surf, (x, y), [top_z - 0.05, top_z - 0.028], segments=40, reach=LEG_REACH)
        strap = cs.slab(prefix + "Strap_" + side, P2.transpose(1, 0, 2), N2.transpose(1, 0, 2), 0.003, gap=0.001,
                        closed=True)
        cs.set_material(strap, mats["boot_panel"])
        out.append((strap, body.transfer(cs.get_co(strap))))
        bp, bn = surf.project_from_axis((x, y, top_z - 0.039), (sx, 0.0, 0.0), reach=LEG_REACH)
        if bp is not None:
            buckle = cs.box(prefix + "Buckle_" + side, bp + bn * 0.005, (0.006, 0.022, 0.026))
            cs.set_material(buckle, mats["metal"])
            out.append((buckle, body.transfer(cs.get_co(buckle))))
        # Pull tab at the back of the shaft.
        tp, tn = surf.project_from_axis((x, y, top_z - 0.01), (0.0, 1.0, 0.0))
        if tp is not None:
            tab = cs.box(prefix + "Tab_" + side, tp + tn * 0.003 + np.array([0, 0, 0.012]), (0.018, 0.004, 0.035))
            cs.set_material(tab, mats["boot_panel"])
            out.append((tab, body.transfer(cs.get_co(tab))))
    return out


def trainers(body, mats, prefix="Trainer"):
    """Low white trainers: a padded upper to just above the ankle, a thick cupsole with a midsole line, laces, a heel
    tab, a side stripe panel."""
    top_z = 0.105
    upper = footwear_upper(body, prefix + "Upper", top_z, foot_offset=0.012, shaft_offset=0.016, hang_shaft=False)
    cs.decimate(upper, tris=4000)
    cs.bisect(upper, (0, 0, 0.045), (0, 0, 1))
    cs.bisect(upper, (0, 0, 0.065), (0, 0, 1))
    cs.set_material(upper, mats["trainer"])
    c, n = cs.face_data(upper)
    stripe = (c[:, 2] > 0.045) & (c[:, 2] < 0.065) & (np.abs(n[:, 0]) > 0.5) & (c[:, 1] > -0.07) & (c[:, 1] < 0.08)
    cs.set_material(upper, mats["trainer_accent"], stripe)
    cs.raise_faces(upper, stripe, 0.001)
    collar = c[:, 2] > top_z - 0.02
    cs.set_material(upper, mats["trainer_accent"], collar & ~stripe)
    out = [(upper, body.transfer(cs.get_co(upper)))]
    surf = cs.Surface(upper)
    for side, sx in (("l", 1), ("r", -1)):
        foot = body.bone_head("foot_" + side)
        ball = body.bone_head("ball_" + side)
        s = sole(prefix + "Sole_" + side, _side(upper, sx), heel_y=foot[1], top_z=0.034, toe_lift=0.01, grow=0.007)
        cs.set_material(s, mats["trainer_sole"])
        c2, _n2 = cs.face_data(s)
        cs.bisect(s, (0, 0, 0.012), (0, 0, 1))
        c2, _n2 = cs.face_data(s)
        cs.set_material(s, mats["trainer_midsole"], c2[:, 2] < 0.012)
        out.append((s, body.transfer(cs.get_co(s))))
        pts, dirs = [], []
        for t in np.linspace(0.0, 1.0, 6):
            a = np.array([ball[0], ball[1] + 0.03 + t * 0.075, 0.05 + t * 0.045])
            d = np.array([0.0, -0.5, 0.86])
            pts.append(a)
            dirs.append(d / np.linalg.norm(d))
        P, N, L, R = lace_path(surf, pts, dirs, half_width=0.012)
        lace = laces(prefix + "Laces_" + side, L, R, N, radius=0.0024)
        cs.set_material(lace, mats["trainer_lace"])
        out.append((lace, body.transfer(cs.get_co(lace))))
        x, y = leg_axis(body, side)(top_z)
        tp, tn = surf.project_from_axis((x, y, top_z - 0.012), (0.0, 1.0, 0.0))
        if tp is not None:
            tab = cs.box(prefix + "Tab_" + side, tp + tn * 0.004 + np.array([0, 0, 0.01]), (0.022, 0.006, 0.03))
            cs.set_material(tab, mats["trainer_accent"])
            out.append((tab, body.transfer(cs.get_co(tab))))
    return out


# --------------------------------------------------------------------------------------------------
# hands, collar, belt
# --------------------------------------------------------------------------------------------------


def gloves(body, mat, from_x=0.615, offset=0.0028, cuff_offset=0.007):
    """Leather gloves: the body's hands (fingers and all) pushed out a few mm, a looser cuff up the wrist (it goes
    under the sleeve). The body's own topology, so its own weights."""
    keep = np.abs(body.co[:, 0]) > from_x
    t = cs.smoothstep(from_x + 0.05, from_x, np.abs(body.co[:, 0]))
    off = offset + (cuff_offset - offset) * t
    obj = body.offset_copy("Gloves", off)
    W = body.W.copy()
    idx = np.nonzero(keep)[0]
    cs.delete_verts(obj, ~keep)
    cs.set_material(obj, mat)
    # Knuckle seam and back-of-hand panel: a raised strip over the knuckles.
    c, n = cs.face_data(obj)
    knuckle = (np.abs(np.abs(c[:, 0]) - 0.79) < 0.008) & (n[:, 2] > 0.4)
    cs.raise_faces(obj, knuckle, 0.0015)
    return obj, _rows_for(obj, body, W[idx])


def _rows_for(obj, body, W_kept):
    """Weights for obj after edits that may add verts: exact rows when the count still matches, else transfer."""
    if len(obj.data.vertices) == len(W_kept):
        return W_kept
    return body.transfer(cs.get_co(obj))


def neck_shape(body):
    """(centre y, half width, half depth) of the neck, from the verts the head and neck bones own at z 1.545 (lower
    down the skin belongs to spine_03 and the trapezius would be measured instead)."""
    sel = (np.abs(body.co[:, 2] - 1.545) < 0.012) & (body.family["head"] > 0.5)
    pts = body.co[sel]
    return (float(pts[:, 1].min() + pts[:, 1].max()) / 2, float(np.abs(pts[:, 0]).max()),
            float(pts[:, 1].max() - pts[:, 1].min()) / 2)


def neck_hole(body, c, pad=0.045, z_from=1.45, z_cap=1.58):
    """Face mask (face centres c) for a garment's neck opening: an ellipse round the neck, and everything above
    z_cap (the head)."""
    cy, rx, ry = neck_shape(body)
    e = (c[:, 0] / (rx + pad)) ** 2 + ((c[:, 1] - cy + 0.008) / (ry + pad)) ** 2
    return ((e < 1.0) & (c[:, 2] > z_from)) | (c[:, 2] > z_cap)


def snap_neckline(body, obj, pad=0.045, z_from=1.40, iters=6):
    """Make a neck_hole cut's ragged edge a clean curve: its boundary verts go onto the ellipse, and their heights
    are smoothed along the edge."""
    cy, rx, ry = neck_shape(body)
    cy -= 0.008
    with cs.Edit(obj) as bm:
        ring = [v for v in bm.verts if v.is_boundary and v.co.z > z_from
                and (v.co.x / (rx + pad)) ** 2 + ((v.co.y - cy) / (ry + pad)) ** 2 < 1.8]
        for v in ring:
            e = math.sqrt((v.co.x / (rx + pad)) ** 2 + ((v.co.y - cy) / (ry + pad)) ** 2)
            if e > 1e-6:
                v.co.x /= e
                v.co.y = cy + (v.co.y - cy) / e
        members = set(ring)
        for _ in range(iters):
            heights = {}
            for v in ring:
                nbrs = [e.other_vert(v) for e in v.link_edges if e.is_boundary and e.other_vert(v) in members]
                if nbrs:
                    heights[v] = (v.co.z + sum(n.co.z for n in nbrs)) / (1 + len(nbrs))
            for v, z in heights.items():
                v.co.z = z


def skin_above_collar(body, z_from=1.44, pad=0.04):
    """Body-vertex mask of the skin a collared garment leaves showing: the head, and the neck down inside the
    collar (a little past its foot, so no gap shows between)."""
    cy, rx, ry = neck_shape(body)
    co = body.co
    e = (co[:, 0] / (rx + pad)) ** 2 + ((co[:, 1] - cy + 0.008) / (ry + pad)) ** 2
    return (body.family["head"] > 0.3) | ((e < 1.0) & (co[:, 2] > z_from))


def stand_collar(body, outer, inner, z0=1.43, z1=1.60, front_drop=0.05, gap_deg=16.0, pad0=0.05, pad1=0.052,
                 thickness=0.006, name="Collar"):
    """A stand collar open at the front: lofted round the neck's shape, flared a little, tilted forward with the
    neck, lower at the front. Outer face one material, inner (the lining) another. Its foot sits inside the
    garment's neck_hole edge. Weights: chest at the base, part neck at the top."""
    cy, rx, ry = neck_shape(body)
    gap = math.radians(gap_deg)
    angles = np.linspace(-math.pi / 2 + gap, 3 * math.pi / 2 - gap, 64)   # -pi/2 is the front (-Y)
    rings = []
    levels = ((z0, pad0, -0.012), ((z0 + z1) / 2, (pad0 + pad1) / 2, -0.004), (z1, pad1, 0.004))
    for k, (z, pad, dy) in enumerate(levels):
        ring = []
        for a in angles:
            ca, sa = math.cos(a), math.sin(a)
            front = max(0.0, -sa) ** 2
            zz = z - front_drop * front * (k / 2.0)   # the top edge dips toward the front opening
            ring.append(((rx + pad) * ca, cy + dy + (ry + pad) * sa, zz))
        rings.append(ring)
    obj = cs.loft(name, np.array(rings), closed=False)
    cs.set_material(obj, outer)
    inner_mask = cs.solidify(obj, thickness)
    cs.set_material(obj, inner, inner_mask)
    co = cs.get_co(obj)
    t = np.clip((co[:, 2] - z0) / (z1 - z0), 0, 1)
    W = rigid(body, len(co), {"spine_03": 1.0})
    W[:, body.bone_index["spine_03"]] = 1.0 - 0.6 * t
    W[:, body.bone_index["neck_01"]] = 0.6 * t
    return obj, W


def rigid(body, n, bones):
    return cs.rigid(body, n, bones)


def tuck(obj, container, axis_fns, z_top, margin=0.005, blouse=0.012, blouse_h=0.05, fade=0.04, lip=0.012):
    """Tuck a garment into another (trousers into boots). Inside (below z_top - lip) no vertex is further from its
    leg's axis than the container's surface less margin. From there up the cloth bags out over the rim (so the step
    from inside to outside happens under the overhang, hidden) by up to `blouse`, then fades back to its own shape."""
    surf = cs.Surface(container)
    co = cs.get_co(obj)
    inside_top = z_top - lip
    for i in np.nonzero(co[:, 2] < z_top + blouse_h + fade)[0]:
        x, y, z = co[i]
        ax, ay = axis_fns["l" if x > 0 else "r"](min(z, z_top))
        d = np.array([x - ax, y - ay])
        r = float(np.linalg.norm(d))
        if r < 1e-6:
            continue
        d /= r
        hit, _n = surf.project_from_axis((ax, ay, min(z, z_top - 0.003)), (d[0], d[1], 0.0), reach=LEG_REACH)
        if hit is None:
            continue
        rim = float(np.hypot(hit[0] - ax, hit[1] - ay))
        if z <= inside_top:
            r_new = min(r, rim - margin)
        else:
            t = min(1.0, (z - inside_top) / (blouse_h + lip))
            over = max(r, rim + 0.004 + blouse * math.sin(math.pi * (0.25 + 0.75 * t)))
            k = 1.0 - min(1.0, max(0.0, (z - z_top - blouse_h) / fade))
            r_new = over * k + r * (1 - k)
        co[i, 0] = ax + d[0] * r_new
        co[i, 1] = ay + d[1] * r_new
    cs.set_co(obj, co)


def belt(body, surface, mats, z=1.0, height=0.042, center=TORSO_AXIS, name="Belt", buckle=True):
    """A leather belt hugging a garment's surface, a metal buckle at the front."""
    P, N = cs.band_around(surface, center, [z - height / 2, z + height / 2], segments=72)
    obj = cs.slab(name, P.transpose(1, 0, 2), N.transpose(1, 0, 2), 0.005, gap=0.0015, closed=True)
    cs.set_material(obj, mats["belt"])
    out = [(obj, body.transfer(cs.get_co(obj)))]
    if buckle:
        p, n = surface.project_from_axis((center[0], center[1], z), (0.0, -1.0, 0.0))
        frame = cs.tube(name + "Buckle", [p + n * 0.008 + np.array([dx, 0, dz]) for dx, dz in
                                          ((-0.025, -0.022), (0.025, -0.022), (0.025, 0.022), (-0.025, 0.022),
                                           (-0.025, -0.022))], 0.0035, segments=6, cap=True)
        cs.set_material(frame, mats["metal"])
        out.append((frame, body.transfer(cs.get_co(frame))))
    return out
