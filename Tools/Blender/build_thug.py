"""The Tracksuit Mafia street thug (claude-docs/blender-pipeline.md, "Thug").

    blender.exe --background --factory-startup --python Tools/Blender/build_thug.py -- [--no-render] [--views a,b]

A loose dark red tracksuit: a zip jacket that hangs straight from the chest and gathers into a ribbed hem, ribbed
cuffs, a stand collar, a zip with a pull, two white stripes from the collar down each sleeve; baggy trousers with two
white stripes down the outside of each leg and gathered ankles; white trainers with a thick sole, laces and a heel
tab; buzzed black hair. Writes <assets root>/built/thug/SK_Thug.glb (+ .blend, slots json) and the renders.
build_heavy.py builds on build_tracksuit() here.
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import costume as cs  # noqa: E402
import pieces  # noqa: E402

WHO = "thug"
HEM_Z = 0.90          # the jacket's hem, at the hips
RIB_H = 0.045         # ribbed hem band height
CUFF_X = 0.69         # sleeves end at the wrist
CUFF_W = 0.04         # ribbed cuff length
ZIP_X = 0.0045
STRIPES_ARM = ((0.051, 0.061), (0.077, 0.087))    # y bands along the top of each sleeve (arm axis at y 0.069)
STRIPES_LEG = ((0.026, 0.036), (0.050, 0.060))    # y bands down the outside of each leg
STRIPE_FROM_X = 0.10                               # sleeve stripes start at the collar
ANKLE_Z = 0.092
TROUSER_TOP = 1.0

RED = (0.065, 0.0045, 0.006)
# Decimation budgets (triangles before thickness, which roughly doubles them): enemies are many on screen.
JACKET_TRIS = 5000
TROUSER_TRIS = 4000


def make_materials(out, who="Thug", red=RED):
    tricot = cs.weave_normal("tricot", out, strength=1.2)
    rib = cs.weave_normal("rib", out, strength=3.0)
    grain = cs.weave_normal("grain", out, strength=1.0)
    knit = cs.weave_normal("knit", out)
    m = cs.material
    p = "M_" + who + "_"
    return {
        "suit": m(p + "Tracksuit", red, 0.62, normal=tricot, normal_strength=0.6),
        "rib": m(p + "Rib", tuple(c * 0.85 for c in red), 0.7, normal=rib, normal_strength=1.0),
        "stripe": m(p + "Stripe", (0.72, 0.72, 0.70), 0.5, normal=tricot, normal_strength=0.4),
        "zip": m(p + "Zip", (0.25, 0.25, 0.26), 0.35, metallic=1.0),
        "trainer": m(p + "Trainer", (0.80, 0.80, 0.78), 0.45, normal=grain, normal_strength=0.25),
        "trainer_accent": m(p + "TrainerAccent", (0.05, 0.05, 0.055), 0.5, normal=grain, normal_strength=0.4),
        "trainer_sole": m(p + "TrainerSole", (0.85, 0.85, 0.83), 0.7),
        "trainer_midsole": m(p + "TrainerMidsole", (0.30, 0.30, 0.31), 0.8),
        "trainer_lace": m(p + "TrainerLace", (0.78, 0.78, 0.76), 0.8, normal=knit, normal_strength=0.4),
        # Boots (the heavy's): black leather.
        "boot": m(p + "Boot", (0.014, 0.013, 0.012), 0.38, normal=grain, normal_strength=0.5),
        "boot_panel": m(p + "BootPanel", (0.010, 0.010, 0.010), 0.3, normal=grain, normal_strength=0.6),
        "sole": m(p + "Sole", (0.008, 0.008, 0.008), 0.9),
        "lace": m(p + "Lace", (0.012, 0.012, 0.012), 0.8, normal=knit, normal_strength=0.4),
        "metal": m(p + "Metal", (0.35, 0.35, 0.37), 0.35, metallic=1.0),
        "belt": m(p + "Strap", (0.012, 0.012, 0.012), 0.5, normal=grain, normal_strength=0.5),
    }


def head_materials(body, out, who, hair_tint):
    import bpy  # noqa: PLC0415

    root = body.root
    skin_img = bpy.data.images.load(cs.pack_file(root, cs.TEXTURES + ("T_Superhero_Male_Dark.png",)),
                                    check_existing=True)
    skin_n = bpy.data.images.load(cs.pack_file(root, cs.GL_NORMALS + ("T_Superhero_Male_Normal.png",)),
                                  check_existing=True)
    skin_n.colorspace_settings.name = "Non-Color"
    eye_img = bpy.data.images.load(cs.pack_file(root, cs.TEXTURES + ("T_Eye_Brown.png",)), check_existing=True)
    hair_img = cs.tinted_image(cs.pack_file(root, cs.HAIR_TEXTURES + ("T_Hair_1_BaseColor.png",)),
                               "T_" + who + "_Hair", hair_tint, os.path.join(out, "T_Hair.png"), keep_detail=0.8)
    m = cs.material
    return {
        "skin": m("M_" + who + "_Skin", (0.8, 0.6, 0.5), 0.55, normal=skin_n, base_image=skin_img, glow=True),
        "eyes": m("M_" + who + "_Eyes", (1, 1, 1), 0.2, base_image=eye_img, glow=True),
        "hair": m("M_" + who + "_Hair", hair_tint, 0.7, base_image=hair_img, alpha_clip=True, glow=True),
    }


def build_jacket(body, mats):
    fam = body.family
    off = 0.024 * fam["torso"] + 0.016 * fam["arm"] + 0.016 * fam["hand"] + 0.016 * fam["leg"] + 0.012 * fam["head"]
    # Sleeves sag underneath (loose polyester hanging off a T-posed arm).
    off = off + 0.008 * np.clip(-body.normals[:, 2], 0, 1) * cs.smoothstep(0.22, 0.4, np.abs(body.co[:, 0])) \
        * (fam["arm"] > 0.5)
    hang = pieces.torso_solid(body, HEM_Z - 0.04, 1.42, pad=0.03, hem_in=0.03)
    sleeves = pieces.arm_tubes(body, 0.26, CUFF_X - CUFF_W - 0.01, pad0=0.022, pad1=0.03, sag=0.012)
    jacket = pieces.shell(body, "Jacket", off, extras=[hang] + sleeves, voxel=0.009, smooth=6,
                          keep=body.co[:, 2] > 0.75)
    pieces.iron(body, jacket, torso_iters=60, limb_iters=20, z_range=(0.9, 1.52))
    cs.cut_and_delete(jacket, (0, 0, HEM_Z), (0, 0, 1), lambda c, n: c[:, 2] < HEM_Z)
    for sx in (1, -1):
        cs.cut_and_delete(jacket, (sx * CUFF_X, 0, 0), (1, 0, 0), lambda c, n, sx=sx: sx * c[:, 0] > CUFF_X)
    c, n = cs.face_data(jacket)
    cs.delete_faces(jacket, pieces.neck_hole(body, c))
    pieces.keep_largest(jacket)
    ay = pieces.arm_axis(body, "l")
    cy = pieces.TORSO_AXIS[1]

    def shape(co, n):
        """Ribbed hem and cuffs gathered in, the body of the jacket bagging over them, folds at the elbows."""
        x, y, z = co[:, 0], co[:, 1], co[:, 2]
        ax = np.abs(x)
        d = np.zeros(len(co))
        hem = cs.smoothstep(HEM_Z + RIB_H + 0.01, HEM_Z + RIB_H - 0.005, z) * (ax < 0.3)
        theta = np.arctan2(y - cy, x)
        d += hem * (-0.014 + 0.0018 * np.abs(np.sin(theta * 70)))
        blouse = np.exp(-((z - (HEM_Z + RIB_H + 0.03)) / 0.025) ** 2) * (ax < 0.3)
        d += 0.006 * blouse * (0.6 + 0.4 * cs.fold_noise(theta * 9, 5))
        cuff = cs.smoothstep(CUFF_X - CUFF_W - 0.006, CUFF_X - CUFF_W + 0.004, ax)
        ang = np.array([math.atan2(zz - ay(xx)[1], yy - ay(xx)[0]) for xx, yy, zz in zip(ax, y, z)])
        d += cuff * (-0.008 + 0.0015 * np.abs(np.sin(ang * 40)))
        wrist = np.exp(-((ax - (CUFF_X - CUFF_W - 0.03)) / 0.025) ** 2)
        d += 0.005 * wrist * np.sin(ax / 0.02 * 2 * math.pi + cs.fold_noise(ang, 2))
        elbow = np.exp(-((ax - 0.47) / 0.05) ** 2) * (n[:, 1] < -0.1) * (ax > 0.3)
        d += 0.004 * elbow * np.sin((ax + 0.5 * (z - 1.456)) / 0.022 * 2 * math.pi)
        side = cs.smoothstep(1.3, 1.1, z) * (np.abs(n[:, 0]) > 0.5) * (ax < 0.3)
        d += 0.0015 * side * np.sin((z + 0.3 * y) / 0.05 * 2 * math.pi)
        return d

    cs.displace(jacket, shape)
    cs.decimate(jacket, tris=JACKET_TRIS)
    pieces.snap_neckline(body, jacket)
    # Trim: the zip, the ribbed hem and cuffs, the sleeve stripes, all on clean planes.
    for sx in (1, -1):
        cs.bisect(jacket, (sx * ZIP_X, 0, 0), (1, 0, 0), lambda c, n: (c[:, 1] < cy) & (np.abs(c[:, 0]) < 0.05))
        for x0 in (CUFF_X - CUFF_W, STRIPE_FROM_X):
            cs.bisect(jacket, (sx * x0, 0, 0), (1, 0, 0), lambda c, n: np.abs(c[:, 0]) > 0.05)
    cs.bisect(jacket, (0, 0, HEM_Z + RIB_H), (0, 0, 1), lambda c, n: np.abs(c[:, 0]) < 0.3)
    for band in STRIPES_ARM:
        for y0 in band:
            cs.bisect(jacket, (0, y0, 0), (0, 1, 0), lambda c, n: (np.abs(c[:, 0]) > STRIPE_FROM_X - 0.01)
                      & (n[:, 2] > 0.2))
    c, n = cs.face_data(jacket)
    ax = np.abs(c[:, 0])
    cs.set_material(jacket, mats["suit"])
    rib = (c[:, 2] < HEM_Z + RIB_H) & (ax < 0.3) | (ax > CUFF_X - CUFF_W)
    cs.set_material(jacket, mats["rib"], rib)
    stripe = np.zeros(len(c), bool)
    for lo, hi in STRIPES_ARM:
        stripe |= (c[:, 1] > lo) & (c[:, 1] < hi)
    stripe &= (ax > STRIPE_FROM_X) & (ax < CUFF_X - CUFF_W) & (n[:, 2] > 0.2) & (c[:, 2] > 1.4)
    cs.set_material(jacket, mats["stripe"], stripe)
    zipper = (ax < ZIP_X) & (c[:, 1] < cy)
    cs.set_material(jacket, mats["zip"], zipper)
    inner = cs.solidify(jacket, 0.004)
    cs.set_material(jacket, mats["suit"], inner)
    outer = cs.get_face_int(jacket, "shell") == 0
    cs.raise_faces(jacket, cs.material_mask(jacket, mats["stripe"]) & outer, 0.0008)
    cs.raise_faces(jacket, cs.material_mask(jacket, mats["zip"]) & (cs.get_face_int(jacket, "shell") == 0), 0.002)
    cs.box_uv(jacket, 0.03)
    cs.mark_sharp(jacket, 55)
    co = cs.get_co(jacket)
    W = cs.reassign(body, body.transfer(co), "leg", "pelvis")   # the hem hangs from the hips, not the thighs
    edges = cs.get_edges(jacket)
    W = cs.smooth_values(W, edges, 4, 0.5)
    armpit = np.zeros(len(co), bool)
    for side in ("l", "r"):
        armpit |= np.linalg.norm(co - body.bone_head("upperarm_" + side), axis=1) < 0.15
    W = cs.smooth_values(W, edges, 12, 0.5, mask=armpit)
    out = [(jacket, W)]
    # The zip pull, hanging from the slider at the chest.
    surf = cs.Surface(jacket)
    hit, nrm = surf.project_from_axis((0, cy, 1.38), (0, -1, 0))
    if hit is not None:
        pull = cs.box("ZipPull", hit + nrm * 0.004 - np.array([0, 0, 0.012]), (0.008, 0.003, 0.024))
        cs.set_material(pull, mats["zip"])
        out.append((pull, body.transfer(cs.get_co(pull))))
    collar = pieces.stand_collar(body, mats["suit"], mats["suit"], z1=1.58, front_drop=0.04, gap_deg=6.0,
                                 pad0=0.05, pad1=0.05, thickness=0.005, name="TrackCollar")
    out.append(collar)
    return out


def build_trousers(body, mats, boots=None, boot_top=0.0):
    """Baggy tracksuit trousers: hang from the thigh, ribbed gathered ankles (or tucked into boots), stripes."""
    fam = body.family
    off = 0.016 * (fam["leg"] + fam["torso"]) + 0.01 * fam["foot"]
    bottom = boot_top - 0.13 if boots is not None else ANKLE_Z
    tubes = pieces.leg_tubes(body, 0.76, bottom - 0.03, pad_top=0.018, pad_bottom=0.03)
    tr = pieces.shell(body, "TrackTrousers", off, extras=tubes, voxel=0.009, smooth=6, keep=body.co[:, 2] < 1.12)
    pieces.iron(body, tr, torso_iters=10, limb_iters=24, z_range=(0.8, 1.1))
    cs.cut_and_delete(tr, (0, 0, TROUSER_TOP), (0, 0, 1), lambda c, n: c[:, 2] > TROUSER_TOP)
    cs.cut_and_delete(tr, (0, 0, bottom), (0, 0, 1), lambda c, n: c[:, 2] < bottom)
    pieces.keep_islands(tr, 300)
    axes = {s: pieces.leg_axis(body, s) for s in ("l", "r")}

    def shape(co, n):
        z = co[:, 2]
        side = np.where(co[:, 0] > 0, 1, -1)
        lxy = np.array([axes["l" if x > 0 else "r"](zz) for x, zz in zip(co[:, 0], z)])
        ang = np.arctan2(co[:, 1] - lxy[:, 1], (co[:, 0] - lxy[:, 0]) * side)
        d = np.zeros(len(co))
        if boots is None:
            cuff = cs.smoothstep(ANKLE_Z + 0.045, ANKLE_Z + 0.03, z)
            d += cuff * (-0.022 + 0.0012 * np.abs(np.sin(ang * 30)))
            bunch = np.exp(-((z - (ANKLE_Z + 0.07)) / 0.035) ** 2)
            d += 0.005 * bunch * np.sin(z / 0.03 * 2 * math.pi + 1.5 * cs.fold_noise(ang, 8))
        knee = np.exp(-((z - 0.55) / 0.06) ** 2)
        d += 0.0025 * knee * np.sin((z + 0.02 * np.sin(ang * 2)) / 0.035 * 2 * math.pi)
        crotch = np.exp(-((z - 0.86) / 0.05) ** 2) * (np.abs(co[:, 0]) < 0.12)
        d += 0.003 * crotch * np.sin((z + 0.6 * np.abs(co[:, 0])) / 0.03 * 2 * math.pi)
        return d

    cs.displace(tr, shape)
    if boots is not None:
        pieces.tuck(tr, boots, axes, boot_top, blouse=0.014)
        cs.taubin(tr, 2)
    cs.decimate(tr, tris=TROUSER_TRIS)
    for band in STRIPES_LEG:
        for y0 in band:
            cs.bisect(tr, (0, y0, 0), (0, 1, 0), lambda c, n: np.sign(n[:, 0]) * np.sign(c[:, 0]) > 0.3)
    if boots is None:
        cs.bisect(tr, (0, 0, ANKLE_Z + 0.035), (0, 0, 1))
    c, n = cs.face_data(tr)
    cs.set_material(tr, mats["suit"])
    if boots is None:
        cs.set_material(tr, mats["rib"], c[:, 2] < ANKLE_Z + 0.035)
    stripe = np.zeros(len(c), bool)
    for lo, hi in STRIPES_LEG:
        stripe |= (c[:, 1] > lo) & (c[:, 1] < hi)
    stripe &= (np.sign(n[:, 0]) * np.sign(c[:, 0]) > 0.3) & (np.abs(c[:, 0]) > 0.13) & (c[:, 2] > (ANKLE_Z + 0.035 if boots is None else boot_top))
    cs.set_material(tr, mats["stripe"], stripe)
    inner = cs.solidify(tr, 0.003)
    cs.set_material(tr, mats["suit"], inner)
    cs.raise_faces(tr, cs.material_mask(tr, mats["stripe"]) & (cs.get_face_int(tr, "shell") == 0), 0.0008)
    cs.box_uv(tr, 0.03)
    cs.mark_sharp(tr, 60)
    W = cs.smooth_values(body.transfer(cs.get_co(tr)), cs.get_edges(tr), 3, 0.5)
    return tr, W


def build_tracksuit(body, mats, boots=None, boot_top=0.0):
    return build_jacket(body, mats) + [build_trousers(body, mats, boots, boot_top)]


def dress_head(body, head, hair_file="Hair_Buzzed.gltf", keep=None):
    """Skin where nothing covers it (head and neck, and the hands: no gloves), eyes, brows, hair."""
    hands = np.abs(body.co[:, 0]) > CUFF_X - CUFF_W
    body.keep_skin((pieces.skin_above_collar(body) if keep is None else keep) | hands)
    cs.set_material(body.skin, head["skin"])
    body.eyes.data.materials.clear()
    body.eyes.data.materials.append(head["eyes"])
    body.brows.data.materials.clear()
    body.brows.data.materials.append(head["hair"])
    parts = [body.skin, body.eyes, body.brows]
    if hair_file:
        parts.append(body.add_hair(hair_file, head["hair"]))
    return parts


def skin_all(body, pieces_out):
    for obj, W in pieces_out:
        if len(obj.data.uv_layers) == 0:
            cs.box_uv(obj, 0.04)
        if "shell" not in obj.data.attributes:   # the cloth shells were shaded when built
            cs.mark_sharp(obj, 45)
        cs.skin(obj, body, W)
    return [p for p, _w in pieces_out]


def build(args):
    body = cs.Body(args.assets_root)
    mats = make_materials(args.out)
    head = head_materials(body, args.out, "Thug", hair_tint=(0.012, 0.010, 0.009))
    out = build_tracksuit(body, mats)
    out += pieces.trainers(body, mats)
    parts = dress_head(body, head) + skin_all(body, out)
    cs.finish(body, parts, WHO, args)


if __name__ == "__main__":
    build(cs.parse_args(WHO))
