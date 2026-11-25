"""Blender side of the head cage (claude-docs/blender-pipeline.md, "Enemy heads, second pass"): subdivides the cage of
`headcage` twice (Catmull-Clark, with the cage's creases at the lip line, the lid margins and the nostrils) and hands
back plain arrays; builds the ear cages the same way; and answers surface queries (a ray from the head's axis, the
nearest skin to a point) for the brows, the hair and the headwear, which were written against a ring grid.

Runs inside Blender 5.3 (bpy, mathutils). Deterministic; nothing is downloaded and no model is run.
"""

import math

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

import headshape as hs

LEVELS = 2


class Surface:
    """A subdivided mesh as arrays: verts (n, 3), faces (list of tuples), corner_uv ((corners, 2), in face order) and
    named per-vertex weights."""

    def __init__(self, verts, faces, corner_uv, weights):
        self.verts = verts
        self.faces = faces
        self.corner_uv = corner_uv
        self.weights = weights

    def tri_arrays(self):
        """Triangles (t, 3) of vertex indices, the face each came from, and the (t, 3) corner indices."""
        tris, owner, corners = [], [], []
        k = 0
        for fi, f in enumerate(self.faces):
            for a in range(1, len(f) - 1):
                tris.append((f[0], f[a], f[a + 1]))
                owner.append(fi)
                corners.append((k, k + a, k + a + 1))
            k += len(f)
        return np.array(tris), np.array(owner), np.array(corners)


def _cage_object(name, verts, faces, corner_uv, creases=(), groups=None, fix_normals=False):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], [tuple(int(i) for i in f) for f in faces])
    me.update()
    layer = me.uv_layers.new(name="UVMap")
    layer.data.foreach_set("uv", np.asarray(corner_uv, np.float32).ravel())
    if fix_normals:
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        bm.to_mesh(me)
        bm.free()
    if creases:
        edge_of = {}
        for e in me.edges:
            a, b = e.vertices
            edge_of[(min(a, b), max(a, b))] = e.index
        values = np.zeros(len(me.edges), np.float32)
        for a, b, w in creases:
            values[edge_of[(min(a, b), max(a, b))]] = w
        attr = me.attributes.new("crease_edge", "FLOAT", "EDGE")
        attr.data.foreach_set("value", values)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    for gname, w in (groups or {}).items():
        vg = obj.vertex_groups.new(name=gname)
        for i, wi in enumerate(w):
            if wi > 0.0:
                vg.add([i], float(wi), "REPLACE")
    return obj


def subdivide(name, verts, faces, corner_uv, creases=(), groups=None, levels=LEVELS, fix_normals=False):
    """Catmull-Clark `levels` times (the cage's UVs smoothed with it, the seam kept), as a Surface."""
    obj = _cage_object(name, verts, faces, corner_uv, creases, groups, fix_normals)
    mod = obj.modifiers.new("Subsurf", "SUBSURF")
    mod.subdivision_type = "CATMULL_CLARK"
    mod.levels = levels
    mod.render_levels = levels
    mod.quality = 4
    mod.uv_smooth = "PRESERVE_BOUNDARIES"
    mod.use_creases = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    try:
        n = len(me.vertices)
        co = np.empty(n * 3, np.float64)
        me.vertices.foreach_get("co", co)
        loops = len(me.loops)
        lv = np.empty(loops, np.int64)
        me.loops.foreach_get("vertex_index", lv)
        starts = np.empty(len(me.polygons), np.int64)
        me.polygons.foreach_get("loop_start", starts)
        totals = np.diff(np.append(starts, loops))
        uv = np.empty(loops * 2, np.float32)
        me.uv_layers[0].data.foreach_get("uv", uv)
        faces_out = [tuple(int(v) for v in lv[s:s + t]) for s, t in zip(starts, totals)]
        weights = {g.name: np.zeros(n) for g in obj.vertex_groups}
        names = {g.index: g.name for g in obj.vertex_groups}
        for i, v in enumerate(me.vertices):
            for g in v.groups:
                weights[names[g.group]][i] = g.weight
        out = Surface(co.reshape(-1, 3), faces_out, uv.reshape(-1, 2).astype(np.float64), weights)
    finally:
        ev.to_mesh_clear()
    me_cage = obj.data
    bpy.data.objects.remove(obj)
    bpy.data.meshes.remove(me_cage)
    return out


