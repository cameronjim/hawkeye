"""Kate Bishop's hero suit (claude-docs/blender-pipeline.md, "Kate").

    blender.exe --background --factory-startup --python Tools/Blender/build_kate.py -- [--no-render] [--views a,b]

On the pack's Superhero_Female body, made a little more athletic (bust, hips and seat brought in under the clothes):
a fitted long-sleeved top in deep purple with black side panels running up under the sleeves, a black yoke over the
shoulders whose lower edge is a chevron on the chest and the back, light purple piping along it, black cuffs and a
high purple stand collar; black fitted trousers with a thin purple stripe down the outside of each leg, tucked into
black mid-calf combat boots with flat soles; a utility belt with pouches and a strap with a pouch round the right
thigh; a black archery guard on the left (bow) forearm, a fingerless glove on the bow hand and a three-finger shooting
glove on the draw hand; a smaller version of Clint's quiver with purple-fletched arrows, mouth over the right shoulder
(the draw hand's), its strap across the chest; dark brown hair pulled back into a high ponytail of separate locks.
The face is the pack's (an original face, nobody's likeness). Writes <assets root>/built/kate/SK_Kate.glb (+ .blend,
slots json, textures) and the renders, including kate_clint_scale (her beside Clint as the game draws them).
"""

import math
import os
import sys

import bmesh
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_thug as bt  # noqa: E402
import costume as cs  # noqa: E402
import pieces  # noqa: E402

WHO = "kate"
NECK_Z = 1.51          # where pieces.neck_shape measures her neck (neck_01 at 1.485, Head at 1.55)
HEM_Z = 0.953          # the top's hem, hidden under the belt
WAIST_Z = 0.968        # the belt
TROUSER_TOP = 1.0
CUFF_X = 0.598         # sleeves end at the wrist (hand_l at 0.641)
CUFF_W = 0.032         # black cuff band
BOOT_TOP = 0.30        # mid-calf (calf_l at 0.535, foot_l at 0.071)
GLOVE_X = 0.555        # gloves start up the wrist, under the sleeve
YOKE_V = (1.235, 1.285)   # the chevron's point on the chest (front) and on the back
YOKE_SLOPE = 0.78      # its edges rise this much per metre out to the shoulder
YOKE_END_X = 0.235     # the yoke stops on the shoulder cap
SIDE_Y = (-0.035, 0.050)   # the black side panels between these y planes, under the arms
UNDER_Z = 1.392        # and along the underside of each sleeve below this
UNDER_END_X = 0.40     # to the elbow (on the forearm it hid the black guard)
PIPE = 0.005
PIPE_V = PIPE * math.sqrt(1 + 0.78 ** 2)   # the piping's height under the chevron's sloped edge
STRIPE_Y = (0.034, 0.043)   # the trousers' purple stripe, on the outside of the leg
THIGH_STRAP_Z = 0.70
GAME_SCALE = 0.93      # BP_Kate's driver scale; the look takes it (claude-docs/animation.md)

# The suit's purple: sRGB #541A84 in linear, #5B2A86 with less green (that read lavender under the studio's lights
# and AgX). A darker one for the lining, a lighter one for piping and stripes.
PURPLE = (0.090, 0.010, 0.228)
PURPLE_DARK = (0.042, 0.009, 0.098)
PURPLE_LIGHT = (0.27, 0.107, 0.55)
BLACK = (0.012, 0.011, 0.014)
HAIR = (0.100, 0.056, 0.030)   # dark brown (darker read black under the lights)


def make_materials(out):
    twill = cs.weave_normal("twill", out)
    knit = cs.weave_normal("knit", out)
    grain = cs.weave_normal("grain", out, strength=1.2)
    m = cs.material
    return {
        "top": m("M_Kate_Top", PURPLE, 0.85, normal=twill, normal_strength=0.6),
        "panel": m("M_Kate_Panel", BLACK, 0.86, normal=knit, normal_strength=0.5),
        "piping": m("M_Kate_Piping", PURPLE_LIGHT, 0.8, normal=twill, normal_strength=0.4),
        "lining": m("M_Kate_Lining", PURPLE_DARK, 0.85),
        "trousers": m("M_Kate_Trousers", (0.013, 0.012, 0.014), 0.86, normal=twill, normal_strength=0.6),
        "boot": m("M_Kate_Boot", (0.014, 0.013, 0.012), 0.45, normal=grain, normal_strength=0.5),
        "boot_panel": m("M_Kate_BootPanel", (0.010, 0.010, 0.010), 0.4, normal=grain, normal_strength=0.6),
        "sole": m("M_Kate_Sole", (0.008, 0.008, 0.008), 0.9),
        "lace": m("M_Kate_Lace", (0.012, 0.012, 0.012), 0.85, normal=knit, normal_strength=0.4),
        "metal": m("M_Kate_Metal", (0.35, 0.35, 0.37), 0.35, metallic=1.0),
        "glove": m("M_Kate_Glove", (0.009, 0.009, 0.009), 0.55, normal=grain, normal_strength=0.3),
        "guard": m("M_Kate_Guard", (0.012, 0.012, 0.013), 0.36, normal=grain, normal_strength=0.4),
        "belt": m("M_Kate_Belt", (0.011, 0.010, 0.010), 0.5, normal=grain, normal_strength=0.6),
        "quiver": m("M_Kate_Quiver", (0.012, 0.011, 0.013), 0.5, normal=grain, normal_strength=0.6),
        "arrow": m("M_Kate_Arrow", (0.025, 0.025, 0.028), 0.3, metallic=0.6),
        "nock": m("M_Kate_Nock", PURPLE_LIGHT, 0.5),
        "vane": m("M_Kate_Vane", PURPLE, 0.6),
        "zip": m("M_Kate_Zip", (0.25, 0.25, 0.26), 0.35, metallic=1.0),
    }


