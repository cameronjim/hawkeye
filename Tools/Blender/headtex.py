"""Procedural textures for the enemies' heads: skin (albedo and normal), eyes, brows and hair. numpy only, no bpy, no
network, no model inference: nothing here reads a photograph or a trained model, and a face is not copied from anyone.

Everything is painted in the head's own UV space (u round the head from the back, v up from the neck's base: see
`headshape.HeadGrid.loop_uvs`) from the same numbers that shaped the mesh, so a feature is painted where it was
sculpted. Colours are authored as sRGB bytes (0..1), which is what `costume.save_image` writes; arrays are (rows from
the bottom, columns, channels), Blender's pixel order. Deterministic: `face.seed` is the only randomness.

An optional hand-made override: if a folder holds `<look id>_skin.png` (and `_skin_n.png`), headbuild uses that image
in place of this bake. That is the hook for anything made outside the repo (a hand-painted or locally generated map);
nothing in the repo produces one, and anything that would, with a licence still to be signed off, stays out of the
default path (docs/CREDITS.md).
"""

import math

import numpy as np

import headshape as hs
import looks

MOUTH_COLOUR = (0.95, 0.76, 0.72)    # multiplies the skin on the lips


def tileable_noise(h, w, seed, blur):
    """Unit-variance noise that tiles both ways; blur in texels (bigger is smoother)."""
    rng = np.random.default_rng(seed)
    white = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-(fy ** 2 + fx ** 2) * (blur ** 2) * 20.0)
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * g))
    return (out - out.mean()) / (out.std() + 1e-9)


def normal_from_height(h, strength):
    """OpenGL (+Y) tangent-space normals from a height field, as an (H, W, 3) 0..1 array (costume.normal_from_height)."""
    gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    gy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    n = np.stack([-gx, -gy, np.ones_like(h)], axis=2)
    n /= np.linalg.norm(n, axis=2)[:, :, None]
    return n * 0.5 + 0.5


def lerp(a, b, t):
    return a + (b - a) * t


class Frame:
    """Per-texel head coordinates for a texture of size (h, w): the angle from the front, the height, |x|, y."""

    def __init__(self, grid, h, w):
        self.h, self.w = h, w
        u = (np.arange(w) + 0.5) / w
        v = (np.arange(h) + 0.5) / h
        cols = u * grid.segs
        ext = np.concatenate([grid.theta, [grid.theta[0] + 2.0 * math.pi]])
        theta = np.interp(cols, np.arange(grid.segs + 1), ext)
        self.theta = (theta + math.pi) % (2.0 * math.pi) - math.pi
        self.z = np.interp(v * (grid.rows - 1), np.arange(grid.rows), grid.z)
        prof = hs.profile(grid.face, self.z, grid.neck_y)
        x, y = hs.ring_xy(prof, self.theta)
        self.ax = np.abs(x)
        self.x = x
        self.y = y
        self.zz = np.broadcast_to(self.z[:, None], x.shape)
        self.cos = np.broadcast_to(np.cos(self.theta)[None, :], x.shape)   # 1 at the front, -1 at the back
        self.t = hs.t_of(self.zz)


def _wrinkle(dist, width):
    return np.exp(-0.5 * (dist / width) ** 2)


def beard_mask(face, fr, lm):
    """Where facial hair shows, 0..1 (before density): the shape of the style on the face's lower half."""
    cos = fr.cos
    around = hs.smoothstep(-0.75, 0.0, cos)      # round to the ears, not the back of the head
    lips_core = hs.gauss(fr.ax, fr.zz - lm.mouth, lm.corner * 0.85, 0.0090)
    if face.facial_hair in ("stubble", "beard"):
        up = 0.040 if face.facial_hair == "beard" else 0.030
        edge = lm.sub + up * hs.smoothstep(0.02, 0.06, fr.ax)
        m = hs.smoothstep(edge + 0.006, edge - 0.006, fr.zz)
        m *= hs.smoothstep(hs.Z(-0.34), hs.Z(-0.12), fr.zz)     # fades down the neck
        if face.facial_hair == "stubble":
            m *= 0.85
        return np.clip(m * around * (1.0 - 0.8 * lips_core), 0.0, 1.0)
    if face.facial_hair == "goatee":
        chin = hs.gauss(fr.ax, fr.zz - hs.Z(0.12), 0.026, 0.034)
        mous = hs.gauss(fr.ax - 0.011, fr.zz - (lm.mouth + 0.0125), 0.0215, 0.0058)
        under = hs.gauss(fr.ax, fr.zz - (lm.ll - 0.012), 0.012, 0.007)
        return np.clip(np.maximum.reduce([chin, mous, under]) * around * (1.0 - 0.8 * lips_core), 0.0, 1.0)
    return np.zeros_like(fr.ax)


