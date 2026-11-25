"""Reshape the pack's bodybuilder into an ordinary man, under the clothes (claude-docs/blender-pipeline.md, "Enemy heads
and bodies").

The garments are built round `body.reference`, a pristine copy of the pack's Superhero_Male: shoulders 54 cm across the
deltoids, a 27 cm deep chest, abs, thighs 13 cm in radius. A tracksuit over that reads as a tracksuit over muscle, however
it is smoothed. `reshape_body` narrows and softens that copy by a `looks.BodyBuild` before any garment measures it, so
every garment (the arm tubes, the leg tubes, the torso solid, the shell offsets) is built round the smaller man and
hangs from the places a real jacket hangs from. Only the reference moves: the skeleton, its rest pose and the weights
are untouched (the retargeters, `_handpose.py` and the montages see the same armature), and the visible skin that is left
(the hands) is not reshaped.

The scale profiles (`looks.torso_scale_profiles`) are plain Python and tested (`test_looks.py`); the mesh part needs Blender.
"""

import numpy as np

import costume as cs
import looks
import pieces

PROFILE_Z = looks.PROFILE_Z


def welded_taubin(obj, iters, mask=None):
    """cs.taubin over the mesh with coincident vertices treated as one. The glTF body's vertices are split at the UV
    seams; smoothing each copy on its own tears the seams open and the voxel remesh then fills only crumbs."""
    co = cs.get_co(obj)
    key = np.round(co * 1e5).astype(np.int64)
    _u, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1)
    edges = inv[cs.get_edges(obj)]
    edges = edges[edges[:, 0] != edges[:, 1]]
    wmask = None
    if mask is not None:
        wmask = np.zeros(len(first), bool)
        np.logical_or.at(wmask, inv, np.asarray(mask, bool))
    wco = smooth_welded(co[first], edges, iters, wmask)
    cs.set_co(obj, wco[inv])


def weld_normals(co, normals):
    """Normals averaged over vertices at the same place. The glTF's seam vertices are duplicates with their own normals;
    offset along those they part, the offset shell has cracks, and the voxel remesh (which needs a closed surface)
    sometimes fills only a crust of it (thug_d: 5.6k vertices instead of 90k at one voxel size, then no feet)."""
    key = np.round(co * 1e5).astype(np.int64)
    _u, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    acc = np.zeros((inv.max() + 1, 3))
    np.add.at(acc, inv, normals)
    acc /= np.maximum(np.linalg.norm(acc, axis=1), 1e-9)[:, None]
    return acc[inv]


def smooth_welded(wco, edges, iters, wmask):
    return cs.smooth_values(wco, edges, iters * 2, factor=(0.5, -0.53), mask=wmask)


def reshape_body(body, build):
    """Move body.reference to the build's shape (and refresh body.co, normals and surface). Returns the mask of the
    vertices that moved."""
    co = body.co.copy()
    x, y, z = co[:, 0], co[:, 1], co[:, 2]
    fam = body.family
    cy = pieces.TORSO_AXIS[1]
    ref = co.copy()

    # Torso: width and depth by height, on the vertices the torso bones own.
    w_t = cs.smoothstep(0.35, 0.75, fam["torso"])
    px, pf, pb = looks.torso_scale_profiles(build)
    sx = np.interp(z, PROFILE_Z, px)
    sf = np.interp(z, PROFILE_Z, pf)
    sb = np.interp(z, PROFILE_Z, pb)
    co[:, 0] = x * (1.0 + (sx - 1.0) * w_t)
    dy = y - cy
    sy = np.where(dy < 0, sf, sb)
    co[:, 1] = cy + dy * (1.0 + (sy - 1.0) * w_t)
    # A belly: the stomach and the lower ribs pushed forward under the jacket, in the middle.
    belly = build.belly * 0.06 * np.exp(-(((z - 1.06) / 0.11) ** 2)) * np.exp(-((x / 0.15) ** 2)) * w_t * (dy < 0)
    co[:, 1] -= belly

    # Arms: girth about each arm's bone line, from the shoulder to near the wrist (the hands keep their size).
    w_a = cs.smoothstep(0.35, 0.75, fam["arm"])
    for side, sgn in (("l", 1), ("r", -1)):
        axis = pieces.arm_axis(body, side)
        sel = np.nonzero((np.sign(x) == sgn) & (w_a > 0))[0]
        if len(sel) == 0:
            continue
        ax = np.abs(x[sel])
        c = np.array([axis(v) for v in ax])
        f = 1.0 - (1.0 - build.arm) * cs.smoothstep(0.20, 0.30, ax) * cs.smoothstep(0.66, 0.54, ax) * w_a[sel]
        co[sel, 1] = c[:, 0] + (co[sel, 1] - c[:, 0]) * f
        co[sel, 2] = c[:, 1] + (co[sel, 2] - c[:, 1]) * f

    # Legs: girth about each leg's bone line, thigh to calf, easing out at the knee's ends and the ankle.
    w_l = cs.smoothstep(0.35, 0.75, fam["leg"])
    for side, sgn in (("l", 1), ("r", -1)):
        axis = pieces.leg_axis(body, side)
        sel = np.nonzero((np.sign(x) == sgn) & (w_l > 0) & (np.abs(x) > 0.01))[0]
        if len(sel) == 0:
            continue
        zz = z[sel]
        c = np.array([axis(v) for v in zz])
        f = 1.0 - (1.0 - build.thigh) * cs.smoothstep(0.12, 0.40, zz) * cs.smoothstep(1.02, 0.86, zz) * w_l[sel]
        co[sel, 0] = c[:, 0] + (co[sel, 0] - c[:, 0]) * f
        co[sel, 1] = c[:, 1] + (co[sel, 1] - c[:, 1]) * f

    moved = np.linalg.norm(co - ref, axis=1) > 1e-5
    cs.set_co(body.reference, co)
    # Blend the seams between regions (volume-keeping), only where something moved or is next to it.
    welded_taubin(body.reference, 3, mask=moved)
    body.co = cs.get_co(body.reference)
    body.normals = weld_normals(body.co, cs.get_vnormals(body.reference))
    body.surface = cs.Surface(body.reference)
    cs.log("body reshaped to", build.id, "-", int(moved.sum()), "verts moved")
    return moved
