"""Skin textures baked from the head's own geometry (claude-docs/blender-pipeline.md, "Enemy heads, second pass").

numpy only. The subdivided head (and the ears, which borrow the head's texture around them) is rasterised into its UV
space; every texel then knows where it is on the face (its position, normal, ambient occlusion from rays cast on the
mesh, and the cage's vertex weights carried through the subdivision: the lips, the lid margins, the inner pockets). The
colour is the skin tone, slow drifts in it, the flush a face has at the cheeks, nose and ears, the cage's own occlusion
(sockets, the lip line, under the nose and lip, the nasolabial fold are dark because the mesh shades itself, not because
a shadow was painted), the lips from the lip geometry, stubble and beards from the jaw's own coordinates, a feathered
hairline. The normal map is pores and stubble pits only; every form is in the mesh.

Deterministic: `face.seed` is the only randomness. Arrays are (rows from the bottom, columns, channels), Blender's pixel
order, sRGB 0..1.
"""

import math

import numpy as np

import headtex as ht
import looks

LIP_HUE = np.array([0.88, 0.60, 0.58])        # multiplies the skin on the lips
SOCKET_HUE = np.array([0.80, 0.60, 0.55])     # what the inside of the lids and the pockets take on


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def rasterise(corner_uv, tri_corners, tri_verts, attrs, size):
    """Per-texel values of per-vertex `attrs` ({name: (n, k)}): ({name: (size, size, k)}, covered (size, size) bool)."""
    out = {k: np.zeros((size, size, v.shape[1])) for k, v in attrs.items()}
    cover = np.zeros((size, size), bool)
    for tc, tv in zip(tri_corners, tri_verts):
        uv = corner_uv[tc] * size
        x0 = max(int(math.floor(uv[:, 0].min() - 0.5)), 0)
        x1 = min(int(math.ceil(uv[:, 0].max() + 0.5)), size - 1)
        y0 = max(int(math.floor(uv[:, 1].min() - 0.5)), 0)
        y1 = min(int(math.ceil(uv[:, 1].max() + 0.5)), size - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        px, py = xs + 0.5, ys + 0.5
        (ax, ay), (bx, by), (cx, cy) = uv
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-12:
            continue
        w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
        w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
        w2 = 1.0 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any():
            continue
        iy, ix = ys[inside], xs[inside]
        bary = np.stack([w0[inside], w1[inside], w2[inside]], axis=1)
        for name, a in attrs.items():
            out[name][iy, ix] = bary @ a[list(tv)]
        cover[iy, ix] = True
    return out, cover


def fill_gutter(img, cover, steps=10):
    """Copy covered texels outward (toward every uncovered neighbour) so mip levels and filtering never pull in black."""
    img = img.copy()
    cov = cover.copy()
    for _ in range(steps):
        if cov.all():
            break
        acc = np.zeros_like(img)
        cnt = np.zeros(cov.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            sh_c = np.roll(np.roll(cov, dy, axis=0), dx, axis=1)
            sh_i = np.roll(np.roll(img, dy, axis=0), dx, axis=1)
            acc += sh_i * sh_c[..., None]
            cnt += sh_c
        new = (~cov) & (cnt > 0)
        img[new] = acc[new] / cnt[new][:, None]
        cov = cov | new
    return img


class Noise3:
    """Smooth value noise on 3D positions (cm), so texture drift has no seam where the UV does."""

    def __init__(self, seed):
        rng = np.random.default_rng(seed)
        self.table = rng.standard_normal((16, 16, 16))

    def __call__(self, p, freq):
        q = p * freq
        i0 = np.floor(q).astype(int)
        f = q - i0
        f = f * f * (3.0 - 2.0 * f)
        out = 0.0
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    w = (f[..., 0] if dx else 1 - f[..., 0]) * (f[..., 1] if dy else 1 - f[..., 1]) \
                        * (f[..., 2] if dz else 1 - f[..., 2])
                    out = out + w * self.table[(i0[..., 0] + dx) % 16, (i0[..., 1] + dy) % 16, (i0[..., 2] + dz) % 16]
        return out


def local(pos, chin_z):
    """Position (m, the body's frame) to cm in the face's own: x, forward, height above the menton."""
    return np.stack([pos[..., 0] * 100.0, -pos[..., 1] * 100.0, (pos[..., 2] - chin_z) * 100.0], axis=-1)


def beard_mask(face, lx, lz, lyf, nz):
    """Where facial hair shows, 0..1 before density: the shape of the style on the jaw's own coordinates (x across, z
    up from the menton). The edges are soft (about a centimetre) and follow the cheek line, the jaw and the lip."""
    ax = np.abs(lx)
    front = smoothstep(-3.0, 3.0, lyf + 1.5 - 0.4 * ax)     # to the ears, not the back of the head
    mouth = np.exp(-0.5 * ((ax / 2.6) ** 2 + ((lz - 4.1) / 0.55) ** 2))     # the lips themselves are bare
    if face.facial_hair in ("stubble", "beard"):
        top = (5.7 if face.facial_hair == "beard" else 5.3) + 4.4 * smoothstep(1.2, 6.6, ax)             + 0.35 * np.sin(lx * 2.1 + lyf * 0.9)                                  # the cheek line, rising to the ear
        m = smoothstep(top + 0.9, top - 0.9, lz) * smoothstep(-6.5, -2.5, lz)
        m *= 0.85 if face.facial_hair == "stubble" else 1.0
        # a clean upper lip edge below the nose for stubble; a moustache joins a beard
        gap = np.exp(-0.5 * ((ax / 1.0) ** 2 + ((lz - 5.5) / 0.4) ** 2)) if face.facial_hair == "stubble" else 0.0
        return np.clip(m * front * (1.0 - 0.9 * mouth) * (1.0 - 0.5 * gap), 0.0, 1.0)
    if face.facial_hair == "goatee":
        chin = np.exp(-0.5 * ((ax / 2.4) ** 2 + ((lz - 1.3) / 2.2) ** 2))
        mous = np.exp(-0.5 * (((ax - 1.0) / 2.0) ** 2 + ((lz - 5.5) / 0.55) ** 2))
        patch = np.exp(-0.5 * ((ax / 0.9) ** 2 + ((lz - 2.5) / 0.55) ** 2))
        return np.clip(np.maximum.reduce([chin, mous, patch]) * front * (1.0 - 0.9 * mouth), 0.0, 1.0)
    return np.zeros_like(lx)


def hair_zone(face, lx, lz, lyf, noise, hairline_t):
    """1 on the scalp that carries hair (above the hairline, soft-edged, the edge broken up by noise)."""
    theta = np.arctan2(lx, lyf + 1.0)       # 0 at the front, +-pi at the back; the head's axis is about 1 cm behind 0
    edge = hairline_t(face, theta) * 22.5
    wob = 0.5 * noise
    return smoothstep(edge - 1.3 + wob, edge + 0.9 + wob, lz)


def skin_maps(face, parts, chin_z, size=1024, hairline_t=None):
    """(albedo (size, size, 3), normal (size, size, 3)) for the head.

    parts: dict with 'verts' (n, 3) metres, 'tris' (t, 3), 'tri_corners' (t, 3), 'corner_uv' (c, 2), 'normals' (n, 3),
    'ao' (n,), and the vertex weights 'lips', 'inner', 'lash', 'ridge' ((n,) each)."""
    verts = parts["verts"]
    attrs = {
        "pos": verts,
        "nrm": parts["normals"],
        "w": np.stack([parts["ao"], parts["lips"], parts["inner"], parts["lash"]], axis=1),
        "ridge": parts["ridge"][:, None],
    }
    ras, cover = rasterise(parts["corner_uv"], parts["tri_corners"], parts["tris"], attrs, size)
    for k in ras:
        ras[k] = fill_gutter(ras[k], cover)
    pos = ras["pos"]
    nrm = ras["nrm"] / np.maximum(np.linalg.norm(ras["nrm"], axis=2), 1e-9)[..., None]
    ao, lips, inner, lash = (ras["w"][..., k] for k in range(4))
    ridge = ras["ridge"][..., 0]
    loc = local(pos, chin_z)
    lx, lyf, lz = loc[..., 0], loc[..., 1], loc[..., 2]
    ax = np.abs(lx)
    seed = face.seed * 1000
    n3 = Noise3(seed)
    base = np.array(looks.SKIN_TONES[face.skin])
    hair = np.array(looks.HAIR_COLOURS[face.hair_colour])
    albedo = np.ones((size, size, 3)) * base
    # slow drift: tone, a little redder in places and yellower in others (on positions, so it crosses the UV seam)
    drift = n3(loc, 0.07) * 0.6 + n3(loc, 0.19) * 0.4
    albedo *= (1.0 + 0.04 * drift)[..., None]
    albedo[..., 0] *= 1.0 + 0.018 * n3(loc + 7.0, 0.11)
    albedo[..., 2] *= 1.0 - 0.025 * n3(loc + 3.0, 0.11)
    # flush: the cheeks over the bone, the nose, the ears, a little at the temples; more with age and skin_ruddy
    face_side = smoothstep(-0.5, 1.5, lyf + 3.0)
    cheek = np.exp(-0.5 * (((ax - 4.6) / 1.9) ** 2 + ((lz - 7.2) / 2.2) ** 2 + ((lyf - 6.0) / 3.5) ** 2))
    nose = np.exp(-0.5 * ((ax / 1.7) ** 2 + ((lz - 7.0) / 1.7) ** 2)) * (lyf > 6.5)
    ear = np.exp(-0.5 * (((ax - 7.4) / 1.4) ** 2 + ((lz - 9.3) / 3.5) ** 2))
    flush = (0.9 * cheek + 1.0 * nose) * face_side + 1.5 * ear
    albedo += (face.skin_ruddy * (0.55 + 0.45 * face.age) * 0.06 * np.clip(flush, 0, 1.5))[..., None] \
        * np.array([0.95, -0.40, -0.30])
    # the cage's own occlusion shades the sockets, the lip line, under the nose, the fold beside the mouth
    shade = 1.0 - 0.92 * (1.0 - np.clip(ao, 0.0, 1.0)) ** 1.1
    albedo *= shade[..., None] * np.array([1.0, 0.94, 0.92]) + (1.0 - shade[..., None]) * 0.0
    # lids' margins: a faint dark lash line; the inside of the lids, the mouth's pocket and the nostrils: dark red
    albedo *= 1.0 - 0.45 * np.clip(lash, 0, 1)[..., None]
    albedo = lerp(albedo, albedo * SOCKET_HUE * 0.35, np.clip(inner * 1.4, 0, 1)[..., None])
    # lips: from the lip geometry (the weight is 1 on the lip line and the vermilion border, 0 on the ring outside)
    lip_t = smoothstep(0.25, 0.85, lips)
    lip_col = albedo * LIP_HUE * (0.93 + 0.07 * face.skin_ruddy)
    albedo = lerp(albedo, lip_col, lip_t[..., None])
    # hair: the scalp under it (roots) and the brow ridge's faint tone
    hz = hair_zone(face, lx, lz, lyf, n3(loc, 0.9), hairline_t) if hairline_t is not None else np.zeros_like(lx)
    if face.hair_style == "bald":
        albedo = lerp(albedo, albedo * 0.90 + hair * 0.10, 0.55 * hz[..., None])
    else:
        albedo = lerp(albedo, hair * 0.62 + albedo * 0.18, 0.78 * hz[..., None])
    albedo = lerp(albedo, albedo * 0.85 + hair * 0.15, 0.30 * np.clip(ridge, 0, 1)[..., None])
    # facial hair: a shadow under the skin, then dots of hair, grey flecks as the face ages
    bm = beard_mask(face, lx, lz, lyf, nrm[..., 2]) * (1.0 - 0.9 * hz)
    h = w = size
    dots = ht.tileable_noise(h, w, seed + 3, 0.55)
    if face.facial_hair != "none" and face.stubble > 0.0:
        dens = face.stubble
        albedo = lerp(albedo, albedo * 0.68 + hair * 0.32, (0.55 * dens * bm)[..., None])
        cover_v = np.clip(bm * dens, 0.0, 1.0)
        hair_dots = (dots > (2.35 - 1.75 * cover_v)) & (cover_v > 0.05)
        flecks = ht.tileable_noise(h, w, seed + 4, 0.55) > (1.8 - 1.2 * face.age)
        stub = np.where((flecks & hair_dots)[..., None], np.array([0.55, 0.54, 0.52]), hair * 0.9)
        albedo = np.where(hair_dots[..., None], lerp(albedo, stub, 0.6), albedo)
    # grain and blotches
    albedo *= 1.0 + 0.020 * ht.tileable_noise(h, w, seed + 5, 0.8)[..., None]
    blot = n3(loc, 0.45)
    albedo *= 1.0 - (0.022 * (0.4 + face.age)) * np.clip(blot - 0.8, 0.0, 2.0)[..., None] * np.array([0.6, 0.8, 1.0])
    albedo = np.clip(albedo, 0.0, 1.0)
    # normal: pores and pits only
    pores = 0.55 * ht.tileable_noise(h, w, seed + 7, 0.6) + 0.35 * ht.tileable_noise(h, w, seed + 8, 1.6) \
        + 0.30 * ht.tileable_noise(h, w, seed + 9, 6.0)
    height = 0.0034 * pores
    height -= 0.030 * bm * face.stubble * (dots > 1.0)
    height *= 1.0 - 0.7 * lip_t
    # fine vertical lines on the lips
    height += 0.0012 * np.sin(lx * 14.0 + 3.0 * ht.tileable_noise(h, w, seed + 10, 0.8)) * lip_t
    normal = ht.normal_from_height(height, 40.0)
    return albedo, normal