def hair_textures(out):
    """A strand texture (dark brown, lighter and darker strands, the alpha broken into strands at both ends of v so a
    lock's tip and the hairline feather when clipped) and its normal map, both tiling in u."""

    size = 512
    rng = np.random.default_rng(7)
    x = np.arange(size) / size
    # Strands across u: a sum of fine stripes at random phases, tileable.
    strands = np.zeros(size)
    for f in (24, 37, 61, 89, 131):
        strands += np.sin(2 * math.pi * (f * x + rng.uniform()))
    strands = (strands - strands.min()) / (strands.max() - strands.min())
    v = np.arange(size)[:, None] / size
    tone = 0.62 + 0.55 * strands[None, :] + 0.08 * cs.tileable_noise(size, 11, 4.0)
    rgb = np.clip(tone[:, :, None] * np.asarray(HAIR)[None, None, :], 0, 1)
    # Ends: each strand stops at its own length (v from the root 0 to the tip 1).
    length = 0.86 + 0.13 * np.clip(cs.tileable_noise(size, 12, 0.6)[0], -1.5, 1.5) / 1.5
    start = 0.04 * (1 + np.clip(cs.tileable_noise(size, 13, 0.6)[0], -1.5, 1.5) / 1.5)
    alpha = ((v < length[None, :]) & (v > start[None, :])).astype(np.float64)
    alpha[(v > 0.08)[:, 0] & (v < 0.84)[:, 0]] = 1.0
    img = cs.save_image("T_Kate_Hair", np.concatenate([rgb, alpha[:, :, None]], axis=2),
                        os.path.join(out, "T_Hair.png"), non_color=False)
    img.colorspace_settings.name = "sRGB"
    h = np.broadcast_to(strands[None, :], (size, size))   # grooves along the strands only (noise read as crinkle)
    nrm = cs.save_image("T_N_strand", cs.normal_from_height(h, 3.0), os.path.join(out, "T_N_strand.png"))
    return img, nrm


def head_materials(body, out):
    import bpy  # noqa: PLC0415

    root = body.root
    skin_img = bpy.data.images.load(cs.pack_file(root, cs.TEXTURES + ("T_Superhero_Female_Light_BaseColor.png",)),
                                    check_existing=True)
    skin_n = bpy.data.images.load(cs.pack_file(root, cs.GL_NORMALS + ("T_Superhero_Female_Normal.png",)),
                                  check_existing=True)
    skin_n.colorspace_settings.name = "Non-Color"
    eye_img = bpy.data.images.load(cs.pack_file(root, cs.TEXTURES + ("T_Eye_Brown.png",)), check_existing=True)
    brow_img = cs.tinted_image(cs.pack_file(root, cs.HAIR_TEXTURES + ("T_Hair_2_BaseColor.png",)), "T_Kate_Brows",
                               HAIR, os.path.join(out, "T_Brows.png"), keep_detail=0.6)
    hair_img, hair_n = hair_textures(out)
    m = cs.material
    return {
        "skin": m("M_Kate_Skin", (0.8, 0.6, 0.5), 0.55, normal=skin_n, base_image=skin_img),
        "eyes": m("M_Kate_Eyes", (1, 1, 1), 0.2, base_image=eye_img),
        "brows": m("M_Kate_Brows", HAIR, 0.7, base_image=brow_img, alpha_clip=True),
        "hair": m("M_Kate_Hair", HAIR, 0.6, base_image=hair_img, alpha_clip=True, normal=hair_n,
                  normal_strength=0.35),
        "tie": m("M_Kate_HairTie", (0.010, 0.010, 0.011), 0.6),
    }


# --------------------------------------------------------------------------------------------------
# the body
# --------------------------------------------------------------------------------------------------


def athletic(body):
    """The pack's Superhero_Female is an hourglass; Kate is an athlete. Under the clothes only (that skin is deleted),
    bring the bust in by about half its projection, the hips in and the seat back a little. The skeleton and weights
    are untouched; the garments are built round this shape."""
    co = body.co.copy()
    x, y, z = co[:, 0], co[:, 1], co[:, 2]
    trunk = (body.family["torso"] + body.family["leg"]) > 0.3
    w = np.zeros(len(co))
    for sx in (1, -1):
        w = np.maximum(w, np.exp(-((x - sx * 0.085) ** 2 + (z - 1.31) ** 2) / 0.075 ** 2))
    proj = np.maximum(0.0, -0.100 - y)
    co[:, 1] += 0.5 * proj * w * trunk
    hips = cs.smoothstep(0.74, 0.86, z) * cs.smoothstep(1.06, 0.96, z) * trunk
    ax = np.abs(x)
    co[:, 0] -= np.sign(x) * 0.28 * np.maximum(0.0, ax - 0.135) * hips
    seat = cs.smoothstep(0.76, 0.86, z) * cs.smoothstep(1.06, 0.98, z) * trunk
    co[:, 1] -= 0.3 * np.maximum(0.0, y - 0.10) * seat
    cs.set_co(body.reference, co)
    body.co = co
    body.normals = cs.get_vnormals(body.reference)
    body.surface = cs.Surface(body.reference)


# --------------------------------------------------------------------------------------------------
# the top
# --------------------------------------------------------------------------------------------------


