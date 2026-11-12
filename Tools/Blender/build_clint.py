"""Clint Barton's costume (claude-docs/blender-pipeline.md, "Clint").

    blender.exe --background --factory-startup --python Tools/Blender/build_clint.py -- [--no-render] [--views a,b]

The show's suit, from a cosplay photo of it: a knee-length long-sleeved black coat with a stand collar, wide maroon
panels down both front edges (the skirt opens below the belt), a maroon yoke over the shoulders and bands at the
elbows, thin silver piping along the panels and seams; black trousers into tall lace-up combat boots; black gloves;
a guard on the bow (left) forearm; a black quiver on his back, arrows over the left shoulder, its strap across the
chest. Not the Ronin look. Writes <assets root>/built/clint/SK_Clint.glb (+ .blend, slots json) and the renders.
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import costume as cs  # noqa: E402
import pieces  # noqa: E402

WHO = "clint"
HEM_Z = 0.47          # the coat's hem, at the knee
OPEN_Z = 0.95         # the skirt is open at the front below this
CUFF_X = 0.655        # sleeves end at the wrist
PLACKET_X = 0.011     # the closed chest's black placket between the panels
PANEL_X = 0.085       # the maroon front panels run from the opening out to here
PIPE = 0.0045         # piping width
YOKE_Z = 1.474        # the shoulder yoke is above this (the arm's top line is at 1.456 + its radius)
YOKE_END_X = 0.40     # and ends here down the arm
ELBOW = (0.445, 0.495)
WAIST_Z = 1.0
BOOT_TOP = 0.34


def make_materials(out):
    twill = cs.weave_normal("twill", out)
    grain = cs.weave_normal("grain", out, strength=1.2)
    knit = cs.weave_normal("knit", out)
    m = cs.material
    return {
        "coat": m("M_Clint_Coat", (0.006, 0.007, 0.011), 0.85, normal=twill, normal_strength=0.7),
        "panel": m("M_Clint_Panel", (0.040, 0.004, 0.013), 0.75, normal=twill, normal_strength=0.7),
        "piping": m("M_Clint_Piping", (0.14, 0.14, 0.15), 0.45),
        "lining": m("M_Clint_Lining", (0.035, 0.006, 0.017), 0.6),
        "trousers": m("M_Clint_Trousers", (0.011, 0.011, 0.012), 0.85, normal=twill, normal_strength=0.6),
        "boot": m("M_Clint_Boot", (0.014, 0.013, 0.012), 0.38, normal=grain, normal_strength=0.5),
        "boot_panel": m("M_Clint_BootPanel", (0.010, 0.010, 0.010), 0.3, normal=grain, normal_strength=0.6),
        "sole": m("M_Clint_Sole", (0.008, 0.008, 0.008), 0.9),
        "lace": m("M_Clint_Lace", (0.012, 0.012, 0.012), 0.8, normal=knit, normal_strength=0.4),
        "metal": m("M_Clint_Metal", (0.35, 0.35, 0.37), 0.35, metallic=1.0),
        "glove": m("M_Clint_Glove", (0.009, 0.009, 0.009), 0.5, normal=grain, normal_strength=0.3),
        "guard": m("M_Clint_Guard", (0.016, 0.016, 0.018), 0.28, normal=grain, normal_strength=0.4),
        "belt": m("M_Clint_Belt", (0.014, 0.011, 0.009), 0.45, normal=grain, normal_strength=0.6),
        "quiver": m("M_Clint_Quiver", (0.012, 0.012, 0.013), 0.42, normal=grain, normal_strength=0.6),
        "arrow": m("M_Clint_Arrow", (0.025, 0.025, 0.028), 0.3, metallic=0.6),
        "nock": m("M_Clint_Nock", (0.16, 0.025, 0.10), 0.4),
    }


def head_materials(body, out, hair_tint, skin_file="T_Superhero_Male_Dark.png"):
    """Skin (the pack's texture and normals), eyes, brows and hair tinted from the pack's hair texture."""
    import bpy  # noqa: PLC0415

    root = body.root
    skin_img = bpy.data.images.load(cs.pack_file(root, cs.TEXTURES + (skin_file,)), check_existing=True)
    skin_n = bpy.data.images.load(cs.pack_file(root, cs.GL_NORMALS + ("T_Superhero_Male_Normal.png",)),
                                  check_existing=True)
    skin_n.colorspace_settings.name = "Non-Color"
    eye_img = bpy.data.images.load(cs.pack_file(root, cs.TEXTURES + ("T_Eye_Brown.png",)), check_existing=True)
    hair_img = cs.tinted_image(cs.pack_file(root, cs.HAIR_TEXTURES + ("T_Hair_1_BaseColor.png",)),
                               "T_{0}_Hair".format(WHO.capitalize()), hair_tint,
                               os.path.join(out, "T_Hair.png"), keep_detail=0.8)
    m = cs.material
    return {
        "skin": m("M_{0}_Skin".format(WHO.capitalize()), (0.8, 0.6, 0.5), 0.55, normal=skin_n, base_image=skin_img),
        "eyes": m("M_{0}_Eyes".format(WHO.capitalize()), (1, 1, 1), 0.2, base_image=eye_img),
        "hair": m("M_{0}_Hair".format(WHO.capitalize()), hair_tint, 0.7, base_image=hair_img, alpha_clip=True),
    }


def skirt_solid(body):
    """A closed flared solid from the ribs to below the hem: merged into the coat's volume, it is the skirt hanging
    clear of the legs."""
    cy = pieces.TORSO_AXIS[1]
    hips = [pieces.extents(body, z) for z in np.linspace(0.84, 1.0, 9)]
    hx = max(max(-e[0], e[1]) for e in hips)
    hyf = max(cy - e[2] for e in hips)
    hyb = max(e[3] - cy for e in hips)
    rings = []
    for z in np.linspace(1.14, HEM_Z - 0.05, 34):
        if z >= 0.94:
            e = pieces.extents(body, z)
            k = np.clip((z - 0.94) / 0.2, 0, 1)
            ax = (1 - k) * (hx + 0.03) + k * (max(-e[0], e[1]) + 0.026)
            ayf = (1 - k) * (hyf + 0.03) + k * (cy - e[2] + 0.026)
            ayb = (1 - k) * (hyb + 0.03) + k * (e[3] - cy + 0.026)
        else:
            t = (0.94 - z) / (0.94 - HEM_Z)
            ax = hx + 0.03 + 0.05 * t
            ayf = hyf + 0.03 + 0.035 * t
            ayb = hyb + 0.03 + 0.045 * t
        rings.append(pieces.superellipse(0.0, cy, ax, ayf, ayb, z))
    return cs.loft("Skirt", rings, closed=True, cap_start=True, cap_end=True)


def fold_field(body):
    """Folds pressed into the coat: hanging folds in the skirt, bunching at the cuffs, creases at the elbows."""
    cy = pieces.TORSO_AXIS[1]

    def fn(co, n):
        x, y, z = co[:, 0], co[:, 1], co[:, 2]
        theta = np.arctan2(y - cy, x)
        skirt = cs.smoothstep(0.98, HEM_Z, z) * (np.abs(x) < 0.4)
        d = 0.016 * skirt * cs.fold_noise(theta * 7.0 + 0.8 * z, 11)
        ax = np.abs(x)
        wrist = cs.smoothstep(0.54, 0.60, ax) * cs.smoothstep(CUFF_X + 0.01, CUFF_X - 0.03, ax)
        d += 0.0035 * wrist * np.sin(ax / 0.018 * 2 * math.pi + cs.fold_noise(np.arctan2(z - 1.456, y), 3))
        elbow = np.exp(-((ax - 0.47) / 0.04) ** 2) * (n[:, 1] < -0.2) * (ax > 0.3)
        d += 0.002 * elbow * np.sin((ax + 0.4 * (z - 1.456)) / 0.025 * 2 * math.pi)
        return d

    return fn


def build_coat(body, mats):
    fam = body.family
    off = 0.022 * fam["torso"] + 0.018 * fam["arm"] + 0.018 * fam["hand"] + 0.02 * fam["leg"] + 0.012 * fam["head"]
    torso = (fam["torso"] > 0.5) & (np.abs(body.co[:, 0]) < 0.24)
    hangs = [(torso, lambda z: pieces.TORSO_AXIS, 1.37, 0.85, 0.97, 0.3)]
    coat = pieces.shell(body, "Coat", off, hangs=hangs, extras=[skirt_solid(body)], voxel=0.008, smooth=6)
    pieces.iron(body, coat, torso_iters=60, limb_iters=16)
    cs.cut_and_delete(coat, (0, 0, HEM_Z), (0, 0, 1), lambda c, n: c[:, 2] < HEM_Z)
    for sx in (1, -1):
        cs.cut_and_delete(coat, (sx * CUFF_X, 0, 0), (1, 0, 0), lambda c, n, sx=sx: sx * c[:, 0] > CUFF_X)
    c, n = cs.face_data(coat)
    cs.delete_faces(coat, pieces.neck_hole(body, c))
    pieces.keep_largest(coat)
    cs.displace(coat, fold_field(body))
    # The front opening below the belt.
    def front_low(c, n):
        return (np.abs(c[:, 0]) < 0.05) & (c[:, 1] < 0.0) & (c[:, 2] < OPEN_Z + 0.03)

    for x0 in (-0.009, 0.009):
        cs.bisect(coat, (x0, 0, 0), (1, 0, 0), front_low)
    cs.bisect(coat, (0, 0, OPEN_Z), (0, 0, 1), lambda c, n: (np.abs(c[:, 0]) < 0.05) & (c[:, 1] < 0.0))
    c, n = cs.face_data(coat)
    cs.delete_faces(coat, (np.abs(c[:, 0]) < 0.009) & (c[:, 1] < 0.0) & (c[:, 2] < OPEN_Z))
    cs.decimate(coat, tris=7500)
    pieces.snap_neckline(body, coat)
    trim_coat(coat, mats)
    inner = cs.solidify(coat, 0.005)
    cs.set_material(coat, mats["lining"], inner)
    for key, height in (("panel", 0.0015), ("piping", 0.0025)):
        cs.raise_faces(coat, cs.material_mask(coat, mats[key]) & (cs.get_face_int(coat, "shell") == 0), height)
    cs.box_uv(coat, 0.04)
    cs.mark_sharp(coat, 50)
    return coat, coat_weights(body, coat)


def trim_coat(coat, mats):
    """Maroon panels and silver piping, cut on clean planes and raised off the coat."""
    for sx in (1, -1):
        for x0 in (PLACKET_X, PANEL_X, PANEL_X + PIPE):
            cs.bisect(coat, (sx * x0, 0, 0), (1, 0, 0), lambda c, n: (c[:, 1] < 0.02) & (np.abs(c[:, 0]) < 0.2))
    for z0 in (YOKE_Z - PIPE, YOKE_Z):
        cs.bisect(coat, (0, 0, z0), (0, 0, 1), lambda c, n: (np.abs(c[:, 0]) > 0.06)
                  & (np.abs(c[:, 0]) < YOKE_END_X + 0.02) & (c[:, 2] > 1.40))
    for sx in (1, -1):
        for x0 in (YOKE_END_X, YOKE_END_X + PIPE, ELBOW[0] - PIPE, ELBOW[0], ELBOW[1], ELBOW[1] + PIPE,
                   CUFF_X - 0.035, CUFF_X - 0.035 + PIPE):
            cs.bisect(coat, (sx * x0, 0, 0), (1, 0, 0), lambda c, n: np.abs(c[:, 0]) > 0.3)
    # Side seams (from the armpit to the hem) and a curved chest seam from the yoke down to the panels.
    for y0 in (0.017, 0.017 + PIPE):
        cs.bisect(coat, (0, y0, 0), (0, 1, 0), lambda c, n: (np.abs(n[:, 0]) > 0.4) & (np.abs(c[:, 0]) < 0.32)
                  & (c[:, 2] < 1.32))
    chest_dirs = []
    for sx in (1, -1):
        d = np.array([sx * 0.96, 0.0, 0.28])   # a line from the yoke at the shoulder down onto the panel
        d /= np.linalg.norm(d)
        chest_dirs.append((sx, d))
        for k in (0.0, PIPE):
            cs.bisect(coat, d * (np.dot(d, (sx * 0.13, 0.0, 1.40)) + k), d,
                      lambda c, n, sx=sx: (c[:, 1] < 0.0) & (sx * c[:, 0] > PANEL_X) & (sx * c[:, 0] < 0.2)
                      & (c[:, 2] > 1.15))

    c, n = cs.face_data(coat)
    ax = np.abs(c[:, 0])
    front = (c[:, 1] < 0.0) & (n[:, 1] < 0.2)
    panel = front & (ax < PANEL_X) & (c[:, 2] < 1.5) & ~((ax < PLACKET_X) & (c[:, 2] > OPEN_Z))
    pipe = front & (ax > PANEL_X) & (ax < PANEL_X + PIPE) & (c[:, 2] < 1.5)
    yoke = (c[:, 2] > YOKE_Z) & (ax > 0.06) & (ax < YOKE_END_X) & (n[:, 2] > -0.2)
    pipe |= (c[:, 2] > YOKE_Z - PIPE) & (c[:, 2] < YOKE_Z) & (ax > 0.06) & (ax < YOKE_END_X + PIPE) & (n[:, 2] > -0.2)
    pipe |= (ax > YOKE_END_X) & (ax < YOKE_END_X + PIPE) & (c[:, 2] > YOKE_Z - PIPE) & (n[:, 2] > -0.2)
    elbow = (ax > ELBOW[0]) & (ax < ELBOW[1])
    pipe |= ((ax > ELBOW[0] - PIPE) & (ax < ELBOW[0])) | ((ax > ELBOW[1]) & (ax < ELBOW[1] + PIPE))
    pipe |= (ax > CUFF_X - 0.035) & (ax < CUFF_X - 0.035 + PIPE)
    sides = (np.abs(n[:, 0]) > 0.4) & (ax < 0.32) & (c[:, 2] < 1.32)
    pipe |= sides & (c[:, 1] > 0.017) & (c[:, 1] < 0.017 + PIPE)
    for sx, d in chest_dirs:
        s = c @ d - np.dot(d, (sx * 0.13, 0.0, 1.40))
        pipe |= (c[:, 1] < 0.0) & (sx * c[:, 0] > PANEL_X) & (sx * c[:, 0] < 0.2) & (c[:, 2] > 1.15) \
            & (s > 0) & (s < PIPE)
    cs.set_material(coat, mats["coat"])
    cs.set_material(coat, mats["panel"], panel | yoke | elbow)
    cs.set_material(coat, mats["piping"], pipe)


def coat_weights(body, coat):
    """The body's weights, except the skirt: it hangs from the pelvis and follows each thigh only partly (more at
    the front, more toward the hem), so a stride swings it instead of stretching it round the leg."""
    co = cs.get_co(coat)
    W = body.transfer(co)
    x, y, z = co[:, 0], co[:, 1], co[:, 2]
    t = cs.smoothstep(0.98, HEM_Z, z)
    front = cs.smoothstep(0.10, -0.06, y)
    leg = 0.62 * t ** 1.2 * (0.55 + 0.45 * front)
    side = cs.smoothstep(-0.07, 0.07, x)
    S = np.zeros_like(W)
    S[:, body.bone_index["pelvis"]] = 1.0 - leg
    S[:, body.bone_index["thigh_l"]] = leg * side
    S[:, body.bone_index["thigh_r"]] = leg * (1 - side)
    blend = cs.smoothstep(1.02, 0.93, z)[:, None]
    W = W * (1 - blend) + S * blend
    edges = cs.get_edges(coat)
    W = cs.smooth_values(W, edges, 4, 0.5)
    armpit = np.zeros(len(co), bool)
    for side_name in ("l", "r"):
        h = body.bone_head("upperarm_" + side_name)
        armpit |= np.linalg.norm(co - h, axis=1) < 0.14
    W = cs.smooth_values(W, edges, 12, 0.5, mask=armpit)
    return W


def build_trousers(body, mats, boots_upper, boot_top):
    """Black trousers, a little loose, tucked into the boots (and bagging out over their tops)."""
    fam = body.family
    off = 0.009 * (fam["leg"] + fam["torso"]) + 0.006 * fam["foot"]
    hangs = []
    for side, sx in (("l", 1), ("r", -1)):
        mask = (np.sign(body.co[:, 0]) == sx) & (fam["leg"] > 0.5)
        hangs.append((mask, pieces.leg_axis(body, side), 0.62, 0.12, 0.95, 0.3))
    tr = pieces.shell(body, "Trousers", off, hangs=hangs, voxel=0.008, smooth=6, keep=body.co[:, 2] < 1.12)
    pieces.iron(body, tr, torso_iters=10, limb_iters=12, z_range=(0.8, 1.1))
    cs.cut_and_delete(tr, (0, 0, 1.03), (0, 0, 1), lambda c, n: c[:, 2] > 1.03)
    cs.cut_and_delete(tr, (0, 0, 0.21), (0, 0, 1), lambda c, n: c[:, 2] < 0.21)
    pieces.keep_islands(tr, 300)
    axes = {s: pieces.leg_axis(body, s) for s in ("l", "r")}

    def folds(co, n):
        z = co[:, 2]
        ly = np.array([axes["l" if x > 0 else "r"](zz)[1] for x, zz in zip(co[:, 0], z)])
        knee_back = np.exp(-((z - 0.56) / 0.05) ** 2) * (co[:, 1] > ly)
        d = 0.003 * knee_back * np.sin(z / 0.022 * 2 * math.pi + co[:, 0] * 20)
        crotch = np.exp(-((z - 0.86) / 0.05) ** 2) * (co[:, 1] < ly) * (np.abs(co[:, 0]) < 0.12)
        d += 0.0025 * crotch * np.sin((z + 0.6 * np.abs(co[:, 0])) / 0.03 * 2 * math.pi)
        return d

    cs.displace(tr, folds)
    pieces.tuck(tr, boots_upper, axes, boot_top)
    cs.taubin(tr, 2)
    cs.decimate(tr, tris=6000)
    cs.set_material(tr, mats["trousers"])
    cs.solidify(tr, 0.003)
    cs.box_uv(tr, 0.04)
    cs.mark_sharp(tr, 60)
    W = cs.smooth_values(body.transfer(cs.get_co(tr)), cs.get_edges(tr), 3, 0.5)
    return tr, W


def build_guard(body, surf, mats):
    """A rigid guard on the bow (left) forearm over the sleeve: wraps the inside of the arm (open on top), two straps."""
    ay = pieces.arm_axis(body, "l")
    out = []
    xs = np.linspace(0.505, 0.632, 12)
    angles = np.radians(np.linspace(-135, 135, 31))
    P, N = [], []
    for x in xs:
        y0, z0 = ay(x)
        row_p, row_n = [], []
        for a in angles:
            p, n = surf.project_from_axis((x, y0, z0), (0.0, math.sin(a), -math.cos(a)))
            row_p.append(p)
            row_n.append(n)
        P.append(row_p)
        N.append(row_n)
    guard = cs.slab("Guard", np.array(P), np.array(N), 0.004, gap=0.004)
    cs.set_material(guard, mats["guard"])
    out.append((guard, cs.rigid(body, len(guard.data.vertices), {"lowerarm_l": 1.0})))
    for k, x in enumerate((0.53, 0.607)):
        y0, z0 = ay(x)
        P, N = [], []
        for a in np.radians(np.linspace(0, 360, 40, endpoint=False)):
            row_p, row_n = [], []
            for dx in (-0.008, 0.008):
                p, n = surf.project_from_axis((x + dx, y0, z0), (0.0, math.sin(a), -math.cos(a)))
                row_p.append(p)
                row_n.append(n)
            P.append(row_p)
            N.append(row_n)
        strap = cs.slab("GuardStrap%d" % k, np.array(P), np.array(N), 0.003, gap=0.0085, closed=True)
        cs.set_material(strap, mats["belt"])
        out.append((strap, cs.rigid(body, len(strap.data.vertices), {"lowerarm_l": 1.0})))
    return out


def build_quiver(body, surf, mats):
    """A black leather quiver across the back, mouth over the left shoulder, ten arrows; a strap across the chest."""
    out = []
    bottom, top = np.array([-0.10, 0.0, 1.0]), np.array([0.135, 0.0, 1.47])
    r0, r1, squash = 0.048, 0.064, 0.72
    ts = np.linspace(0, 1, 9)
    need = []
    for t in ts:
        p = bottom + (top - bottom) * t
        hit, _n = surf.project_from_axis((p[0], 0.02, p[2]), (0, 1, 0))
        need.append((hit[1] if hit is not None else 0.15) + (r0 + (r1 - r0) * t) * squash + 0.01)
    yb = max(need[i] - (need[-1] - need[0]) * ts[i] for i in range(len(ts)))
    bottom[1] = yb
    top[1] = yb + (need[-1] - need[0])
    top[1] = max(top[1], need[-1])
    bottom[1] = max(bottom[1], need[0])
    A = top - bottom
    length = np.linalg.norm(A)
    A /= length
    u = np.cross(A, (0, 1, 0))
    u /= np.linalg.norm(u)
    v = np.cross(u, A)
    ang = np.linspace(0, 2 * math.pi, 28, endpoint=False)
    rings = []
    for t in np.concatenate([[0.0, 0.015], np.linspace(0.05, 1.0, 12), [1.0]]):
        r = r0 + (r1 - r0) * t
        if len(rings) == 13:
            r += 0.006   # a rolled lip at the mouth
        c = bottom + A * length * t
        rings.append(c + r * (np.cos(ang)[:, None] * u + squash * np.sin(ang)[:, None] * v))
    body_q = cs.loft("Quiver", rings, closed=True, cap_start=True)
    cs.set_material(body_q, mats["quiver"])
    cs.solidify(body_q, 0.003)
    # Stitched leather bands round the quiver.
    c, _n = cs.face_data(body_q)
    s = (c - bottom) @ A / length
    band = (((s > 0.18) & (s < 0.22)) | ((s > 0.78) & (s < 0.82))) & (cs.get_face_int(body_q, "shell") == 0)
    cs.set_material(body_q, mats["belt"], band)
    cs.raise_faces(body_q, band, 0.002)
    cs.box_uv(body_q, 0.04)
    rig = {"spine_03": 0.65, "spine_02": 0.35}
    out.append((body_q, cs.rigid(body, len(body_q.data.vertices), rig)))
    # Arrows: shafts from deep in the quiver to a fan above the mouth, nocks, three vanes each.
    parts = []
    nocks = []
    grid = [(-0.5, -0.4), (0.0, -0.45), (0.5, -0.4), (-0.6, 0.1), (-0.2, 0.05), (0.2, 0.05), (0.6, 0.1),
            (-0.35, 0.5), (0.1, 0.5), (0.45, 0.45)]
    for k, (gu, gv) in enumerate(grid):
        off = (gu * u + gv * squash * v) * r1 * 0.8
        start = top - A * 0.3 + off
        tip = top + A * (0.16 + 0.02 * ((k * 7) % 3)) + off * 1.6
        d = tip - start
        d /= np.linalg.norm(d)
        parts.append(cs.tube("Shaft", [start, tip], 0.0038, segments=8, cap=True))
        nocks.append(cs.tube("Nock", [tip, tip + d * 0.014], 0.0048, segments=8, cap=True))
        side = np.cross(d, v)
        side /= np.linalg.norm(side)
        for j in range(3):
            a = 2 * math.pi * j / 3 + k
            rad = side * math.cos(a) + np.cross(d, side) * math.sin(a)
            tan = np.cross(d, rad)
            centre = tip - d * 0.055 + rad * 0.011
            vane = cs.box("Vane", centre, (0.075, 0.014, 0.0012), rotation=np.column_stack([d, rad, tan]))
            parts.append(vane)
    shafts = cs.join(parts, "Arrows")
    cs.set_material(shafts, mats["arrow"])
    out.append((shafts, cs.rigid(body, len(shafts.data.vertices), rig)))
    nock = cs.join(nocks, "Nocks")
    cs.set_material(nock, mats["nock"])
    out.append((nock, cs.rigid(body, len(nock.data.vertices), rig)))
    # The strap: from the quiver's mouth over the left shoulder, across the chest, round the right side to its foot.
    path = np.array([top - A * 0.06, (0.16, 0.10, 1.52), (0.15, 0.0, 1.56), (0.12, -0.10, 1.45), (0.04, -0.14, 1.30),
                     (-0.06, -0.14, 1.16), (-0.15, -0.10, 1.05), (-0.21, 0.0, 1.0), (-0.17, 0.12, 0.99),
                     bottom + A * 0.06])
    seg = np.linalg.norm(np.diff(path, axis=0), axis=1)
    tt = np.concatenate([[0], np.cumsum(seg)])
    samples = np.linspace(0, tt[-1], 70)
    pts = np.stack([np.interp(samples, tt, path[:, i]) for i in range(3)], axis=1)
    pts = cs.smooth_values(pts, np.array([(i, i + 1) for i in range(len(pts) - 1)]), 6, 0.5,
                           mask=np.r_[False, np.ones(len(pts) - 2, bool), False])
    def onto(p):
        """The coat's outer surface under p: a ray in from outside, toward the spine."""
        a = np.array([0.0, pieces.TORSO_AXIS[1], min(p[2], 1.40)])
        hit, n = surf.project_from_axis(a, p - a, reach=0.5)
        return (hit, n) if hit is not None else surf.nearest(p)

    P, N = [], []
    for i, p in enumerate(pts):
        q, n = onto(p)
        t = pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]
        side = np.cross(n, t)
        side /= np.linalg.norm(side)
        row_p, row_n = [], []
        for w in (-0.019, 0.0, 0.019):
            q2, n2 = onto(q + side * w)
            row_p.append(q2)
            row_n.append(n2)
        P.append(row_p)
        N.append(row_n)
    strap = cs.slab("QuiverStrap", np.array(P), np.array(N), 0.004, gap=0.004)
    cs.set_material(strap, mats["belt"])
    cs.box_uv(strap, 0.04)
    W = body.transfer(cs.get_co(strap))
    out.append((strap, cs.smooth_values(W, cs.get_edges(strap), 4, 0.5)))
    return out