def skin_maps(face, grid, size=1024):
    """(albedo (size, size, 3), normal (size, size, 3), zones) for the head and neck."""
    h = w = size
    fr = Frame(grid, h, w)
    lm = hs.Landmarks(face)
    seed = face.seed * 1000
    base = np.array(looks.SKIN_TONES[face.skin])
    hair = np.array(looks.HAIR_COLOURS[face.hair_colour])
    ax, z = fr.ax, fr.zz
    front = hs.smoothstep(-0.1, 0.5, fr.cos)
    albedo = np.ones((h, w, 3)) * base
    # Slow drift in tone, a little redder in places, a little yellower in others.
    drift = tileable_noise(h, w, seed + 1, 40.0)
    drift2 = tileable_noise(h, w, seed + 2, 28.0)
    albedo *= (1.0 + 0.045 * drift)[:, :, None]
    albedo[:, :, 0] *= 1.0 + 0.020 * drift2
    albedo[:, :, 2] *= 1.0 - 0.030 * drift2
    # Flush: the cheeks, the nose, the ears' neighbourhood; more with age and the row's skin_ruddy.
    flush = hs.gauss(ax - 0.048, z - hs.Z(0.37), 0.014, 0.017) * front
    flush += 1.1 * hs.gauss(ax, z - (lm.sub + 0.008), 0.017, 0.022) * front
    flush += 1.6 * hs.gauss(ax - 0.073, z - hs.Z(0.47), 0.016, 0.034)
    albedo += (face.skin_ruddy * (0.55 + 0.45 * face.age) * 0.055 * np.clip(flush, 0, 1.4))[:, :, None] \
        * np.array([0.95, -0.40, -0.30])
    # The ears' patch (the ear meshes sample the skin round their anchor): a shade darker and redder than the cheek.
    ear_patch = hs.gauss(ax - 0.0745, z - hs.Z(0.47), 0.022, 0.036)
    albedo *= 1.0 - (0.12 * ear_patch)[:, :, None] * np.array([0.55, 0.95, 0.95])
    # Under the chin: the throat falls into shadow.
    albedo *= 1.0 - (0.15 * hs.smoothstep(hs.Z(0.04), hs.Z(-0.14), z) * hs.smoothstep(-0.6, 0.6, fr.cos))[:, :, None]
    # Eye sockets and under-eye shadow.
    socket = hs.gauss(ax - lm.eye_x, z - lm.eye, 0.023, 0.016) * front
    bag = hs.gauss(ax - lm.eye_x, z - (lm.eye - 0.016), 0.016, 0.006) * front
    shade = np.clip(0.10 * socket + (0.05 + 0.12 * face.age) * bag, 0, 0.3)
    albedo *= 1.0 - shade[:, :, None] * np.array([0.8, 1.0, 0.55])
    # Lips: the skin tinted toward red between a soft edge, a darker line where they meet, a lighter lower lip.
    lip_u = hs.gauss(ax, z - (lm.ul - 0.0005), lm.corner * 0.82, 0.0058)
    lip_l = hs.gauss(ax, z - (lm.ll + 0.0005), lm.corner * 0.80, 0.0070)
    lips = hs.smoothstep(0.12, 0.85, np.maximum(lip_u, lip_l)) * front
    albedo = lerp(albedo, albedo * np.array(MOUTH_COLOUR) * (0.92 + 0.08 * face.skin_ruddy), lips[:, :, None])
    albedo *= 1.0 - 0.40 * (hs.gauss(ax, z - lm.mouth, lm.corner, 0.0013) * front)[:, :, None]
    # The nose's own shading, the front view's only cue: nostrils, the shadow under the tip, the creases at the wings.
    nw = face.nose_width
    nostril = hs.gauss(ax - 0.0082 * nw, z - (lm.sub + 0.0040), 0.0036, 0.0024) * front
    under = hs.gauss(ax, z - (lm.sub - 0.0030), 0.0125 * nw, 0.0028) * front
    alar = hs.gauss(ax - 0.0185 * nw, z - (lm.sub + 0.0070), 0.0024, 0.0070) * front
    albedo *= 1.0 - np.clip(0.42 * nostril + 0.10 * under + 0.16 * alar, 0, 0.7)[:, :, None] * np.array([0.9, 1.0, 0.8])
    # Lash lines on the lid margins (the rim of the opening the eyeball shows through).
    ap, rr = hs.aperture(face, ax, z)
    lash = np.exp(-(((rr - 1.0) / 0.13) ** 2)) * front
    albedo *= 1.0 - np.clip(0.50 * lash, 0, 0.6)[:, :, None]
    # Skin under the hair: roots darken it, and a shaved head shows a faint shadow.
    ht = hs.hairline_t(face, fr.theta)
    above = hs.smoothstep(hs.Z(ht)[None, :] - 0.014, hs.Z(ht)[None, :] + 0.010, z)
    if face.hair_style == "bald":
        albedo = lerp(albedo, albedo * 0.88 + hair * 0.12, 0.55 * above[:, :, None])
    else:
        albedo = lerp(albedo, hair * 0.7 + albedo * 0.15, 0.80 * above[:, :, None])
    brow_zone = hs.gauss(ax - 0.030, z - lm.brow, 0.026, 0.0085) * front
    albedo = lerp(albedo, albedo * 0.7 + hair * 0.3, 0.35 * brow_zone[:, :, None])
    # Facial hair: a shadow under the skin, then dots of hair, with grey flecks as the face ages.
    zones = {"beard": beard_mask(face, fr, lm), "hairline": above, "lips": lips}
    if face.facial_hair != "none" and face.stubble > 0.0:
        m = zones["beard"]
        dens = face.stubble
        albedo = lerp(albedo, albedo * 0.65 + hair * 0.35, (0.55 * dens * m)[:, :, None])
        dots = tileable_noise(h, w, seed + 3, 0.55)
        cover = np.clip(m * dens, 0.0, 1.0)
        hair_dots = (dots > (2.35 - 1.75 * cover)) & (cover > 0.05)
        grey = np.array([0.55, 0.54, 0.52])
        flecks = tileable_noise(h, w, seed + 4, 0.55) > (1.8 - 1.2 * face.age)
        stub_col = np.where((flecks & hair_dots)[:, :, None], grey, hair * 0.9)
        albedo = np.where(hair_dots[:, :, None], lerp(albedo, stub_col, 0.6), albedo)
    # Fine grain, freckle-scale blotches.
    albedo *= 1.0 + 0.020 * tileable_noise(h, w, seed + 5, 0.8)[:, :, None]
    blot = tileable_noise(h, w, seed + 6, 7.0)
    albedo *= 1.0 - (0.025 * (0.4 + face.age)) * np.clip(blot - 1.0, 0.0, 2.0)[:, :, None] * np.array([0.6, 0.8, 1.0])
    albedo = np.clip(albedo, 0.0, 1.0)

    # Height: pores, lines of age, the lips' fine vertical lines.
    pores = 0.55 * tileable_noise(h, w, seed + 7, 0.6) + 0.35 * tileable_noise(h, w, seed + 8, 1.6) \
        + 0.35 * tileable_noise(h, w, seed + 9, 6.0)
    ht_map = 0.0065 * pores
    lines = np.zeros((h, w))
    wr = 0.15 + 0.85 * face.age
    wave = 0.0016 * np.sin(fr.x * 160.0 + 3.0 * np.sin(fr.x * 61.0 + seed))
    for k in range(4):
        zk = hs.Z(0.655 + 0.032 * k) + 0.002 * k
        lines += (0.75 - 0.08 * k) * _wrinkle(z - zk - wave, 0.0014) * hs.smoothstep(0.052, 0.036, ax) * front
    # Crow's feet, from each outer eye corner.
    for k in range(3):
        d, t = hs._segment_distance(ax, z, (lm.eye_x + lm.eye_w + 0.003, lm.eye + 0.002 * (k - 1)),
                                    (lm.eye_x + lm.eye_w + 0.018, lm.eye + 0.011 * (k - 1)))
        lines += 0.55 * _wrinkle(d, 0.0012) * hs.smoothstep(0.0, 0.2, t) * front
    # Under the eye, and on the neck.
    lines += 0.40 * _wrinkle(z - (lm.eye - 0.0105) - 0.0006 * np.sin(ax * 300), 0.0010) * hs.smoothstep(0.012, 0.02,
                                                                                                           ax) \
        * hs.smoothstep(0.045, 0.03, ax) * front
    neck = hs.smoothstep(hs.Z(-0.02), hs.Z(-0.12), z) * hs.smoothstep(hs.Z(-0.5), hs.Z(-0.2), z)
    for k in range(4):
        lines += 0.6 * _wrinkle(z - (hs.Z(-0.11 - 0.07 * k) + 0.0015 * np.sin(fr.x * 90.0 + k)), 0.0016) * neck
    lip_lines = np.sin(fr.x * 1500.0 + 4.0 * tileable_noise(h, w, seed + 10, 0.8)) * lips
    ht_map = ht_map - 0.020 * wr * lines * 1.0 + 0.0012 * lip_lines
    ht_map -= 0.030 * zones["beard"] * face.stubble * (tileable_noise(h, w, seed + 3, 0.55) > 1.0)
    normal = normal_from_height(ht_map, 40.0)
    return albedo, normal, zones


