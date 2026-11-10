"""Where the traversable ledges go: one slab per roof edge, ledge_spots. No ``unreal``;
generate_city re-exports every name and spawns BP_TraversableBlock from these spots.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402
from _district import GENERATOR_VERSION, PARAPET_HEIGHT, PARAPET_MIN_HEIGHT_M, PARAPET_THICK  # noqa: E402


# The Game Animation Sample's traversal (AC_TraversalLogic on BP_Kate) sweeps forward on its
# Traversable trace channel - our first custom channel - and only acts on a LevelBlock_Traversable
# it hits. That block is a 1 m cube (pivot at a bottom corner) with four ledge splines on its top
# edges, Ledge_1 along local y = 0 facing -Y and Ledge_2 opposite it along y = 100; a spline's up
# vector is the ledge's outward normal. So each roof edge gets one, scaled into a slab: local X
# along the edge, local -Y out of the facade, the top at the parapet top. Ledge_1 is then the
# outer top edge of the parapet (corner to corner), Ledge_2 its inner edge, and the short ends are
# narrower than the sample's 60 cm minimum ledge width. The slab is hidden, stands LEDGE_OUTSET
# proud of the facade so the sweep meets it before the wall, and blocks only that channel.
LEDGE_PREFIX = "City_Ledge_"
LEDGE_MIN_EDGE = 100.0      # cm; shorter roof edges get no ledge (the sample wants 60 cm of it)
LEDGE_OUTSET = 2.0          # cm the slab stands out from the facade
LEDGE_SLAB_HEIGHT = 400.0   # cm down from the top; covers a step up from a lower neighbour's roof
LEDGE_SPLINE = "Ledge_1"


def ledge_spots(district):
    """[(label, osm id, origin, yaw, scale, outer corner a, outer corner b, hash)] in a stable order.

    One per roof edge of LEDGE_MIN_EDGE or more, on every building. ``origin`` is the slab's
    bottom corner; ``a`` and ``b`` are where Ledge_1 should end: on the parapet top, LEDGE_OUTSET
    outside the facade line, at the two ends of the edge.
    """
    spots = []
    for rec in sorted(district.buildings, key=lambda r: r["id"]):
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3:
            continue
        height_cm = rec["height_m"] * 100.0
        parapet = rec["height_m"] >= PARAPET_MIN_HEIGHT_M
        top = height_cm + (PARAPET_HEIGHT if parapet else 0.0)
        slab = min(top, LEDGE_SLAB_HEIGHT)
        depth = PARAPET_THICK + LEDGE_OUTSET
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        n = len(ring)
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            length = math.hypot(bx - ax, by - ay)
            if length < LEDGE_MIN_EDGE:
                continue
            nx, ny = -(by - ay) / length * inward, (bx - ax) / length * inward
            ux, uy = ny, -nx                       # local X: turning it +90 degrees gives the inward normal
            if (ax * ux + ay * uy) > (bx * ux + by * uy):
                ax, ay, bx, by = bx, by, ax, ay
            ox, oy = ax - nx * LEDGE_OUTSET, ay - ny * LEDGE_OUTSET
            corner_a = (ox, oy, top)
            corner_b = (bx - nx * LEDGE_OUTSET, by - ny * LEDGE_OUTSET, top)
            yaw = math.degrees(math.atan2(uy, ux))
            scale = (length / 100.0, depth / 100.0, slab / 100.0)
            origin = (ox, oy, top - slab)
            spec = geo.record_hash(GENERATOR_VERSION, "ledge", rec["id"], i, list(origin), yaw, list(scale))
            spots.append((LEDGE_PREFIX + "{0}_{1}".format(rec["id"], i), rec["id"], origin, yaw, scale,
                          corner_a, corner_b, spec))
    return spots