def build(args):
    body = cs.Body(args.assets_root)
    mats = make_materials(args.out)
    head = head_materials(body, args.out, hair_tint=(0.085, 0.052, 0.030))
    pieces_out = []

    coat, W = build_coat(body, mats)
    pieces_out.append((coat, W))
    coat_surface = cs.Surface(coat)
    pieces_out.append(pieces.stand_collar(body, mats["coat"], mats["panel"]))
    pieces_out += pieces.belt(body, coat_surface, mats, z=WAIST_Z, height=0.04)
    boots = pieces.combat_boots(body, mats, top_z=BOOT_TOP)
    pieces_out.append(build_trousers(body, mats, boots[0][0], BOOT_TOP))
    pieces_out += boots
    pieces_out.append(pieces.gloves(body, mats["glove"]))
    pieces_out += build_guard(body, coat_surface, mats)
    pieces_out += build_quiver(body, coat_surface, mats)

    for obj, W in pieces_out:
        if len(obj.data.uv_layers) == 0:
            cs.box_uv(obj, 0.04)
        if obj.name not in ("Coat", "Trousers"):
            cs.mark_sharp(obj, 45)
        cs.skin(obj, body, W)

    # Skin only where nothing covers it: the head and neck.
    body.keep_skin(pieces.skin_above_collar(body))
    cs.set_material(body.skin, head["skin"])
    body.eyes.data.materials.clear()
    body.eyes.data.materials.append(head["eyes"])
    body.brows.data.materials.clear()
    body.brows.data.materials.append(head["hair"])
    hair = body.add_hair("Hair_SimpleParted.gltf", head["hair"])
    beard = body.add_hair("Hair_Beard.gltf", head["hair"])

    parts = [body.skin, body.eyes, body.brows, hair, beard] + [p for p, _w in pieces_out]
    cs.finish(body, parts, WHO, args)


if __name__ == "__main__":
    build(cs.parse_args(WHO))