def eye_image(face, size=256):
    """The eyeball seen from the front: sclera with a few veins and a shaded rim, an iris with fibres and a dark
    limbal ring, a pupil. The cornea highlight is not here: the eye's cornea is a dome and the engine's light puts it
    there."""
    h = w = size
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    dx = (u[None, :] - 0.5) * 2.0
    dz = (v[:, None] - 0.5) * 2.0
    rho = np.sqrt(dx * dx + dz * dz)
    phi = np.arctan2(dz, dx)
    seed = face.seed * 31
    inner, outer = (np.array(c) for c in looks.EYE_COLOURS[face.eye_colour])
    sclera = np.array([0.80, 0.76, 0.71])
    rng = np.random.default_rng(seed)
    out = np.ones((h, w, 3)) * sclera
    veins = np.zeros((h, w))
    for _ in range(14):
        a0 = rng.uniform(0, 2 * math.pi)
        r_start = rng.uniform(0.62, 0.9)
        d = np.abs(((phi - a0 + math.pi) % (2 * math.pi)) - math.pi)
        veins += np.exp(-0.5 * (d / 0.035) ** 2) * hs.smoothstep(r_start, 0.99, rho) * rng.uniform(0.4, 1.0)
    out = lerp(out, out * np.array([0.95, 0.55, 0.52]), np.clip(0.30 * veins * (0.4 + face.age), 0, 0.5)[:, :, None])
    out *= (1.0 - 0.50 * hs.smoothstep(0.62, 1.0, rho))[:, :, None]
    fib = tileable_noise(h, w, seed, 0.9)
    fibres = 0.5 + 0.5 * np.sin(phi * 38.0 + 2.0 * fib)
    t = np.clip(rho / 0.5, 0.0, 1.0)
    iris = lerp(inner, outer, hs.smoothstep(0.30, 1.0, t)[:, :, None])
    iris = iris * (0.78 + 0.45 * fibres * hs.smoothstep(0.2, 0.5, t))[:, :, None]
    iris = lerp(iris, iris * 0.35, hs.smoothstep(0.82, 1.0, t)[:, :, None])           # limbal ring
    in_iris = hs.smoothstep(0.52, 0.47, rho)
    out = lerp(out, iris, in_iris[:, :, None])
    pupil = hs.smoothstep(0.215, 0.185, rho)
    out = lerp(out, np.array([0.012, 0.011, 0.011]), pupil[:, :, None])
    return np.clip(out, 0.0, 1.0)