def fold_field(body):
    def fn(co, n):
        ax = np.abs(co[:, 0])
        z = co[:, 2]
        d = np.zeros(len(co))
        wrist = cs.smoothstep(0.50, 0.55, ax) * cs.smoothstep(CUFF_X - CUFF_W, CUFF_X - CUFF_W - 0.02, ax)
        d += 0.0018 * wrist * np.sin(ax / 0.016 * 2 * math.pi + cs.fold_noise(np.arctan2(z - 1.418, co[:, 1]), 3))
        elbow = np.exp(-((ax - 0.40) / 0.035) ** 2) * (n[:, 1] < -0.2) * (ax > 0.3)
        d += 0.0015 * elbow * np.sin((ax + 0.4 * (z - 1.418)) / 0.022 * 2 * math.pi)
        waist = np.exp(-((z - (WAIST_Z + 0.045)) / 0.025) ** 2) * (ax < 0.2)
        d += 0.0012 * waist * cs.fold_noise(np.arctan2(co[:, 1] - 0.02, co[:, 0]) * 9, 5)
        return d

    return fn


def yoke_edge(c, back):
    """Height of the chevron's lower edge at each face centre's |x|."""
    return np.where(back, YOKE_V[1], YOKE_V[0]) + YOKE_SLOPE * np.abs(c[:, 0])


def build_top(body, mats):
    fam = body.family
    off = 0.007 * fam["torso"] + 0.005 * fam["arm"] + 0.008 * fam["hand"] + 0.008 * fam["leg"] + 0.008 * fam["head"]
    chest = pieces.fitted_torso(body, HEM_Z - 0.02, 1.38, pad=0.006, max_half=0.17)
    sleeves = pieces.arm_tubes(body, 0.20, CUFF_X + 0.01, pad0=0.003, pad1=0.004, sag=0.001)
    top = pieces.shell(body, "Top", off, extras=[chest] + sleeves, voxel=0.006, smooth=6, keep=body.co[:, 2] > 0.80)
    pieces.iron(body, top, torso_iters=90, limb_iters=16, z_range=(0.9, 1.38))
    cs.cut_and_delete(top, (0, 0, HEM_Z), (0, 0, 1), lambda c, n: c[:, 2] < HEM_Z)
    for sx in (1, -1):
        cs.cut_and_delete(top, (sx * CUFF_X, 0, 0), (1, 0, 0), lambda c, n, sx=sx: sx * c[:, 0] > CUFF_X)
    c, n = cs.face_data(top)
    cs.delete_faces(top, pieces.neck_hole(body, c, pad=0.034, z_from=1.40, z_cap=1.545))
    pieces.keep_largest(top)
    cs.displace(top, fold_field(body))
    cs.decimate(top, tris=4800)
    pieces.snap_neckline(body, top, pad=0.034, z_from=1.36)
    trim_top(top, mats)
    inner = cs.solidify(top, 0.003)
    cs.set_material(top, mats["lining"], inner)
    outer = cs.get_face_int(top, "shell") == 0
    cs.raise_faces(top, cs.material_mask(top, mats["panel"]) & outer, 0.0008)
    cs.raise_faces(top, cs.material_mask(top, mats["piping"]) & (cs.get_face_int(top, "shell") == 0), 0.0016)
    cs.raise_faces(top, cs.material_mask(top, mats["zip"]) & (cs.get_face_int(top, "shell") == 0), 0.0015)
    cs.box_uv(top, 0.035)
    cs.mark_sharp(top, 55)
    co = cs.get_co(top)
    W = cs.reassign(body, body.transfer(co), "leg", "pelvis")   # the hem hangs from the hips
    edges = cs.get_edges(top)
    W = cs.smooth_values(W, edges, 4, 0.5)
    armpit = np.zeros(len(co), bool)
    for side in ("l", "r"):
        armpit |= np.linalg.norm(co - body.bone_head("upperarm_" + side), axis=1) < 0.12
    W = cs.smooth_values(W, edges, 12, 0.5, mask=armpit)
    out = [(top, W)]
    # Zipped to the top: a hairline gap (the zip), no skin showing down the front.
    out.append(pieces.stand_collar(body, mats["top"], mats["lining"], z0=1.392, z1=1.552, front_drop=0.012,
                                   gap_deg=0.6, pad0=0.034, pad1=0.026, thickness=0.004, name="Collar"))
    return out


def trim_top(top, mats):
    """Black side panels (up the sides and along the underside of the sleeves), a black yoke whose edge is a chevron
    front and back, light purple piping along it and at the shoulder, black cuffs, a zip down the yoke. All on
    planes."""
    cy = pieces.TORSO_AXIS[1]
    body_faces = lambda c, n: np.abs(c[:, 0]) < 0.26   # noqa: E731
    for y0 in SIDE_Y:
        cs.bisect(top, (0, y0, 0), (0, 1, 0), lambda c, n: (np.abs(c[:, 0]) < 0.24) & (c[:, 2] < 1.36))
    cs.bisect(top, (0, 0, UNDER_Z), (0, 0, 1), lambda c, n: np.abs(c[:, 0]) > 0.16)
    for sx in (1, -1):
        for x0 in (YOKE_END_X, YOKE_END_X + PIPE, UNDER_END_X, CUFF_X - CUFF_W):
            cs.bisect(top, (sx * x0, 0, 0), (1, 0, 0), lambda c, n: np.abs(c[:, 0]) > 0.15)
        for back, vz in ((False, YOKE_V[0]), (True, YOKE_V[1])):
            normal = np.array([-sx * YOKE_SLOPE, 0.0, 1.0])
            normal /= np.linalg.norm(normal)
            for k in (0.0, -PIPE_V):
                point = np.array([0.0, 0.0, vz + k])
                cs.bisect(top, point, normal, lambda c, n, sx=sx, back=back: (sx * c[:, 0] >= -0.002)
                          & ((c[:, 1] > cy) == back) & body_faces(c, n) & (c[:, 2] > 1.15))
    for sx in (1, -1):
        cs.bisect(top, (sx * 0.004, 0, 0), (1, 0, 0), lambda c, n: (c[:, 1] < cy) & (np.abs(c[:, 0]) < 0.03)
                  & (c[:, 2] > YOKE_V[0] - 0.01))
    c, n = cs.face_data(top)
    ax = np.abs(c[:, 0])
    back = c[:, 1] > cy
    edge = yoke_edge(c, back)
    on_body = ax < YOKE_END_X
    yoke = on_body & (c[:, 2] > edge)
    pipe = on_body & (c[:, 2] > edge - PIPE_V) & (c[:, 2] <= edge)
    pipe |= (ax > YOKE_END_X) & (ax < YOKE_END_X + PIPE) & (c[:, 2] > yoke_edge(c * [0, 1, 1] + [YOKE_END_X, 0, 0],
                                                                               back) - PIPE_V)
    side = (c[:, 1] > SIDE_Y[0]) & (c[:, 1] < SIDE_Y[1]) & (ax < 0.24) & (c[:, 2] < 1.36) & (ax > 0.05)
    under = (ax > 0.17) & (ax < UNDER_END_X) & (c[:, 2] < UNDER_Z) & (c[:, 2] > 1.30)
    cuff = ax > CUFF_X - CUFF_W
    zipper = (ax < 0.004) & (c[:, 1] < cy) & (c[:, 2] > YOKE_V[0])
    cs.set_material(top, mats["top"])
    cs.set_material(top, mats["panel"], yoke | side | under | cuff)
    cs.set_material(top, mats["piping"], pipe & ~cuff)
    cs.set_material(top, mats["zip"], zipper)