# --------------------------------------------------------------------------------------------------
# queries on the finished head (the brows, hair and headwear were written against a ring grid)
# --------------------------------------------------------------------------------------------------


class SurfaceGrid:
    """The head, resampled onto the old ring grid's structure so `headshape.cap_grid` (hair, beanie, cap) and
    `headshape.brim_arrays` work on it: `rows` rings of `segs` points (`theta` the angle from the front, `z` the
    heights), P and N from rays cast outward from the head's axis, plus the apex, and the queries those functions use
    (`row_of_t`, `column_of_theta`, `surface_at`, `eyes`)."""

    def __init__(self, surf, face, eyes, neck_y):
        self.face = face
        self.neck_y = neck_y
        self.theta = hs.angles(hs.SEGMENTS)
        self.z = hs.row_heights()
        self.z[-1] = hs.CROWN_Z - 0.0003      # the top ring just under the crown, so the shells' fan to the apex is flat
        self.rows, self.segs = len(self.z), len(self.theta)
        self._eyes = eyes
        tris, _owner, _corners = surf.tri_arrays()
        self.bvh = BVHTree.FromPolygons([tuple(map(float, v)) for v in surf.verts], [tuple(map(int, t)) for t in tris])
        self.surf = surf
        zs = np.linspace(hs.CHIN_Z + 0.01, hs.CROWN_Z - 0.004, 24)
        self._axis_z = zs
        self._axis_y = np.array([self._skull_mid(z0) for z0 in zs])
        self.P = np.zeros((self.rows, self.segs, 3))
        for r, z0 in enumerate(self.z):
            for c, th in enumerate(self.theta):
                self.P[r, c] = self._cast(z0, th)
        crown = self._cast_down()
        self.apex = crown
        self.N = hs.grid_normals(self.P, self.apex)

    def _skull_mid(self, z):
        """Middle of the skull front to back at height z, measured off the midline (the nose does not count)."""
        front = self.bvh.ray_cast(Vector((0.045, -0.3, z)), Vector((0.0, 1.0, 0.0)), 0.6)
        back = self.bvh.ray_cast(Vector((0.045, 0.3, z)), Vector((0.0, -1.0, 0.0)), 0.6)
        if front[0] is None or back[0] is None:
            return 0.0
        return 0.5 * (front[0].y + back[0].y)

    def ray_x(self, z, side, y):
        """x of the skull's side at height z and depth y (metres), 0 if the ray misses."""
        hit = self.bvh.ray_cast(Vector((0.0, y, z)), Vector((float(side), 0.0, 0.0)), 0.5)
        return float(hit[0].x) if hit[0] is not None else 0.0

    def axis_y(self, z):
        return float(np.interp(z, self._axis_z, self._axis_y))

    def _cast(self, z, theta):
        origin = Vector((0.0, self.axis_y(z), z))
        direction = Vector((math.sin(theta), -math.cos(theta), 0.0))
        hit = self.bvh.ray_cast(origin, direction, 0.5)
        if hit[0] is None:
            # a ring below the chin or above the crown: the nearest point on the skin
            near = self.bvh.find_nearest(origin + direction * 0.08)
            return np.array(near[0] if near[0] is not None else origin)
        return np.array(hit[0])

    def _cast_down(self):
        origin = Vector((0.0, self.axis_y(hs.CROWN_Z - 0.01), hs.CROWN_Z + 0.05))
        hit = self.bvh.ray_cast(origin, Vector((0.0, 0.0, -1.0)), 0.2)
        return np.array(hit[0]) if hit[0] is not None else np.array([0.0, origin.y, hs.CROWN_Z])

    def row_of_t(self, t):
        return np.interp(hs.Z(np.asarray(t, np.float64)), self.z, np.arange(self.rows))

    def column_of_theta(self, theta):
        return np.interp(np.asarray(theta, np.float64), self.theta, np.arange(self.segs), period=2 * math.pi)

    def eyes(self):
        return self._eyes

    def surface_at(self, x, z):
        """Front surface points and normals at x, z arrays (cm of the cage's own front: rays from in front, going back)."""
        x = np.atleast_1d(np.asarray(x, np.float64))
        z = np.atleast_1d(np.asarray(z, np.float64))
        pts = np.zeros((len(x), 3))
        nrm = np.zeros((len(x), 3))
        for i in range(len(x)):
            hit = self.bvh.ray_cast(Vector((x[i], -0.2, z[i])), Vector((0.0, 1.0, 0.0)), 0.5)
            if hit[0] is None:
                near = self.bvh.find_nearest(Vector((x[i], -0.1, z[i])))
                pts[i] = near[0]
                nrm[i] = near[1]
            else:
                pts[i] = hit[0]
                nrm[i] = hit[1]
        return pts, nrm