def brow_image(face, width=256, height=64):
    """A brow card as RGBA: many short strokes of hair along the brow, thicker inside and thinning to the tail, the
    alpha clipped (the engine's masked blend). u runs from the inner end, v from the lower edge to the upper."""
    rng = np.random.default_rng(face.seed * 17)
    hair = np.array(looks.HAIR_COLOURS[face.hair_colour])
    rgb = np.ones((height, width, 3)) * hair
    alpha = np.zeros((height, width))
    strokes = int(330 + 330 * face.brow)
    for _ in range(strokes):
        u0 = rng.uniform(0.0, 1.0)
        half = 0.5 * (0.55 + 0.45 * hs.smoothstep(0.0, 0.18, u0)) * (1.0 - 0.60 * hs.smoothstep(0.35, 1.0, u0))
        v0 = 0.5 + rng.uniform(-half, half) * 0.9
        # Hairs grow up and out at the inner end, flat along the body, down and out at the tail.
        angle = lerp(0.55, -0.35, float(hs.smoothstep(0.0, 1.0, u0))) + rng.normal(0, 0.12)
        length = rng.uniform(0.10, 0.26)
        shade = rng.uniform(0.9, 1.9)
        for s in np.linspace(0.0, 1.0, 36):
            u = u0 + math.cos(angle) * length * s
            v = v0 + math.sin(angle) * length * s * 0.5
            i = int(u * (width - 1))
            j = int(v * (height - 1))
            if 0 <= i < width and 0 <= j < height:
                alpha[j, i] = 1.0
                alpha[min(j + 1, height - 1), i] = 1.0 if s < 0.7 else alpha[min(j + 1, height - 1), i]
                rgb[j, i] = hair * shade
                rgb[min(j + 1, height - 1), i] = hair * shade
    return np.concatenate([np.clip(rgb, 0, 1), alpha[:, :, None]], axis=2)