# --------------------------------------------------------------------------------------------------
# trousers, belt, thigh strap
# --------------------------------------------------------------------------------------------------


def build_trousers(body, mats, boots_upper):
    fam = body.family
    off = 0.006 * (fam["leg"] + fam["torso"]) + 0.005 * fam["foot"]
    # Fitted: the legs' own shape, ironed (straight tubes from the thigh read as baggy pipes with a step at their top).
    tr = pieces.shell(body, "Trousers", off, voxel=0.007, smooth=6, keep=body.co[:, 2] < 1.10)
    pieces.iron(body, tr, torso_iters=30, limb_iters=24, z_range=(0.78, 1.05))
    cs.cut_and_delete(tr, (0, 0, TROUSER_TOP), (0, 0, 1), lambda c, n: c[:, 2] > TROUSER_TOP)
    cs.cut_and_delete(tr, (0, 0, 0.19), (0, 0, 1), lambda c, n: c[:, 2] < 0.19)
    pieces.keep_islands(tr, 300)
    axes = {s: pieces.leg_axis(body, s) for s in ("l", "r")}

    def folds(co, n):
        z = co[:, 2]
        ly = np.array([axes["l" if x > 0 else "r"](zz)[1] for x, zz in zip(co[:, 0], z)])
        knee_back = np.exp(-((z - 0.55) / 0.045) ** 2) * (co[:, 1] > ly)
        d = 0.0022 * knee_back * np.sin(z / 0.02 * 2 * math.pi + co[:, 0] * 20)
        crotch = np.exp(-((z - 0.84) / 0.04) ** 2) * (co[:, 1] < ly) * (np.abs(co[:, 0]) < 0.10)
        d += 0.0015 * crotch * np.sin((z + 0.6 * np.abs(co[:, 0])) / 0.03 * 2 * math.pi)
        return d

    cs.displace(tr, folds)
    pieces.tuck(tr, boots_upper, axes, BOOT_TOP, blouse=0.006, blouse_h=0.035)
    cs.taubin(tr, 2)
    cs.decimate(tr, tris=4200)
    for y0 in STRIPE_Y:
        cs.bisect(tr, (0, y0, 0), (0, 1, 0), lambda c, n: np.sign(n[:, 0]) * np.sign(c[:, 0]) > 0.3)
    c, n = cs.face_data(tr)
    cs.set_material(tr, mats["trousers"])
    stripe = (c[:, 1] > STRIPE_Y[0]) & (c[:, 1] < STRIPE_Y[1]) & (np.sign(n[:, 0]) * np.sign(c[:, 0]) > 0.3) \
        & (np.abs(c[:, 0]) > 0.09) & (c[:, 2] > BOOT_TOP)
    cs.set_material(tr, mats["piping"], stripe)
    inner = cs.solidify(tr, 0.003)
    cs.set_material(tr, mats["trousers"], inner)
    cs.raise_faces(tr, cs.material_mask(tr, mats["piping"]) & (cs.get_face_int(tr, "shell") == 0), 0.0009)
    cs.box_uv(tr, 0.035)
    cs.mark_sharp(tr, 60)
    co = cs.get_co(tr)
    edges = cs.get_edges(tr)
    W = cs.smooth_values(body.transfer(co), edges, 3, 0.5)
    return tr, bt.crotch_weights(body, co, edges, W)


def frame_on(p, n):
    """Columns (along, out, up) for a box sitting on a surface at p with normal n."""
    out = np.array([n[0], n[1], 0.0])
    out /= max(np.linalg.norm(out), 1e-9)
    up = np.array([0.0, 0.0, 1.0])
    along = np.cross(up, out)
    return np.column_stack([along, out, up])


def pouch(name, p, n, size, mats, lift=0.0):
    """A belt pouch: a leather box standing off the surface, a flap over its top front, a snap."""
    rot = frame_on(p, n)
    w, d, h = size
    centre = p + rot[:, 1] * (d / 2 + 0.006) + np.array([0, 0, lift])
    box = cs.box(name, centre, (w, d, h), rotation=rot)
    flap = cs.box(name + "Flap", centre + rot[:, 1] * 0.002 + np.array([0, 0, h * 0.22]), (w + 0.004, d + 0.002,
                                                                                         h * 0.6), rotation=rot)
    snap = cs.box(name + "Snap", centre + rot[:, 1] * (d / 2 + 0.003) + np.array([0, 0, -h * 0.02]),
                  (0.012, 0.004, 0.012), rotation=rot)
    cs.set_material(box, mats["quiver"])
    cs.set_material(flap, mats["belt"])
    cs.set_material(snap, mats["metal"])
    return cs.join([box, flap, snap], name)


