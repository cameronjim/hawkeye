"""How a building's street walls are split into facades and shop bays (look pass 3, claude-docs/city-look.md).

M_Facade (``_look.FACADE_HLSL``) draws this and ``_street_look`` dresses it, so both read the same numbers: each
building's frame (its long axis A, its extents along A and the normal B, and an integer seed) is custom primitive
data on its actor (``generate_city.building_pieces``), and every choice made here hashes integers with ``ih``, the
same 32-bit integer hash the shader runs (``FLook.IH``), so the two agree exactly.

* A wall whose tangent runs along A or B spans the building's whole extent on that axis: ``face_span``.
* A span over SEG_MAX is cut into several facades of their own (``segments``), each a different building to the eye:
  its own masonry, colour, window trim, cornice and shop rhythm. The cuts land on the 160 cm window grid.
* Each facade's ground floor is a row of shop bays from its own corner to its own corner (``shop_bays``), with an
  end pier at either side; one bay of three or more is the tenement's entrance. A sign belongs to exactly one bay.

Plain Python.
"""

import math

import _geomutil as gu

SEG_MAX = 1300.0          # cm; a long front is cut into facades about this wide (a few tenement lots)
SEG_JITTER = 0.3          # of a facade's share of the span
BAY_GRID = 160.0          # cm, the window grid the cuts snap to
SEG_MIN = 640.0           # cm, the narrowest facade a cut may leave
END_PIER = 45.0           # cm of pier at either end of a facade's shopfronts
SHOP_PITCHES = (300.0, 360.0, 420.0)
SEED_MOD = 65521          # building seeds are integers below this
AXIS_COS = 0.97           # a wall whose tangent is this close to A or B spans the building on that axis
MAX_SEGMENTS = 8

MASK = 0xFFFFFFFF


def ih(x):
    """lowbias32 (Chris Wellons): the shader's FLook.IH, on unsigned 32-bit integers."""
    x &= MASK
    x ^= x >> 16
    x = (x * 0x7FEB352D) & MASK
    x ^= x >> 15
    x = (x * 0x846CA68B) & MASK
    x ^= x >> 16
    return x


def hf(x):
    """0..1 (24 bits) from ih: the shader's FLook.HF."""
    return (ih(x) >> 8) / 16777216.0


def key(seed, face, seg=0, bay=0, salt=0):
    """The integer every per-facade and per-bay choice hashes (FLook.Key)."""
    return (seed * 1031 + face * 7 + seg * 131071 + bay * 524287 + salt * 8191) & MASK


def _f32(x):
    import struct
    return struct.unpack("f", struct.pack("f", x))[0]


def building_seed(osm_id):
    return int(gu.stable_hash(osm_id, "facade") * SEED_MOD)


def frame(ring, osm_id):
    """(ax, ay, a_min, a_max, b_min, b_max, seed): the building's long axis A (a unit vector along an edge of its
    smallest bounding rectangle), the ring's extent along A and along B = (-ay, ax), world cm, and its seed. The custom
    primitive data M_Facade reads (CPD_FRAME .. CPD_FRAME + 6)."""
    rect = gu.oriented_rect(ring)
    yaw = math.radians(rect[4]) if rect else 0.0
    if rect and rect[3] > rect[2]:
        yaw += math.pi * 0.5
    ax, ay = math.cos(yaw), math.sin(yaw)
    if ax < -1e-6 or (abs(ax) <= 1e-6 and ay < 0.0):
        ax, ay = -ax, -ay
    us = [x * ax + y * ay for x, y in ring]
    vs = [-x * ay + y * ax for x, y in ring]
    return (round(ax, 6), round(ay, 6), round(min(us), 1), round(max(us), 1), round(min(vs), 1), round(max(vs), 1),
            building_seed(osm_id))


def face_span(fr, tx, ty):
    """(u0, u1, face) for a wall whose shader tangent is (tx, ty) = (-ny, nx), or None if it is off both axes. u is the
    shader's coordinate along the wall, dot(world xy, tangent); face 0..3 names the side (+A, -A, +B, -B)."""
    ax, ay, a0, a1, b0, b1, _seed = fr
    ca = tx * ax + ty * ay
    cb = tx * -ay + ty * ax
    if abs(ca) > AXIS_COS:
        return (a0, a1, 0) if ca > 0.0 else (-a1, -a0, 1)
    if abs(cb) > AXIS_COS:
        return (b0, b1, 2) if cb > 0.0 else (-b1, -b0, 3)
    return None


def segment_count(length):
    return max(1, min(MAX_SEGMENTS, int(math.ceil((length - 1.0) / SEG_MAX))))


def segments(u0, u1, seed, face):
    """[(s0, s1, index)] along [u0, u1], in u order: one facade, or several cut on the window grid."""
    u0, u1 = _f32(u0), _f32(u1)
    length = u1 - u0
    n = segment_count(length)
    cuts = [u0]
    for k in range(1, n):
        j = hf(key(seed, face, k, 0, 1)) - 0.5
        b = u0 + length * (k + j * SEG_JITTER) / n
        b = math.floor(b / BAY_GRID + 0.5) * BAY_GRID
        b = min(max(b, cuts[-1] + SEG_MIN), u1 - SEG_MIN)
        cuts.append(b)
    cuts.append(u1)
    return [(cuts[i], cuts[i + 1], i) for i in range(n)]


def shop_bays(s0, s1, seed, face, seg):
    """(pitch, [(centre u, bay index)], entrance index or -1) for the facade [s0, s1]."""
    usable = (s1 - s0) - 2.0 * END_PIER
    target = SHOP_PITCHES[int(hf(key(seed, face, seg, 0, 2)) * len(SHOP_PITCHES)) % len(SHOP_PITCHES)]
    n = max(1, int(math.floor(usable / target + 0.5)))
    pitch = usable / n
    entrance = n // 2 if n >= 3 else -1
    return pitch, [(s0 + END_PIER + (i + 0.5) * pitch, i) for i in range(n)], entrance


def facade_style(seed, face, seg):
    """0..7: the palette entry of facade ``seg`` (_look.SEG_STYLES); 0 for the first, which keeps the building's own."""
    if seg == 0:
        return -1
    return int(hf(key(seed, face, seg, 0, 3)) * 7.0) % 7


def trim_style(seed, face, seg):
    """Window heads: 0 flat stone lintel, 1 brick arch, 2 stone surround, 3 projecting hood."""
    return int(hf(key(seed, face, seg, 0, 4)) * 4.0) % 4


def cornice_style(seed, face, seg):
    """0 stepped stone with dentils, 1 brackets under a slab, 2 a plain coping, 3 a deep pressed-metal cornice."""
    return int(hf(key(seed, face, seg, 0, 5)) * 4.0) % 4


def shop_open(seed, face, seg, bay, lit_fraction=0.35):
    """(vacant, open at night) for one bay, as M_Facade gates it."""
    vacant = hf(key(seed, face, seg, bay, 6)) < 0.15
    return vacant, (not vacant) and hf(key(seed, face, seg, bay, 7)) < lit_fraction + 0.2


def shop_trade(seed, face, seg, bay, count):
    return int(hf(key(seed, face, seg, bay, 8)) * count) % count