def hair_maps(face, size=512):
    """(albedo (size, size, 3), normal) for a hair cap: strands as stripes round the head (u), root to crown (v);
    short hair reads as grooves and tone, buzzed hair as a darker, finer pattern. Tiles in u."""
    h = w = size
    seed = face.seed * 53
    rng = np.random.default_rng(seed)
    colour = np.array(looks.HAIR_COLOURS[face.hair_colour])
    x = np.arange(w) / w
    strands = np.zeros(w)
    freqs = (31, 47, 71, 109, 163) if face.hair_style != "buzz" else (97, 151, 223, 331, 449)
    for f in freqs:
        strands += np.sin(2 * math.pi * (f * x + rng.uniform()))
    strands = (strands - strands.min()) / (strands.max() - strands.min())
    tone = 0.62 + 0.62 * strands[None, :] + 0.10 * tileable_noise(h, w, seed + 1, 3.0)
    if face.hair_style == "buzz":
        tone = 0.70 + 0.10 * strands[None, :] + 0.22 * tileable_noise(h, w, seed + 2, 0.5)
    rgb = np.clip(tone[:, :, None] * colour[None, None, :], 0, 1)
    height = np.broadcast_to(strands[None, :], (h, w)) + 0.15 * tileable_noise(h, w, seed + 3, 1.2)
    # Alpha: solid up on the head, dissolving into stipple toward the hairline (v 0) so no hard edge reads as a wig.
    v = ((np.arange(h) + 0.5) / h)[:, None]
    dens = 0.10 + 0.90 * hs.smoothstep(0.0, 0.34 if face.hair_style in ("buzz", "bald") else 0.22, v)
    u01 = 0.5 * (1.0 + np.tanh(0.8 * tileable_noise(h, w, seed + 9, 0.55)))
    alpha = (u01 < dens).astype(np.float64)
    return np.concatenate([rgb, alpha[:, :, None]], axis=2), normal_from_height(height, 3.0 if face.hair_style != "buzz" else 1.2)