def build_belt(body, surf, tr_surf, mats):
    out = pieces.belt(body, surf, mats, z=WAIST_Z, height=0.036, name="Belt")
    pouches = []
    cy = pieces.TORSO_AXIS[1]
    for k, ang in enumerate((-55.0, 55.0, 112.0, -112.0)):
        a = math.radians(ang)
        p, n = surf.project_from_axis((0.0, cy, WAIST_Z), (math.sin(a), -math.cos(a), 0.0))
        if p is None:
            continue
        size = (0.075, 0.032, 0.058) if k < 2 else (0.06, 0.028, 0.05)
        pouches.append(pouch("Pouch%d" % k, p, n, size, mats, lift=-0.012))
    if pouches:
        obj = cs.join(pouches, "Pouches")
        out.append((obj, cs.rigid(body, len(obj.data.vertices), {"pelvis": 1.0})))
    # A strap round the right thigh with a pouch on the outside.
    x, y = pieces.leg_axis(body, "r")(THIGH_STRAP_Z)
    P, N = cs.band_around(tr_surf, (x, y), [THIGH_STRAP_Z - 0.013, THIGH_STRAP_Z + 0.013], segments=40,
                          reach=pieces.LEG_REACH)
    strap = cs.slab("ThighStrap", P.transpose(1, 0, 2), N.transpose(1, 0, 2), 0.003, gap=0.001, closed=True)
    cs.set_material(strap, mats["belt"])
    out.append((strap, body.transfer(cs.get_co(strap))))
    p, n = tr_surf.project_from_axis((x, y, THIGH_STRAP_Z - 0.01), (-1.0, 0.0, 0.0), reach=pieces.LEG_REACH)
    if p is not None:
        tp = pouch("ThighPouch", p, n, (0.085, 0.03, 0.10), mats, lift=-0.025)
        out.append((tp, cs.rigid(body, len(tp.data.vertices), {"thigh_r": 1.0})))
    return out


# --------------------------------------------------------------------------------------------------
# hands
# --------------------------------------------------------------------------------------------------


def dominant(body):
    return np.array(body.bones)[body.W.argmax(axis=1)]


def glove_masks(body):
    """Body-vertex masks: the bow hand's fingerless glove (hand and the first finger joints, the thumb's base) and
    the draw hand's shooting glove (the hand, index, middle and ring fingers whole; thumb and little finger bare)."""
    dom = dominant(body)
    left = np.isin(dom, ["hand_l", "index_01_l", "middle_01_l", "ring_01_l", "pinky_01_l", "thumb_01_l",
                         "lowerarm_l"])
    right_bones = ["hand_r", "lowerarm_r"] + ["%s_%s_r" % (f, j) for f in ("index", "middle", "ring")
                                              for j in ("01", "02", "03", "04_leaf")]
    right = np.isin(dom, right_bones)
    return left & (body.co[:, 0] > 0), right & (body.co[:, 0] < 0)


def bare_hand_skin(body, gloved):
    """The hands' skin a glove leaves: everything not gloved, plus the ring of gloved verts on its edge (so the faces
    across the glove's edge stay skin, under the glove's rim, and no gap opens)."""
    hand = np.abs(body.co[:, 0]) > GLOVE_X
    e = np.array(body.surface.tris)
    edges = np.concatenate([e[:, [0, 1]], e[:, [1, 2]], e[:, [2, 0]]])
    bare = ~gloved
    edge_ring = np.zeros(len(gloved), bool)
    for a, b in ((0, 1), (1, 0)):
        sel = gloved[edges[:, a]] & bare[edges[:, b]]
        edge_ring[edges[sel, a]] = True
    return hand & (bare | edge_ring)


# --------------------------------------------------------------------------------------------------
# hair
# --------------------------------------------------------------------------------------------------


def head_centre():
    return np.array([0.0, 0.012, 1.655])


def tie_point(scalp_surf):
    c = head_centre()
    d = np.array([0.0, 0.80, 0.60])   # the back of the crown: high, not on top
    p, n = scalp_surf.project_from_axis(c, d, reach=0.3)
    return p, n