def ambient_occlusion(surf, samples=48, distance=0.035, seed=1):
    """Per-vertex ambient occlusion of the surface (0 closed in, 1 open), by rays over the hemisphere on a BVH of the
    surface itself (the ears, when passed in `extra`, are part of it)."""
    tris, _o, _c = surf.tri_arrays()
    bvh = BVHTree.FromPolygons([tuple(map(float, v)) for v in surf.verts], [tuple(map(int, t)) for t in tris])
    n = len(surf.verts)
    normals = vertex_normals(surf)
    rng = np.random.default_rng(seed)
    dirs = rng.normal(size=(samples, 3))
    dirs /= np.linalg.norm(dirs, axis=1)[:, None]
    ao = np.ones(n)
    for i in range(n):
        nrm = normals[i]
        d = dirs * np.sign(dirs @ nrm)[:, None]
        # cosine-weighted: bend toward the normal
        d = d + nrm * 1.0
        d /= np.linalg.norm(d, axis=1)[:, None]
        origin = Vector(tuple(surf.verts[i] + nrm * 0.0008))
        hits = 0
        for k in range(samples):
            if bvh.ray_cast(origin, Vector(tuple(d[k])), distance)[0] is not None:
                hits += 1
        ao[i] = 1.0 - hits / samples
    return ao


def vertex_normals(surf):
    v = surf.verts
    n = np.zeros_like(v)
    for f in surf.faces:
        p = v[list(f)]
        nn = np.zeros(3)
        for k in range(len(p)):
            a, b = p[k], p[(k + 1) % len(p)]
            nn += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])])
        for i in f:
            n[i] += nn
    ln = np.linalg.norm(n, axis=1)[:, None]
    ln[ln < 1e-12] = 1.0
    return n / ln


def concat(a, b):
    """Two surfaces as one (for the occlusion bake: the ears shade the head and the head the ears)."""
    off = len(a.verts)
    weights = {k: np.concatenate([a.weights.get(k, np.zeros(len(a.verts))), b.weights.get(k, np.zeros(len(b.verts)))])
               for k in set(a.weights) | set(b.weights)}
    return Surface(np.concatenate([a.verts, b.verts]), a.faces + [tuple(i + off for i in f) for f in b.faces],
                   np.concatenate([a.corner_uv, b.corner_uv]), weights)


def brow_arrays(face, grid, side, chin_z):
    """A brow card on the skin: P (rows, cols, 3), u and v per vertex. Rows run from the inner end out along the ridge
    (arching over the eye, tapering to the tail), columns bottom to top across the hair; the points are on the skin
    and 1.2 mm above it."""
    rows, cols = 14, 4
    ax = np.linspace(1.15, 5.35, rows) * face.eye_spacing ** 0.5
    t = np.clip((ax - 0.9) / 3.4, 0.0, 1.0)
    arch = 13.0 + 0.45 * np.sin(t * math.pi * 0.62) - 0.8 * np.clip((ax - 3.6) / 1.8, 0.0, 1.0) ** 1.6
    thick = (0.75 + 0.30 * face.brow) * (1.0 - 0.5 * hs.smoothstep(1.5, 5.2, ax))
    P = np.zeros((rows, cols, 3))
    for j, c in enumerate(np.linspace(-0.5, 0.5, cols)):
        pts, nrm = grid.surface_at(side * ax * 0.01, (arch + c * thick) * 0.01 + chin_z)
        P[:, j] = pts + nrm * 0.0012
    u = np.repeat(np.linspace(0.0, 1.0, rows)[:, None], cols, axis=1)
    v = np.repeat(np.linspace(0.0, 1.0, cols)[None, :], rows, axis=0)
    return P, u, v
