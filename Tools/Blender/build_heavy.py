"""The heavy: the Tracksuit Mafia's bruiser (claude-docs/blender-pipeline.md, "Heavy").

    blender.exe --background --factory-startup --python Tools/Blender/build_heavy.py -- [--no-render] [--views a,b]

The thug's dark red tracksuit (build_thug.build_jacket / build_trousers), with a black quilted padded vest over the
jacket (channels stitched across it, a zip, a stand collar), a black knit balaclava with one eye slot, and black
lace-up combat boots with the trousers tucked in. No hair. Writes <assets root>/built/heavy/SK_Heavy.glb (+ .blend,
slots json) and the renders.
"""

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_thug as bt  # noqa: E402
import costume as cs  # noqa: E402
import pieces  # noqa: E402

WHO = "heavy"
VEST_HEM = 0.93
QUILT = 0.07             # channel spacing
BOOT_TOP = 0.30
ARMHOLE_X = 0.232
SLOT = (1.668, 1.732, 0.068)   # the balaclava's eye slot: bottom, top, half width


def vest_materials(out):
    grain = cs.weave_normal("grain", out, strength=0.6)
    knit = cs.weave_normal("knit", out, strength=2.5)
    m = cs.material
    return {
        "vest": m("M_Heavy_Vest", (0.010, 0.010, 0.011), 0.32, normal=grain, normal_strength=0.12),
        "mask": m("M_Heavy_Mask", (0.011, 0.011, 0.011), 0.92, normal=knit, normal_strength=1.0, glow=True),
    }


def build_vest(body, mats, vmats):
    fam = body.family
    off = 0.05 * fam["torso"] + 0.03 * fam["arm"] + 0.02 * fam["head"] + 0.03 * fam["leg"]
    hang = pieces.torso_solid(body, VEST_HEM - 0.04, 1.38, pad=0.058, hem_in=0.01)
    keep = (body.co[:, 2] > 0.8) & (body.co[:, 2] < 1.62) & (np.abs(body.co[:, 0]) < 0.36)
    vest = pieces.shell(body, "Vest", off, extras=[hang], voxel=0.01, smooth=6, keep=keep)
    pieces.iron(body, vest, torso_iters=40, limb_iters=10)
    cs.cut_and_delete(vest, (0, 0, VEST_HEM), (0, 0, 1), lambda c, n: c[:, 2] < VEST_HEM)
    # Armholes: a clean vertical cut past the shoulder, and the web the remesh grew under each arm.
    for sx in (1, -1):
        cs.cut_and_delete(vest, (sx * ARMHOLE_X, 0, 0), (1, 0, 0), lambda c, n, sx=sx: sx * c[:, 0] > ARMHOLE_X)
    c, n = cs.face_data(vest)
    arm = body.families_of(body.transfer(c))["arm"]
    cs.delete_faces(vest, (arm > 0.6) & (c[:, 2] < 1.42))
    c, n = cs.face_data(vest)
    cs.delete_faces(vest, pieces.neck_hole(body, c, pad=0.062))
    pieces.keep_largest(vest)
    cy = pieces.TORSO_AXIS[1]

    def quilt(co, n):
        z = co[:, 2]
        ch = np.abs(np.sin(math.pi * (z - VEST_HEM) / QUILT)) ** 0.5
        return 0.008 * (ch - 0.7) * (np.abs(co[:, 0]) < 0.3)

    cs.displace(vest, quilt)
    cs.decimate(vest, tris=4000)
    pieces.snap_neckline(body, vest, pad=0.062)
    for sx in (1, -1):
        cs.bisect(vest, (sx * 0.005, 0, 0), (1, 0, 0), lambda c, n: (c[:, 1] < cy) & (np.abs(c[:, 0]) < 0.05))
    c, n = cs.face_data(vest)
    cs.set_material(vest, vmats["vest"])
    zipper = (np.abs(c[:, 0]) < 0.005) & (c[:, 1] < cy)
    cs.set_material(vest, mats["zip"], zipper)
    inner = cs.solidify(vest, 0.008)
    cs.set_material(vest, vmats["vest"], inner)
    cs.raise_faces(vest, cs.material_mask(vest, mats["zip"]) & (cs.get_face_int(vest, "shell") == 0), 0.002)
    cs.box_uv(vest, 0.04)
    cs.mark_sharp(vest, 55)
    co = cs.get_co(vest)
    W = cs.reassign(body, body.transfer(co), "leg", "pelvis")   # the hem hangs from the hips, not the thighs
    W = cs.smooth_values(W, cs.get_edges(vest), 6, 0.5)
    collar = pieces.stand_collar(body, vmats["vest"], vmats["vest"], z0=1.44, z1=1.6, front_drop=0.04, gap_deg=8.0,
                                 pad0=0.068, pad1=0.068, thickness=0.01, name="VestCollar")
    return [(vest, W), collar]


def build_mask(body, vmats):
    """A knit balaclava: the head and neck pushed out a few mm (the ears merge in), one eye slot cut on clean
    lines, its foot tucked into the collar."""
    fam = body.family
    keep = (fam["head"] > 0.2) & (body.co[:, 2] > 1.42)
    mask = pieces.shell(body, "Balaclava", 0.006, voxel=0.005, smooth=10, keep=keep)
    cs.cut_and_delete(mask, (0, 0, 1.46), (0, 0, 1), lambda c, n: c[:, 2] < 1.46)
    lo, hi, hw = SLOT

    def front(c, n):
        return (c[:, 1] < -0.02) & (np.abs(c[:, 0]) < hw + 0.03) & (c[:, 2] > lo - 0.02) & (c[:, 2] < hi + 0.02)

    for z0 in (lo, hi):
        cs.bisect(mask, (0, 0, z0), (0, 0, 1), front)
    for sx in (1, -1):
        cs.bisect(mask, (sx * hw, 0, 0), (1, 0, 0), front)
    c, n = cs.face_data(mask)
    cs.delete_faces(mask, (c[:, 1] < -0.02) & (np.abs(c[:, 0]) < hw) & (c[:, 2] > lo) & (c[:, 2] < hi))
    pieces.keep_largest(mask)
    cs.decimate(mask, tris=2000)
    cs.set_material(mask, vmats["mask"])
    cs.solidify(mask, 0.003)
    cs.box_uv(mask, 0.02)
    cs.mark_sharp(mask, 60)
    return mask, cs.smooth_values(body.transfer(cs.get_co(mask)), cs.get_edges(mask), 2, 0.5)


def build(args):
    body = cs.Body(args.assets_root)
    mats = bt.make_materials(args.out, who="Heavy")
    vmats = vest_materials(args.out)
    head = bt.head_materials(body, args.out, "Heavy", hair_tint=(0.02, 0.016, 0.013))
    boots = pieces.combat_boots(body, mats, top_z=BOOT_TOP)
    out = bt.build_jacket(body, mats)
    out.append(bt.build_trousers(body, mats, boots=boots[0][0], boot_top=BOOT_TOP))
    out += boots
    out += build_vest(body, mats, vmats)
    out.append(build_mask(body, vmats))
    # Skin shows only through the eye slot (and the hands); no hair under the mask.
    lo, hi, hw = SLOT
    co = body.co
    slot = (co[:, 1] < -0.02) & (np.abs(co[:, 0]) < hw + 0.012) & (co[:, 2] > lo - 0.012) & (co[:, 2] < hi + 0.012)
    parts = bt.dress_head(body, head, hair_file=None, keep=slot) + bt.skin_all(body, out)
    cs.finish(body, parts, WHO, args)


if __name__ == "__main__":
    build(cs.parse_args(WHO))