def build_scalp(body, guide):
    """Hair pulled back: the scalp (where the pack's buzz cut grows, its ragged front smoothed) pushed out 6 mm, thinning
    to nothing at the hairline, with strand grooves running from the hairline to the tie at the crown. UVs: u round
    the tie, v from the hairline (0) to the tie (0.8)."""
    # The head's own skin pushed out (a voxel shell of the head had surfaces inside the skull from the eyes and mouth,
    # and lost the crown to them), subdivided twice for the grooves.
    scalp = body.offset_copy("Scalp", 0.0065)
    # The hairline: per direction round a crown axis, how far down the guide reaches (its lowest edge), smoothed round
    # the head so the buzz cut's ragged front becomes a clean line. (Keeping faces near the guide's surface instead
    # left holes over the crown, where the guide has gaps.)
    gco = cs.get_co(guide)
    gphi, galpha = crown_coords(gco)
    bins = 72
    gb = ((gphi + math.pi) / (2 * math.pi) * bins).astype(int) % bins
    line = np.zeros(bins)
    np.maximum.at(line, gb, galpha)
    for _ in range(4):
        line = np.maximum(line, 0.0)
        line = 0.25 * np.roll(line, 1) + 0.5 * line + 0.25 * np.roll(line, -1)
    # The buzz cut's front sits back off the forehead; a woman's hairline comes further down at the front.
    centres = (np.arange(bins) + 0.5) / bins * 2 * math.pi - math.pi
    line = line + 0.16 * np.maximum(0.0, -np.sin(centres)) ** 2
    phi, alpha = crown_coords(body.co)
    vb = ((phi + math.pi) / (2 * math.pi) * bins).astype(int) % bins
    inside = (alpha < line[vb] - 0.01) & (body.family["head"] > 0.3) & (body.co[:, 2] > 1.55)
    cs.log("scalp", int(inside.sum()), "of", len(inside), "head verts under the hair")
    cs.delete_verts(scalp, ~inside)
    with cs.Edit(scalp) as bm:   # the glTF's verts are split at UV seams: one surface again
        bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    pieces.keep_largest(scalp)
    with cs.Edit(scalp) as bm:
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True, smooth=1.0)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
    with cs.Edit(scalp) as bm:
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True, smooth=1.0)
    scalp.vertex_groups.clear()
    cs.taubin(scalp, 3)
    # Smooth the hairline: boundary verts along the boundary, then back onto the scalp offset.
    co = cs.get_co(scalp)
    with cs.Edit(scalp) as bm:
        bverts = np.array([v.is_boundary for v in bm.verts])
        bedges = np.array([(e.verts[0].index, e.verts[1].index) for e in bm.edges if e.is_boundary])
    if len(bedges):
        co = cs.smooth_values(co, bedges, 25, 0.5, mask=bverts)
        for i in np.nonzero(bverts)[0]:
            p, n = body.surface.nearest(co[i])
            co[i] = p + n * 0.0012
    # Thin toward the hairline.
    b = co[bverts]
    dist = np.array([np.min(np.linalg.norm(b - p, axis=1)) for p in co]) if len(b) else np.ones(len(co))
    # In along the shell's own normals (snapping every vertex to the nearest skin found surfaces inside the head and
    # sank the hair under the crown).
    cs.set_co(scalp, co)
    nn = cs.get_vnormals(scalp)
    thick = 0.0012 + (0.0065 - 0.0012) * cs.smoothstep(0.0, 0.02, dist)
    sink = np.where(bverts, 0.0, 0.0065 - thick)
    cs.set_co(scalp, co - nn * sink[:, None])
    cs.taubin(scalp, 2)
    return scalp


def crown_coords(co):
    """(angle round, angle down from) a crown axis through the head's centre, tipped back a little: the guide's
    hairline is a function of the first."""
    c = head_centre()
    t = np.array([0.0, 0.3, 0.954])
    a = np.array([1.0, 0.0, 0.0])
    b = np.cross(t, a)
    v = co - c
    phi = np.arctan2(v @ b, v @ a)
    alpha = np.arccos(np.clip((v / np.linalg.norm(v, axis=1)[:, None]) @ t, -1, 1))
    return phi, alpha


def strand_coords(co, tie):
    """(u, angle from the tie) of points on the head: u is the angle round the axis from the head's centre to the
    tie, so lines of constant u run from the hairline to the tie."""
    c = head_centre()
    t = tie - c
    t /= np.linalg.norm(t)
    a = np.cross(t, (1.0, 0.0, 0.0))
    a /= np.linalg.norm(a)
    b = np.cross(t, a)
    v = co - c
    phi = np.arctan2(v @ b, v @ a)
    alpha = np.arccos(np.clip((v / np.linalg.norm(v, axis=1)[:, None]) @ t, -1, 1))
    return phi, alpha


def finish_scalp(scalp, tie, mats):
    co = cs.get_co(scalp)
    phi, alpha = strand_coords(co, tie)
    bins = 64
    bi = ((phi + math.pi) / (2 * math.pi) * bins).astype(int) % bins
    edge = np.zeros(bins)
    np.maximum.at(edge, bi, alpha)
    edge = np.maximum(edge, 0.98 * np.maximum(np.roll(edge, 1), np.roll(edge, -1)))
    v = 0.8 * (1.0 - alpha / np.maximum(edge[bi], 1e-3))
    n = cs.get_vnormals(scalp)
    groove = 0.0011 * (np.abs(np.sin(phi * 34)) ** 0.6 - 0.6) * cs.smoothstep(0.0, 0.25, v)
    cs.set_co(scalp, co + n * groove[:, None])
    cs.decimate(scalp, tris=3500)
    co = cs.get_co(scalp)
    phi, alpha = strand_coords(co, tie)
    bi = ((phi + math.pi) / (2 * math.pi) * bins).astype(int) % bins
    v = np.clip(0.8 * (1.0 - alpha / np.maximum(edge[bi], 1e-3)), 0.0, 0.8)
    u = (phi + math.pi) / (2 * math.pi) * 6.0
    set_vertex_uvs(scalp, u, v, wrap=6.0)
    cs.set_material(scalp, mats["hair"])
    cs.mark_sharp(scalp, 70)


def set_vertex_uvs(obj, u, v, wrap=None):
    """UVs from per-vertex u, v; a face whose u spans more than half of `wrap` has its small u's moved up by wrap
    (the seam where u comes round)."""
    me = obj.data
    cs.keep_first_uv(obj)
    lv = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", lv)
    lu = u[lv].copy()
    if wrap:
        starts = np.empty(len(me.polygons), np.int64)
        totals = np.empty(len(me.polygons), np.int64)
        me.polygons.foreach_get("loop_start", starts)
        me.polygons.foreach_get("loop_total", totals)
        for s, t in zip(starts, totals):
            seg = lu[s:s + t]
            if seg.max() - seg.min() > wrap / 2:
                seg[seg < wrap / 2] += wrap
                lu[s:s + t] = seg
    uv = np.column_stack([lu, v[lv]])
    me.uv_layers[0].data.foreach_set("uv", uv.astype(np.float32).ravel())


def ponytail_path(tie, tie_n, clear):
    """The ponytail's axis: out of the tie, up and back a little, then falling down the back of the head and neck to
    between the shoulder blades, kept off the neck and collar."""
    back = np.array([0.0, 1.0, 0.0])
    pts = [tie - tie_n * 0.004, tie + tie_n * 0.022 + np.array([0, 0.004, 0.010]),
           tie + np.array([0, 0.050, -0.008]), tie + np.array([0, 0.066, -0.06]),
           tie + np.array([0, 0.066, -0.14]), tie + np.array([0, 0.058, -0.23]), tie + np.array([0, 0.052, -0.32])]
    pts = np.array(pts)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    t = np.concatenate([[0], np.cumsum(seg)])
    s = np.linspace(0, t[-1], 30)
    path = np.stack([np.interp(s, t, pts[:, i]) for i in range(3)], axis=1)
    path = cs.smooth_values(path, np.array([(i, i + 1) for i in range(len(path) - 1)]), 8, 0.5,
                            mask=np.r_[False, False, np.ones(len(path) - 3, bool), False])
    # Off the body: at least the bundle's radius plus a gap behind the neck and back.
    for i in range(4, len(path)):
        for surf in clear:
            hit, _n = surf.project_from_axis((0.0, 0.0, path[i][2]), back, reach=0.4)
            if hit is not None:
                path[i][1] = max(path[i][1], hit[1] + 0.036)
    return path, s / s[-1]


def build_ponytail(body, tie, tie_n, mats, clear):
    """A high ponytail of separate locks: a bundle that swells below the tie and tapers to a ragged tip, each lock a
    tapered tube with its own wave and length, a fuller core inside, a hair tie. UV v runs root to tip along each
    lock, so the strand texture's ragged alpha breaks the tips."""
    path, t = ponytail_path(tie, tie_n, clear)
    T, Nf, Bf = cs.frames_along(path)
    rng = np.random.default_rng(21)
    bundle = 0.013 + 0.026 * np.sin(np.pi * np.clip(t * 1.8, 0, 1) * 0.5) ** 0.7 * (1 - 0.55 * t)
    locks = []
    count = 26
    for k in range(count):
        ang = 2 * math.pi * k / count + rng.uniform(-0.2, 0.2)
        rho = rng.uniform(0.3, 1.0)
        end = rng.uniform(0.80, 1.0)
        phase = rng.uniform(0, 2 * math.pi)
        n_ring = 16
        tt = np.linspace(0, end, n_ring)
        centre = np.stack([np.interp(tt, t, path[:, i]) for i in range(3)], axis=1)
        nf = np.stack([np.interp(tt, t, Nf[:, i]) for i in range(3)], axis=1)
        bf = np.stack([np.interp(tt, t, Bf[:, i]) for i in range(3)], axis=1)
        R = np.interp(tt, t, bundle) * rho * (1 + 0.25 * tt)   # the tip fans a little
        wave = 0.004 * np.sin(tt * 9 + phase) * tt
        a2 = ang + 0.3 * np.sin(tt * 4 + phase) * tt
        pts = centre + (R + wave)[:, None] * (np.cos(a2)[:, None] * nf + np.sin(a2)[:, None] * bf)
        r = 0.0085 * (1 - 0.8 * (tt / end) ** 1.5) * rng.uniform(0.8, 1.15)
        lock = cs.tube("Lock%d" % k, pts, r, segments=6, cap=True, squash=0.65)
        lock_uvs(lock, n_ring, 6, k, tt / end)
        locks.append(lock)
    core_t = np.linspace(0.0, 0.78, 20)
    core_c = np.stack([np.interp(core_t, t, path[:, i]) for i in range(3)], axis=1)
    core_r = np.interp(core_t, t, bundle) * 0.75 + 0.002
    core = cs.tube("LockCore", core_c, core_r, segments=10, cap=True)
    lock_uvs(core, 20, 10, 99, core_t / 0.78 * 0.9)
    locks.append(core)
    tail = cs.join(locks, "Ponytail")
    cs.set_material(tail, mats["hair"])
    cs.mark_sharp(tail, 80)
    # Weights: the head carries it; toward the tip the neck shares a little, so a nod does not swing it into the back.
    co = cs.get_co(tail)
    s = np.clip((tie[2] - co[:, 2]) / 0.33, 0, 1)
    W = cs.rigid(body, len(co), {"Head": 1.0})
    W[:, body.bone_index["Head"]] = 1.0 - 0.3 * s
    W[:, body.bone_index["neck_01"]] = 0.3 * s
    # The hair tie: a band round the bundle just below the root.
    i = 2
    ring = [path[i] + 0.0155 * (math.cos(a) * Nf[i] + math.sin(a) * Bf[i]) for a in np.linspace(0, 2 * math.pi, 17)]
    tie_obj = cs.tube("HairTie", ring, 0.0045, segments=8, cap=True)
    cs.set_material(tie_obj, mats["tie"])
    return [(tail, W), (tie_obj, cs.rigid(body, len(tie_obj.data.vertices), {"Head": 1.0}))]


def lock_uvs(obj, rings, segs, k, v_of_ring):
    """u round the tube (a lock's own strip of the texture), v along it; the two cap centres take the end v's."""
    n = len(obj.data.vertices)
    idx = np.arange(n)
    ring = np.minimum(idx // segs, rings - 1)
    j = idx % segs
    u = (j / segs) * 0.35 + (k * 0.137) % 1.0
    v = np.asarray(v_of_ring)[ring] * 0.98
    if n > rings * segs:            # cap centres: start, end
        u[rings * segs:] = (k * 0.137) % 1.0
        v[rings * segs] = v_of_ring[0]
        v[rings * segs + 1:] = v_of_ring[-1] * 0.98
    set_vertex_uvs(obj, u, v, wrap=None)


# --------------------------------------------------------------------------------------------------
# the build
# --------------------------------------------------------------------------------------------------


def build(args):
    body = cs.Body(args.assets_root, gltf=cs.BODY_FEMALE_GLTF)
    body.neck_z = NECK_Z
    athletic(body)
    mats = make_materials(args.out)
    head = head_materials(body, args.out)
    out = []

    top_parts = build_top(body, mats)
    out += top_parts
    top_surf = cs.Surface(top_parts[0][0])
    boots = pieces.combat_boots(body, mats, top_z=BOOT_TOP, heel_drop=0.003)
    trousers = build_trousers(body, mats, boots[0][0])
    out.append(trousers)
    out += boots
    out += build_belt(body, top_surf, cs.Surface(trousers[0]), mats)
    left, right = glove_masks(body)
    out.append(pieces.gloves(body, mats["glove"], from_x=GLOVE_X, keep=left, name="GloveBow", knuckle_x=0.715))
    out.append(pieces.gloves(body, mats["glove"], from_x=GLOVE_X, keep=right, name="GloveDraw", knuckle_x=0.715))
    out += pieces.forearm_guard(body, top_surf, mats, x0=0.436, x1=0.566, strap_xs=(0.46, 0.542), wrap_deg=125.0)
    strap = [(-p[0] * 0.9, p[1] * 0.9, p[2] * 0.972) for p in pieces.CLINT_STRAP]
    arrows = [(-0.5, -0.4), (0.0, -0.45), (0.5, -0.4), (-0.45, 0.1), (0.0, 0.05), (0.45, 0.1), (-0.2, 0.5),
              (0.25, 0.45)]
    out += pieces.quiver(body, top_surf, mats, bottom=(0.085, 0.0, 0.985), top=(-0.118, 0.0, 1.415), r0=0.040,
                         r1=0.053, arrows=arrows, strap=strap, strap_half=0.016, axis_top=1.36, rise=0.13,
                         vane=(0.065, 0.012, 0.048), name="Quiver")

    # Hair: the scalp, then the ponytail from the tie.
    guide = body.add_hair("Hair_BuzzedFemale.gltf", head["hair"])
    scalp = build_scalp(body, guide)
    cs.remove(guide)
    tie, tie_n = tie_point(cs.Surface(scalp))
    finish_scalp(scalp, tie, head)
    out.append((scalp, cs.rigid(body, len(scalp.data.vertices), {"Head": 1.0})))
    out += build_ponytail(body, tie, tie_n, head, [body.surface, cs.Surface(top_parts[1][0])])

    # Skin where nothing covers it: the head and neck inside the collar, the bare fingers.
    bare = bare_hand_skin(body, left | right)
    body.keep_skin(pieces.skin_above_collar(body, z_from=1.40, pad=0.03) | bare)
    cs.set_material(body.skin, head["skin"])
    body.eyes.data.materials.clear()
    body.eyes.data.materials.append(head["eyes"])
    body.brows.data.materials.clear()
    body.brows.data.materials.append(head["brows"])
    parts = [body.skin, body.eyes, body.brows] + skin_all(body, out)
    cs.finish(body, parts, WHO, args, extra_renders=lambda b, mesh: render_scale(b, mesh, args))


SMOOTH = ("Scalp", "Ponytail", "HairTie")   # shaded as built (six-sided locks would facet at 45 degrees)


def skin_all(body, pieces_out):
    for obj, W in pieces_out:
        if len(obj.data.uv_layers) == 0:
            cs.box_uv(obj, 0.04)
        if "shell" not in obj.data.attributes and obj.name not in SMOOTH:
            cs.mark_sharp(obj, 45)
        cs.skin(obj, body, W)
    return [p for p, _w in pieces_out]


def render_scale(body, mesh, args):
    """kate_clint_scale.png: Kate beside Clint, both in the idle pose, as the game draws them (her look takes her
    driver's GAME_SCALE; Clint's is 1), front on, orthographic, from <assets root>/built/clint/clint.blend."""
    import bpy  # noqa: PLC0415

    path = os.path.join(args.assets_root, "built", "clint", "clint.blend")
    if not os.path.isfile(path):
        cs.log("scale render skipped: no", path)
        return
    with bpy.data.libraries.load(path) as (src, dst):
        dst.objects = [n for n in src.objects if n in ("Armature", "SK_Clint")]
    clint_arm = clint_mesh = None
    for o in dst.objects:
        if o is None:
            continue
        cs.link(o)
        if o.type == "ARMATURE":
            clint_arm = o
        else:
            clint_mesh = o
    if clint_arm is None or clint_mesh is None:
        cs.log("scale render skipped: Clint's armature or mesh missing in", path)
        return
    kate_arm = body.armature
    kate_arm.location = (-0.42, 0.0, 0.0)
    kate_arm.scale = (GAME_SCALE,) * 3
    clint_arm.location = (0.42, 0.0, 0.0)
    for arm in (kate_arm, clint_arm):
        cs.apply_pose(arm, "idle")
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    body.studio.shoot(os.path.join(args.renders, "kate_clint_scale.png"), 0.0, target=(0.0, 0.0, 0.95), scale=3.6)
    sc.render.resolution_x, sc.render.resolution_y = 1000, 1000
    for arm in (kate_arm, clint_arm):
        cs.clear_pose(arm)
        arm.data.pose_position = "REST"
    kate_arm.location = (0.0, 0.0, 0.0)
    kate_arm.scale = (1.0, 1.0, 1.0)
    cs.remove(clint_mesh)
    bpy.data.objects.remove(clint_arm, do_unlink=True)


if __name__ == "__main__":
    build(cs.parse_args(WHO))
