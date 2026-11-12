"""Shared helpers for the costume builds (claude-docs/blender-pipeline.md).

Runs inside Blender 5.3 (bpy, bmesh, mathutils, numpy), headless:

    blender.exe --background --factory-startup --python Tools/Blender/build_clint.py -- [--no-render]

The Quaternius packs under the assets root are data: their files are handed to Blender's glTF importer and their
textures are read as pixels, nothing in them is run. Everything here is deterministic (no random state without a
fixed seed), so a rebuild with the same inputs makes the same meshes.

The body is Universal Base Characters' Superhero_Male (the mesh import_bodies.py brings into Unreal). In Blender it
stands at the origin, Z up, facing -Y, in metres, T-posed with his left arm along +X. Garments are built around it:

    offset copy of the body (per-region distance, hung so cloth falls from the widest point instead of following
    every muscle) + extra solids (a coat skirt) -> voxel remesh (one smooth closed surface, concavities filled)
    -> smoothing -> folds -> openings cut on clean planes -> decimate -> trim cut on clean planes, raised
    -> thickness -> UVs -> weights from the body (nearest surface), with hand-written fixes where cloth should not
    follow the nearest bone (the coat skirt).

Weights are carried as dense numpy arrays (vertices x bones, in the armature's bone order) until skin() writes them
as vertex groups, so pieces built differently join into one skinned mesh on the body's own armature and bone names.
"""

import argparse
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

DEFAULT_ASSETS_ROOT = r"C:\Users\camer\code\hawkeye-assets"
PACK = ("quaternius-base-characters", "Universal Base Characters[Standard]")
BODY_GLTF = PACK + ("Base Characters", "Godot - UE", "Superhero_Male_FullBody.gltf")
BODY_FEMALE_GLTF = PACK + ("Base Characters", "Godot - UE", "Superhero_Female_FullBody.gltf")
TEXTURES = PACK + ("Base Characters", "Textures")
GL_NORMALS = TEXTURES + ("Normals Unity - Godot",)   # OpenGL (+Y) normals, Blender's and glTF's convention
HAIR_DIR = PACK + ("Hairstyles", "Rigged to Head Bone", "glTF (Godot -Unreal)")
HAIR_TEXTURES = PACK + ("Hairstyles", "Textures")

# Bone families on the body's skeleton (UE mannequin names).
FAMILIES = {
    "head": ("neck_01", "Head"),
    "torso": ("pelvis", "spine_01", "spine_02", "spine_03", "clavicle_l", "clavicle_r", "root"),
    "arm": ("upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r"),
    "leg": ("thigh_l", "thigh_r", "calf_l", "calf_r"),
    "foot": ("foot_l", "foot_r", "ball_l", "ball_r", "ball_leaf_l", "ball_leaf_r"),
}


def family_of(bone):
    for family, names in FAMILIES.items():
        if bone in names:
            return family
    return "hand"   # hand_l/r and every finger joint


# --------------------------------------------------------------------------------------------------
# paths and arguments
# --------------------------------------------------------------------------------------------------


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def assets_root(explicit=""):
    """--assets-root, HAWKEYE_ASSETS_ROOT, Tools\\local.json "assetsRoot", else the default: the first that exists."""
    candidates = [explicit, os.environ.get("HAWKEYE_ASSETS_ROOT", "")]
    local = os.path.join(repo_root(), "Tools", "local.json")
    if os.path.isfile(local):
        try:
            with open(local, encoding="utf-8") as f:
                candidates.append(json.load(f).get("assetsRoot", ""))
        except (OSError, ValueError):
            pass
    candidates.append(DEFAULT_ASSETS_ROOT)
    for candidate in candidates:
        if candidate and os.path.isdir(candidate):
            return candidate
    return explicit or DEFAULT_ASSETS_ROOT


def parse_args(who):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="build_" + who)
    parser.add_argument("--assets-root", default="")
    parser.add_argument("--out", default="", help="default <assets root>/built/" + who)
    parser.add_argument("--renders", default="", help="default Saved/Screenshots/Blender in the repo")
    parser.add_argument("--no-render", action="store_true")
    parser.add_argument("--no-export", action="store_true")
    parser.add_argument("--views", default="", help="comma list: front,34,side,back,idle,arms_up,crouch,lunge")
    args = parser.parse_args(argv)
    args.assets_root = assets_root(args.assets_root)
    args.out = args.out or os.path.join(args.assets_root, "built", who)
    args.renders = args.renders or os.path.join(repo_root(), "Saved", "Screenshots", "Blender")
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.renders, exist_ok=True)
    return args


def pack_file(root, parts):
    path = os.path.join(root, *parts)
    if not os.path.exists(path):
        raise SystemExit("costume: missing " + path)
    return path


def log(*parts):
    print("[costume]", *parts, flush=True)


# --------------------------------------------------------------------------------------------------
# mesh data as numpy
# --------------------------------------------------------------------------------------------------


def get_co(obj):
    me = obj.data
    a = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3).astype(np.float64)


def set_co(obj, co):
    obj.data.vertices.foreach_set("co", np.asarray(co, np.float32).ravel())
    obj.data.update()


def get_vnormals(obj):
    me = obj.data
    a = np.empty(len(me.vertices) * 3, np.float32)
    me.vertex_normals.foreach_get("vector", a)
    return a.reshape(-1, 3).astype(np.float64)


def face_data(obj):
    """(centres, normals) of the polygons, (F, 3) each."""
    me = obj.data
    n = len(me.polygons)
    c = np.empty(n * 3, np.float32)
    nr = np.empty(n * 3, np.float32)
    me.polygons.foreach_get("center", c)
    me.polygons.foreach_get("normal", nr)
    return c.reshape(-1, 3).astype(np.float64), nr.reshape(-1, 3).astype(np.float64)


def get_edges(obj):
    me = obj.data
    e = np.empty(len(me.edges) * 2, np.int64)
    me.edges.foreach_get("vertices", e)
    return e.reshape(-1, 2)


def smooth_values(values, edges, iters, factor=0.5, mask=None):
    """Laplacian smoothing of per-vertex values (positions or weight rows) over the mesh edges."""
    values = np.array(values, np.float64)
    n = len(values)
    deg = np.bincount(edges.ravel(), minlength=n).astype(np.float64)
    deg[deg == 0] = 1.0
    flat = values.reshape(n, -1)
    for i in range(iters):
        f = factor if not isinstance(factor, tuple) else factor[i % 2]
        s = np.zeros_like(flat)
        for col in range(flat.shape[1]):
            s[:, col] = (np.bincount(edges[:, 0], flat[edges[:, 1], col], n)
                         + np.bincount(edges[:, 1], flat[edges[:, 0], col], n))
        delta = f * (s / deg[:, None] - flat)
        if mask is not None:
            delta[~mask] = 0.0
        flat = flat + delta
    return flat.reshape(values.shape)


def taubin(obj, iters, mask=None):
    """Volume-keeping smoothing (alternating shrink and inflate steps)."""
    co = smooth_values(get_co(obj), get_edges(obj), iters * 2, factor=(0.5, -0.53), mask=mask)
    set_co(obj, co)


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# --------------------------------------------------------------------------------------------------
# objects
# --------------------------------------------------------------------------------------------------


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mesh_object(name, verts, faces):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], [tuple(int(i) for i in f) for f in faces])
    me.update()
    return link(bpy.data.objects.new(name, me))


def duplicate(obj, name):
    new = obj.copy()
    new.data = obj.data.copy()
    new.name = name
    new.data.name = name
    new.modifiers.clear()
    new.parent = None
    new.matrix_world = Matrix.Identity(4)
    return link(new)


def remove(obj):
    me = obj.data if obj.type == "MESH" else None
    bpy.data.objects.remove(obj, do_unlink=True)
    if me is not None and me.users == 0:
        bpy.data.meshes.remove(me)


def apply_modifiers(obj):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(obj.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    old = obj.data
    obj.modifiers.clear()
    obj.data = me
    me.name = obj.name
    if old.users == 0:
        bpy.data.meshes.remove(old)
    return obj


def join(objs, name):
    """One mesh object from several (geometry, materials, UVs and vertex groups merged by name)."""
    objs = [o for o in objs if o is not None]
    target = objs[0]
    with bpy.context.temp_override(active_object=target, object=target, selected_objects=objs,
                                   selected_editable_objects=objs):
        bpy.ops.object.join()
    target.name = name
    target.data.name = name
    return target


def voxel_remesh(obj, voxel):
    mod = obj.modifiers.new("remesh", "REMESH")
    mod.mode = "VOXEL"
    mod.voxel_size = voxel
    mod.adaptivity = 0.0
    return apply_modifiers(obj)


def decimate(obj, ratio=None, tris=None):
    """Collapse to ratio of the triangles, or to about `tris` triangles."""
    if tris is not None:
        ratio = min(1.0, tris / max(1, poly_count(obj)))
    mod = obj.modifiers.new("decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = ratio
    return apply_modifiers(obj)


def solidify(obj, thickness):
    """Thickness inward (the outer surface stays where it was built), rims closed. Returns the mask of the new
    inner-shell faces (so a lining can take another material)."""
    n_before = len(obj.data.polygons)
    mod = obj.modifiers.new("solidify", "SOLIDIFY")
    mod.thickness = thickness
    mod.offset = -1.0
    mod.use_rim = True
    mod.use_even_offset = False   # even offset spikes on slivers and steep walls
    mod.use_quality_normals = True
    apply_modifiers(obj)
    n = len(obj.data.polygons)
    shell = np.full(n, 2, np.int32)       # Solidify writes the original faces, then the inner shell, then the rims
    shell[:n_before] = 0
    shell[n_before:2 * n_before] = 1
    set_face_int(obj, "shell", shell)     # an attribute: it survives later extrusions, unlike face indices
    return shell == 1


def set_face_int(obj, name, values):
    attrs = obj.data.attributes
    attr = attrs.get(name) or attrs.new(name, "INT", "FACE")
    attr.data.foreach_set("value", np.asarray(values, np.int32))


def get_face_int(obj, name):
    attr = obj.data.attributes.get(name)
    out = np.zeros(len(obj.data.polygons), np.int32)
    if attr is not None:
        attr.data.foreach_get("value", out)
    return out


class Edit:
    """with Edit(obj) as bm: ... (written back on exit)."""

    def __init__(self, obj):
        self.obj = obj

    def __enter__(self):
        self.bm = bmesh.new()
        self.bm.from_mesh(self.obj.data)
        self.bm.verts.ensure_lookup_table()
        self.bm.faces.ensure_lookup_table()
        return self.bm

    def __exit__(self, *exc):
        self.bm.to_mesh(self.obj.data)
        self.bm.free()
        self.obj.data.update()
        return False


def delete_faces(obj, mask):
    mask = np.asarray(mask, bool)
    if not mask.any():
        return
    with Edit(obj) as bm:
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if mask[f.index]], context="FACES")


def delete_verts(obj, mask):
    mask = np.asarray(mask, bool)
    if not mask.any():
        return
    with Edit(obj) as bm:
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if mask[v.index]], context="VERTS")


def bisect(obj, co, no, face_mask=None):
    """Cut the mesh along a plane (only the faces in face_mask, when given), so a later region edge is a clean line.
    face_mask may be a function of (face centres, face normals), evaluated on the mesh as it is now."""
    if callable(face_mask):
        face_mask = face_mask(*face_data(obj))
    with Edit(obj) as bm:
        faces = [f for f in bm.faces if face_mask is None or face_mask[f.index]]
        if not faces:
            return
        edges = {e for f in faces for e in f.edges}
        verts = {v for f in faces for v in f.verts}
        bmesh.ops.bisect_plane(bm, geom=list(verts) + list(edges) + faces, plane_co=Vector(co), plane_no=Vector(no),
                               dist=1e-6)


def cut_and_delete(obj, co, no, side_mask_fn):
    """Bisect on the plane, then delete the faces whose centres side_mask_fn(centres, normals) selects."""
    bisect(obj, co, no)
    c, n = face_data(obj)
    delete_faces(obj, side_mask_fn(c, n))


def raise_faces(obj, mask, dist):
    """Extrude the masked faces outward along their region's normals: a trim panel or a piping ridge with real edges."""
    mask = np.asarray(mask, bool)
    if not mask.any():
        return
    with Edit(obj) as bm:
        faces = [f for f in bm.faces if mask[f.index]]
        normals = {}
        for f in faces:
            for v in f.verts:
                normals.setdefault(v.index, Vector()).__iadd__(f.normal)
        keyed = {tuple(round(x, 6) for x in bm.verts[i].co): n.normalized() for i, n in normals.items()}
        result = bmesh.ops.extrude_face_region(bm, geom=faces, use_keep_orig=False)
        for el in result["geom"]:
            if isinstance(el, bmesh.types.BMVert):
                n = keyed.get(tuple(round(x, 6) for x in el.co))
                if n is not None:
                    el.co += n * dist


def mark_sharp(obj, angle_deg=40.0):
    """Smooth shading, with edges sharper than angle_deg split (trim walls, soles, rims)."""
    limit = math.radians(angle_deg)
    with Edit(obj) as bm:
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if e.is_manifold:
                e.smooth = e.calc_face_angle(0.0) < limit


def recalc_normals(obj):
    with Edit(obj) as bm:
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def orient_outward(obj, axis_xy):
    """Point an open band's normals away from a vertical axis (recalc cannot tell inside from outside on a surface
    with no inside; Solidify and the lining depend on it)."""
    c, n = face_data(obj)
    radial = c[:, :2] - np.asarray(axis_xy)
    if (radial * n[:, :2]).sum() < 0:
        with Edit(obj) as bm:
            bmesh.ops.reverse_faces(bm, faces=bm.faces[:])


# --------------------------------------------------------------------------------------------------
# primitives
# --------------------------------------------------------------------------------------------------


def loft(name, rings, closed=True, cap_start=False, cap_end=False):
    """A surface through rings of points ((R, M, 3)); closed joins each ring's ends. Caps are fans to a centre."""
    rings = np.asarray(rings, np.float64)
    r, m = rings.shape[0], rings.shape[1]
    verts = list(rings.reshape(-1, 3))
    faces = []
    cols = m if closed else m - 1
    for i in range(r - 1):
        for j in range(cols):
            j1 = (j + 1) % m
            faces.append((i * m + j, i * m + j1, (i + 1) * m + j1, (i + 1) * m + j))
    if cap_start:
        verts.append(rings[0].mean(axis=0))
        c = len(verts) - 1
        faces += [(c, (j + 1) % m, j) for j in range(m)]
    if cap_end:
        verts.append(rings[-1].mean(axis=0))
        c = len(verts) - 1
        base = (r - 1) * m
        faces += [(c, base + j, base + (j + 1) % m) for j in range(m)]
    obj = mesh_object(name, np.array(verts), faces)
    recalc_normals(obj)
    return obj


def frames_along(points):
    """Tangent and two normals per point of a polyline (parallel transport)."""
    p = np.asarray(points, np.float64)
    t = np.gradient(p, axis=0)
    t /= np.linalg.norm(t, axis=1)[:, None]
    ref = np.array([0.0, 0.0, 1.0]) if abs(t[0][2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    n = np.cross(t[0], ref)
    n /= np.linalg.norm(n)
    ns, bs = [], []
    for i in range(len(p)):
        n = n - t[i] * np.dot(n, t[i])
        n /= np.linalg.norm(n)
        ns.append(n)
        bs.append(np.cross(t[i], n))
    return t, np.array(ns), np.array(bs)


def tube(name, points, radii, segments=12, cap=True, squash=1.0):
    p = np.asarray(points, np.float64)
    radii = np.broadcast_to(np.asarray(radii, np.float64), (len(p),))
    _t, n, b = frames_along(p)
    ang = np.linspace(0, 2 * math.pi, segments, endpoint=False)
    rings = [p[i] + radii[i] * (np.cos(ang)[:, None] * n[i] + squash * np.sin(ang)[:, None] * b[i])
             for i in range(len(p))]
    return loft(name, rings, closed=True, cap_start=cap, cap_end=cap)


def box(name, center, size, rotation=None):
    sx, sy, sz = (s / 2.0 for s in size)
    v = np.array([(x, y, z) for x in (-sx, sx) for y in (-sy, sy) for z in (-sz, sz)])
    if rotation is not None:
        v = v @ np.asarray(rotation).T
    v += np.asarray(center)
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    obj = mesh_object(name, v, faces)
    recalc_normals(obj)
    return obj


def slab(name, P, N, thickness, gap=0.0, closed=False):
    """A band of real thickness on a surface: P (rows, cols, 3) points on it, N their normals. Rows run along the
    band, cols across; closed joins the last row to the first (a belt)."""
    P = np.asarray(P, np.float64)
    N = np.asarray(N, np.float64)
    outer = P + N * (gap + thickness)
    inner = P + N * gap
    # A ring per row around the band's cross-section: outer left->right, then inner right->left.
    rings = np.concatenate([outer, inner[:, ::-1]], axis=1)
    obj = loft(name, rings if not closed else np.concatenate([rings, rings[:1]], axis=0), closed=True,
               cap_start=not closed, cap_end=not closed)
    if closed:
        with Edit(obj) as bm:
            bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-6)
    return obj


# --------------------------------------------------------------------------------------------------
# surfaces: projection onto a mesh
# --------------------------------------------------------------------------------------------------


class Surface:
    """BVH over a mesh object (world space) for rays and nearest points."""

    def __init__(self, obj):
        me = obj.data
        me.calc_loop_triangles()
        mw = obj.matrix_world
        self.verts = [mw @ v.co for v in me.vertices]
        self.tris = [tuple(t.vertices) for t in me.loop_triangles]
        self.bvh = BVHTree.FromPolygons(self.verts, self.tris)

    def hit_toward(self, origin, target, max_dist=2.0):
        """First hit of a ray from origin toward target: (point, normal) or (None, None)."""
        o = Vector(origin)
        d = (Vector(target) - o).normalized()
        loc, nrm, _i, _d = self.bvh.ray_cast(o, d, max_dist)
        if loc is None:
            return None, None
        if nrm.dot(d) > 0:
            nrm = -nrm
        return np.array(loc), np.array(nrm)

    def project_from_axis(self, axis_point, direction, reach=0.6):
        """Outermost surface point in a direction from an axis point (ray cast inward from outside)."""
        a = np.asarray(axis_point, np.float64)
        d = np.asarray(direction, np.float64)
        d = d / np.linalg.norm(d)
        return self.hit_toward(a + d * reach, a)

    def nearest(self, point):
        loc, nrm, _i, _d = self.bvh.find_nearest(Vector(point))
        return np.array(loc), np.array(nrm)


def band_around(surface, center_xy, z_rows, segments=64, push=0.0, reach=0.6):
    """Points and normals on a surface around a vertical axis: (rows, segments) grid, rows at z_rows. Keep reach
    short round a leg, or the rays find the other leg first."""
    P, N = [], []
    for z in z_rows:
        row_p, row_n = [], []
        for k in range(segments):
            a = 2 * math.pi * k / segments
            d = np.array([math.sin(a), -math.cos(a), 0.0])
            p, n = surface.project_from_axis((center_xy[0], center_xy[1], z), d, reach=reach)
            if p is None:
                p, n = np.array([center_xy[0], center_xy[1], z]) + d * 0.15, d
            row_p.append(p + d * push)
            row_n.append(n)
        P.append(row_p)
        N.append(row_n)
    return np.array(P), np.array(N)


# --------------------------------------------------------------------------------------------------
# the body
# --------------------------------------------------------------------------------------------------


class Body:
    """The imported body: armature, skin mesh, eyes, brows; a pristine copy to read weights and shape from."""

    # Heights the shared pieces measure the neck at (pieces.neck_shape): on the male the head and neck bones own the
    # skin at 1.545; a build on another body sets its own.
    neck_z = 1.545

    def __init__(self, root, gltf=BODY_GLTF):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        self.root = root
        bpy.ops.import_scene.gltf(filepath=pack_file(root, gltf))
        for o in list(bpy.data.objects):
            if o.name.startswith("Icosphere"):   # a stray node in the pack's glTF, outside its scene
                remove(o)
        self.armature = bpy.data.objects["Armature"]
        # SuperHero_Male, Superhero_Female: the skinned body is the mesh that is not the eyes or the brows.
        self.skin = next(o for o in bpy.data.objects if o.type == "MESH" and o.name.lower().startswith("superhero"))
        self.eyes = bpy.data.objects.get("Eyes")
        self.brows = bpy.data.objects.get("Eyebrows")
        self.bones = [b.name for b in self.armature.data.bones]
        self.bone_index = {n: i for i, n in enumerate(self.bones)}
        self.reference = duplicate(self.skin, "BodyReference")
        self.reference.hide_render = True
        self.reference.hide_set(True)
        self.co = get_co(self.reference)
        self.normals = get_vnormals(self.reference)
        self.W = self.weights_of(self.reference)
        self.surface = Surface(self.reference)
        self.tris = np.array(self.surface.tris)
        self.family = {f: self.W[:, [self.bone_index[b] for b in self.bones if family_of(b) == f]].sum(axis=1)
                       for f in ("head", "torso", "arm", "hand", "leg", "foot")}
        for o in (self.skin, self.eyes, self.brows):
            if o is not None:
                keep_first_uv(o)
        log("body", len(self.co), "verts,", len(self.bones), "bones")

    def bone_head(self, name):
        return np.array(self.armature.matrix_world @ self.armature.data.bones[name].head_local)

    def weights_of(self, obj):
        W = np.zeros((len(obj.data.vertices), len(self.bones)))
        names = {g.index: g.name for g in obj.vertex_groups}
        for v in obj.data.vertices:
            for g in v.groups:
                b = self.bone_index.get(names.get(g.group, ""))
                if b is not None:
                    W[v.index, b] = g.weight
        s = W.sum(axis=1)
        s[s == 0] = 1.0
        return W / s[:, None]

    def transfer(self, points):
        """Bone weights at points: the body's, interpolated at the nearest point of its surface. (N, bones)."""
        points = np.asarray(points, np.float64)
        locs = np.empty_like(points)
        tri_idx = np.empty(len(points), np.int64)
        bvh = self.surface.bvh
        for i, p in enumerate(points):
            loc, _n, ti, _d = bvh.find_nearest(Vector(p))
            locs[i] = loc
            tri_idx[i] = ti
        t = self.tris[tri_idx]
        a, b, c = self.co[t[:, 0]], self.co[t[:, 1]], self.co[t[:, 2]]
        v0, v1, v2 = b - a, c - a, locs - a
        d00 = (v0 * v0).sum(1)
        d01 = (v0 * v1).sum(1)
        d11 = (v1 * v1).sum(1)
        d20 = (v2 * v0).sum(1)
        d21 = (v2 * v1).sum(1)
        den = d00 * d11 - d01 * d01
        den[den == 0] = 1e-12
        v = (d11 * d20 - d01 * d21) / den
        w = (d00 * d21 - d01 * d20) / den
        u = 1.0 - v - w
        bary = np.clip(np.stack([u, v, w], axis=1), 0.0, 1.0)
        bary /= bary.sum(axis=1)[:, None]
        return (self.W[t] * bary[:, :, None]).sum(axis=1)

    def families_of(self, W):
        return {f: W[:, [self.bone_index[b] for b in self.bones if family_of(b) == f]].sum(axis=1)
                for f in ("head", "torso", "arm", "hand", "leg", "foot")}

    def offset_copy(self, name, offset):
        """The body pushed out along its normals by offset (metres, scalar or per vertex)."""
        obj = duplicate(self.reference, name)
        obj.hide_set(False)
        obj.hide_render = False
        obj.vertex_groups.clear()
        off = np.broadcast_to(np.asarray(offset, np.float64), (len(self.co),))
        set_co(obj, self.co + self.normals * off[:, None])
        return obj

    def keep_skin(self, keep_mask):
        """Delete the skin everywhere keep_mask is False (under the clothes)."""
        delete_verts(self.skin, ~np.asarray(keep_mask, bool))

    def add_hair(self, filename, material):
        """A hair mesh from the pack's 'Rigged to Head Bone' glTFs, moved onto this armature."""
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=pack_file(self.root, HAIR_DIR + (filename,)))
        new = [o for o in bpy.data.objects if o not in before]
        hair = next(o for o in new if o.type == "MESH" and not o.name.startswith("Icosphere"))
        for o in new:
            if o is not hair:
                bpy.data.objects.remove(o, do_unlink=True)
        hair.parent = self.armature
        hair.matrix_world = Matrix.Identity(4)
        hair.modifiers.clear()
        hair.modifiers.new("Armature", "ARMATURE").object = self.armature
        keep_first_uv(hair)
        hair.data.materials.clear()
        hair.data.materials.append(material)
        return hair


def keep_first_uv(obj):
    uvs = obj.data.uv_layers
    while len(uvs) > 1:
        uvs.remove(uvs[-1])
    if len(uvs) == 0:
        uvs.new(name="UVMap")
    uvs[0].name = "UVMap"


# --------------------------------------------------------------------------------------------------
# cloth shaping
# --------------------------------------------------------------------------------------------------


def hang(co, mask, axis_fn, z_top, z_bottom, bins=72, fill=1.0, taper=0.0, layer=0.005):
    """Let cloth fall from its widest point: going down from z_top, the distance from the axis (per direction) never
    drops below the widest seen above it (times fill, minus taper per metre). Fills the hollows under the chest,
    behind the knee, above the ankle, which is what turns an offset skin into a garment."""
    co = np.array(co, np.float64)
    sel = np.where(mask & (co[:, 2] <= z_top) & (co[:, 2] >= z_bottom))[0]
    if len(sel) == 0:
        return co
    z = co[sel, 2]
    centers = np.array([axis_fn(zz) for zz in z])
    d = co[sel, :2] - centers
    r = np.linalg.norm(d, axis=1)
    theta = np.arctan2(d[:, 1], d[:, 0])
    b = ((theta + math.pi) / (2 * math.pi) * bins).astype(int) % bins
    layer_of = ((z_top - z) / layer).astype(int)
    running = np.zeros(bins)
    new_r = r.copy()
    for L in range(layer_of.max() + 1):
        idx = np.where(layer_of == L)[0]
        running *= (1.0 - taper * layer)
        if len(idx) == 0:
            continue
        here = np.zeros(bins)
        np.maximum.at(here, b[idx], r[idx])
        # Neighbouring directions share their width a little, so the fill has no stripes.
        here = np.maximum(here, 0.97 * np.maximum(np.roll(here, 1), np.roll(here, -1)))
        running = np.maximum(running, here)
        new_r[idx] = np.maximum(r[idx], running[b[idx]] * fill)
    scale = np.where(r > 1e-6, new_r / np.maximum(r, 1e-6), 1.0)
    co[sel, :2] = centers + d * scale[:, None]
    return co


def displace(obj, fn):
    """Move each vertex along its normal by fn(co, normals) metres (folds, ribs, quilting)."""
    co = get_co(obj)
    n = get_vnormals(obj)
    set_co(obj, co + n * fn(co, n)[:, None])


def fold_noise(x, seed, octaves=3):
    """A smooth deterministic 1D signal in about [-1, 1] (sum of sines with fixed phases)."""
    rng = np.random.default_rng(seed)
    out = np.zeros_like(np.asarray(x, np.float64))
    amp = 1.0
    for o in range(octaves):
        f = (o + 1) * 1.7
        out += amp * np.sin(np.asarray(x) * f + rng.uniform(0, 2 * math.pi))
        amp *= 0.5
    return out / 1.75


# --------------------------------------------------------------------------------------------------
# materials and generated textures
# --------------------------------------------------------------------------------------------------


def _node_tree(idblock):
    if getattr(idblock, "node_tree", None) is None:
        idblock.use_nodes = True
    return idblock.node_tree


def save_image(name, rgb, path, non_color=True):
    """A PNG from an (H, W, 3) array in 0..1, saved to path and loaded as a Blender image (packed into the glb)."""
    h, w = rgb.shape[0], rgb.shape[1]
    img = bpy.data.images.get(name) or bpy.data.images.new(name, w, h, alpha=rgb.shape[2] == 4)
    if tuple(img.size) != (w, h):
        img.scale(w, h)
    rgba = np.clip(rgb, 0, 1)
    if rgb.shape[2] == 3:
        rgba = np.concatenate([rgba, np.ones((h, w, 1))], axis=2)
    rgba = rgba.astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    if non_color:
        img.colorspace_settings.name = "Non-Color"
    return img


def normal_from_height(h, strength):
    gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    gy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    n = np.stack([-gx, -gy, np.ones_like(h)], axis=2)
    n /= np.linalg.norm(n, axis=2)[:, :, None]
    return n * 0.5 + 0.5


def tileable_noise(size, seed, blur):
    rng = np.random.default_rng(seed)
    white = rng.standard_normal((size, size))
    f = np.fft.fftfreq(size)
    g = np.exp(-(f[:, None] ** 2 + f[None, :] ** 2) * (blur ** 2) * 20.0)
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * g))
    return (out - out.mean()) / (out.std() + 1e-9)


def weave_height(kind, size=256):
    """A tileable height field: twill (coat, trousers), knit rib (cuffs, hems), tricot (tracksuit), grain (leather),
    quilt (not used: quilting is geometry)."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    if kind == "twill":
        threads = 32
        u, v = x / size * threads, y / size * threads
        diag = np.sin(2 * math.pi * (u + v) / 2.0) * 0.5 + 0.5
        warp = np.abs(np.sin(math.pi * u)) ** 0.5
        h = 0.7 * diag + 0.3 * warp + 0.15 * tileable_noise(size, 1, 2.0)
    elif kind == "rib":
        ribs = 24
        h = np.abs(np.sin(math.pi * x / size * ribs)) ** 0.6 + 0.08 * tileable_noise(size, 2, 1.0)
    elif kind == "tricot":
        loops = 48
        h = (np.sin(2 * math.pi * x / size * loops) * np.sin(math.pi * y / size * loops)) ** 2
        h = 0.5 * h + 0.25 * tileable_noise(size, 3, 1.5)
    elif kind == "grain":
        h = tileable_noise(size, 4, 3.0) + 0.5 * tileable_noise(size, 5, 1.0)
    elif kind == "knit":
        loops = 32
        h = np.abs(np.sin(math.pi * x / size * loops)) * (0.6 + 0.4 * np.abs(np.sin(2 * math.pi * y / size * loops)))
        h += 0.1 * tileable_noise(size, 6, 1.0)
    else:
        h = np.zeros((size, size))
    return h


_NORMALS = {}


def weave_normal(kind, out_dir, strength=2.0):
    """The generated normal map for a kind of cloth, saved as T_N_<kind>.png in out_dir. One per kind per run."""
    if kind not in _NORMALS:
        rgb = normal_from_height(weave_height(kind), strength)
        _NORMALS[kind] = save_image("T_N_" + kind, rgb, os.path.join(out_dir, "T_N_{0}.png".format(kind)))
    return _NORMALS[kind]


def material(name, color, roughness, metallic=0.0, normal=None, normal_strength=1.0, base_image=None,
             alpha_clip=False, glow=False):
    """A Principled BSDF with constants (and an optional normal map, base colour image): what glTF carries.
    alpha_clip: the image's alpha through a Round node, which the glTF exporter writes as alphaMode MASK at 0.5.
    glow: the engine's thug telegraph lights this slot (the head, as on the painted body)."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    tree = _node_tree(mat)
    nodes, links = tree.nodes, tree.links
    for n in list(nodes):
        nodes.remove(n)
    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    bsdf.inputs["Base Color"].default_value = (color[0], color[1], color[2], 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if base_image is not None:
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = base_image
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        if alpha_clip:
            clip = nodes.new("ShaderNodeMath")
            clip.operation = "ROUND"
            links.new(tex.outputs["Alpha"], clip.inputs[0])
            links.new(clip.outputs[0], bsdf.inputs["Alpha"])
    if normal is not None:
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = normal
        nm = nodes.new("ShaderNodeNormalMap")
        nm.inputs["Strength"].default_value = normal_strength
        links.new(tex.outputs["Color"], nm.inputs["Color"])
        links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    mat["hawkeye"] = json.dumps({"color": list(color) if base_image is None else [1.0, 1.0, 1.0],
                                 "roughness": roughness, "metallic": metallic, "normal_strength": normal_strength,
                                 "normal": normal.name if normal is not None else "",
                                 "image": base_image.name if base_image is not None else "",
                                 "masked": bool(alpha_clip), "glow": bool(glow)})
    return mat


def _texture_ref(image_name, out_dir):
    """Where the engine finds an image a slot uses: {"file", "from": "built" (next to the glb) or "pack" (the
    Quaternius Textures folder)}. Pack normals are named by the file in Textures (the engine's DirectX ones)."""
    if not image_name:
        return None
    img = bpy.data.images.get(image_name)
    path = bpy.path.abspath(img.filepath_raw or img.filepath) if img is not None else ""
    built = bool(path) and os.path.normcase(os.path.dirname(os.path.abspath(path))) == os.path.normcase(
        os.path.abspath(out_dir))
    return {"file": os.path.basename(path) or image_name, "from": "built" if built else "pack"}


def tinted_image(src_path, name, tint, out_path, keep_detail=1.0):
    """The pack's texture recoloured: luminance (contrast kept by keep_detail) times tint, saved to out_path."""
    src = bpy.data.images.load(src_path, check_existing=True)
    w, h = src.size
    px = np.empty(w * h * 4, np.float32)
    src.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    lum = px[:, :, :3] @ np.array([0.2126, 0.7152, 0.0722])
    lum = lum / max(float(lum.mean()), 1e-4)
    lum = 1.0 + (lum - 1.0) * keep_detail
    rgb = np.clip(lum[:, :, None] * np.asarray(tint)[None, None, :], 0.0, 1.0)
    rgba = np.concatenate([rgb, px[:, :, 3:4]], axis=2)   # the strands' alpha, for clipping
    img = save_image(name, rgba, out_path, non_color=False)
    img.colorspace_settings.name = "sRGB"
    return img


def ensure_slot(obj, mat):
    mats = obj.data.materials
    for i, m in enumerate(mats):
        if m is not None and m.name == mat.name:
            return i
    mats.append(mat)
    return len(mats) - 1


def set_material(obj, mat, mask=None):
    idx = ensure_slot(obj, mat)
    me = obj.data
    mi = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get("material_index", mi)
    if mask is None:
        mi[:] = idx
    else:
        mi[np.asarray(mask, bool)] = idx
    me.polygons.foreach_set("material_index", mi)
    me.update()


def material_mask(obj, mat):
    me = obj.data
    mi = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get("material_index", mi)
    idx = [i for i, m in enumerate(me.materials) if m is not None and m.name == mat.name]
    return np.isin(mi, idx)


def box_uv(obj, tile):
    """Box-projected UVs, tile metres per UV unit (the tiling cloth normal maps read the same scale everywhere)."""
    me = obj.data
    keep_first_uv(obj)
    nl = len(me.loops)
    lv = np.empty(nl, np.int64)
    me.loops.foreach_get("vertex_index", lv)
    starts = np.empty(len(me.polygons), np.int64)
    totals = np.empty(len(me.polygons), np.int64)
    me.polygons.foreach_get("loop_start", starts)
    me.polygons.foreach_get("loop_total", totals)
    poly_of_loop = np.repeat(np.arange(len(me.polygons)), totals)
    _c, n = face_data(obj)
    co = get_co(obj)[lv]
    axis = np.abs(n).argmax(axis=1)[poly_of_loop]
    uv = np.where(axis[:, None] == 0, co[:, [1, 2]], np.where(axis[:, None] == 1, co[:, [0, 2]], co[:, [0, 1]]))
    me.uv_layers[0].data.foreach_set("uv", (uv / tile).astype(np.float32).ravel())


# --------------------------------------------------------------------------------------------------
# weights and the final mesh
# --------------------------------------------------------------------------------------------------


def skin(obj, body, W, max_influences=4):
    """Write W (verts x bones) as vertex groups (strongest max_influences, normalised) and bind to the armature."""
    W = np.array(W, np.float64)
    if max_influences < W.shape[1]:
        cut = np.partition(W, -max_influences, axis=1)[:, -max_influences][:, None]
        W = np.where(W >= cut, W, 0.0)
    s = W.sum(axis=1)
    s[s == 0] = 1.0
    W /= s[:, None]
    obj.vertex_groups.clear()
    for b, name in enumerate(body.bones):
        rows = np.nonzero(W[:, b] > 1e-4)[0]
        if len(rows) == 0:
            continue
        vg = obj.vertex_groups.new(name=name)
        for i in rows:
            vg.add([int(i)], float(W[i, b]), "REPLACE")
    obj.parent = body.armature
    obj.matrix_parent_inverse = Matrix.Identity(4)
    obj.modifiers.clear()
    obj.modifiers.new("Armature", "ARMATURE").object = body.armature
    return obj


def reassign(body, W, from_family, to_bone, mask=None):
    """Move the weight a family of bones has (where mask is true) onto one bone: a vest hem the thighs should not
    drag, say."""
    W = np.array(W, np.float64)
    cols = [body.bone_index[b] for b in body.bones if family_of(b) == from_family]
    rows = np.ones(len(W), bool) if mask is None else np.asarray(mask, bool)
    moved = W[np.ix_(rows, cols)].sum(axis=1)
    W[np.ix_(rows, cols)] = 0.0
    W[rows, body.bone_index[to_bone]] += moved
    return W


def rigid(body, n, bones):
    """Weights for n verts bound to bones {name: weight}."""
    W = np.zeros((n, len(body.bones)))
    for name, w in bones.items():
        W[:, body.bone_index[name]] = w
    return W


def finish_piece(obj, body, W, tile=0.05, sharp=40.0):
    """UVs, shading and skin for one built piece."""
    if len(obj.data.uv_layers) == 0 or tile:
        box_uv(obj, tile or 0.05)
    mark_sharp(obj, sharp)
    return skin(obj, body, W)


def poly_count(obj):
    me = obj.data
    me.calc_loop_triangles()
    return len(me.loop_triangles)


# --------------------------------------------------------------------------------------------------
# export
# --------------------------------------------------------------------------------------------------


def finish(body, parts, who, args, extra_renders=None):
    """Join the pieces into SK_<Who>, render, export, verify the export. extra_renders(body, mesh) runs after the
    render set (the studio is body.studio), before the export."""
    for p in parts:
        log("piece", p.name, poly_count(p), "tris")
    mesh = join(parts, "SK_" + who.capitalize())
    drop_unused_slots(mesh)
    log("total", poly_count(mesh), "tris,", len(mesh.data.vertices), "verts,", unweighted(mesh), "unweighted,",
        len(mesh.data.materials), "materials")
    if not args.no_render:
        render_set(body, who, args.renders, args.views)
        if extra_renders is not None:
            extra_renders(body, mesh)
    if not args.no_export:
        bones = list(body.bones)
        path = export(body, mesh, args.out, who)
        if not verify_export(path, bones):
            raise SystemExit(1)


def drop_unused_slots(obj):
    """Remove material slots no face uses (each would be an empty section in the engine)."""
    me = obj.data
    mi = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get("material_index", mi)
    used = sorted(set(mi.tolist()))
    keep = [me.materials[i] for i in used]
    remap = np.zeros(max(len(me.materials), 1), np.int32)
    for new, old in enumerate(used):
        remap[old] = new
    me.materials.clear()
    for m in keep:
        me.materials.append(m)
    me.polygons.foreach_set("material_index", remap[mi])
    me.update()


def unweighted(obj):
    """Vertices with no bone weight at all (they would hang off the root in the engine)."""
    return sum(1 for v in obj.data.vertices if not any(g.weight > 0 for g in v.groups))


def verify_export(path, bones):
    """Re-import the glb into an empty scene and check what the engine will see: one armature with the body's bones
    in the same names, one skinned mesh, every vertex weighted. Destroys the scene; run last."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    # The importer adds an Icosphere as its bone display shape; it is not in the file's scene.
    meshes = [o for o in bpy.data.objects if o.type == "MESH" and not o.name.startswith("Icosphere")]
    problems = []
    if len(arms) != 1:
        problems.append("{0} armatures".format(len(arms)))
    elif {b.name for b in arms[0].data.bones} != set(bones):
        got = {b.name for b in arms[0].data.bones}
        problems.append("bones differ: missing {0}, extra {1}".format(sorted(set(bones) - got),
                                                                     sorted(got - set(bones))))
    if len(meshes) != 1:
        problems.append("{0} meshes".format(len(meshes)))
    else:
        loose = unweighted(meshes[0])
        if loose:
            problems.append("{0} unweighted verts".format(loose))
        log("verify", path, len(meshes[0].data.vertices), "verts,", poly_count(meshes[0]), "tris,",
            len(meshes[0].data.materials), "materials")
    log("verify", "OK" if not problems else "PROBLEMS: " + "; ".join(problems))
    return not problems


def export(body, mesh_obj, out_dir, who):
    """SK_<Who>.glb (armature + one skinned mesh), <who>.blend and <who>.slots.json (material slot -> PBR values,
    which import_bodies.py turns into material instances) in out_dir."""
    body.armature.data.pose_position = "REST"
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    body.armature.select_set(True)
    mesh_obj.select_set(True)
    bpy.context.view_layer.objects.active = body.armature
    name = "SK_" + who.capitalize()
    path = os.path.join(out_dir, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_skins=True,
                              export_animations=False, export_morph=False, export_apply=False, export_yup=True,
                              export_materials="EXPORT", export_extras=False)
    slots = {}
    for m in mesh_obj.data.materials:
        if m is not None:
            spec = json.loads(m.get("hawkeye", "{}"))
            spec["image"] = _texture_ref(spec.get("image", ""), out_dir)
            spec["normal"] = _texture_ref(spec.get("normal", ""), out_dir)
            slots[m.name] = spec
    with open(os.path.join(out_dir, who + ".slots.json"), "w", encoding="utf-8") as f:
        json.dump({"mesh": name, "slots": slots, "triangles": poly_count(mesh_obj),
                   "bones": len(body.bones)}, f, indent=2, sort_keys=True)
    bpy.context.preferences.filepaths.save_version = 0   # no .blend1 beside it
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, who + ".blend"), compress=True)
    log("exported", path, poly_count(mesh_obj), "triangles,", len(mesh_obj.data.materials), "materials")
    stale = os.path.join(out_dir, who + ".blend1")
    if os.path.isfile(stale):
        os.remove(stale)
    return path


# --------------------------------------------------------------------------------------------------
# renders
# --------------------------------------------------------------------------------------------------

# Poses for the deformation check: (op, bone, value) applied in order, rotations in degrees about armature axes
# (X his left, -Y his front, Z up) through the bone's head.
POSES = {
    "idle": [("rot", "upperarm_l", "Y", 72), ("rot", "upperarm_r", "Y", -72),
             ("rot", "lowerarm_l", "X", -15), ("rot", "lowerarm_r", "X", -15)],
    "arms_up": [("rot", "upperarm_l", "Y", -65), ("rot", "upperarm_r", "Y", 65),
                ("rot", "lowerarm_l", "Y", -15), ("rot", "lowerarm_r", "Y", 15)],
    "crouch": [("move", "pelvis", (0.0, 0.10, -0.42)),
               ("rot", "spine_01", "X", 28), ("rot", "neck_01", "X", -20),
               ("rot", "thigh_l", "X", -105), ("rot", "thigh_r", "X", -105),
               ("rot", "thigh_l", "Y", 12), ("rot", "thigh_r", "Y", -12),
               ("rot", "calf_l", "X", 135), ("rot", "calf_r", "X", 135),
               ("rot", "foot_l", "X", -30), ("rot", "foot_r", "X", -30),
               ("rot", "upperarm_l", "Y", 55), ("rot", "upperarm_r", "Y", -55),
               ("rot", "upperarm_l", "Z", -40), ("rot", "upperarm_r", "Z", 40)],
    "lunge": [("move", "pelvis", (0.0, -0.08, -0.16)),
              ("rot", "spine_01", "Z", 25), ("rot", "spine_01", "X", 8),
              ("rot", "thigh_r", "X", -70), ("rot", "calf_r", "X", 75),
              ("rot", "thigh_l", "X", 35), ("rot", "calf_l", "X", 25), ("rot", "foot_l", "X", -40),
              ("rot", "upperarm_r", "Z", 88), ("rot", "upperarm_r", "X", 8),
              ("rot", "upperarm_l", "Y", 60), ("rot", "upperarm_l", "Z", -35),
              ("rot", "lowerarm_l", "Z", -110)],
    # Full draw, side-on to a target on her left (+X): the bow arm straight out, the draw hand at the jaw with the
    # elbow up and back, the head turned to the target.
    "draw": [("rot", "neck_01", "Z", 70), ("rot", "upperarm_l", "Z", 8),
             ("rot", "upperarm_r", "Z", -18), ("rot", "upperarm_r", "Y", 8),
             ("rot", "lowerarm_r", "Z", 165), ("rot", "lowerarm_r", "Y", -14),
             ("rot", "hand_r", "Z", 20)],
}

TURNAROUND = {"front": 0.0, "34": 40.0, "side": 90.0, "back": 180.0}
POSE_ANGLE = {"idle": 30.0, "arms_up": 25.0, "crouch": 50.0, "lunge": 60.0, "draw": 20.0}
# Close-ups for checking details (not in the default set): (angle, target, ortho scale).
CLOSEUPS = {"head_front": (20.0, (0, 0, 1.52), 0.55), "head_back": (200.0, (0, 0, 1.52), 0.55),
            "feet": (35.0, (0, 0, 0.22), 0.7), "hand": (60.0, (0.55, 0, 1.45), 0.5)}


def clear_pose(arm):
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def apply_pose(arm, name):
    clear_pose(arm)
    arm.data.pose_position = "POSE"
    axes = {"X": Vector((1, 0, 0)), "Y": Vector((0, 1, 0)), "Z": Vector((0, 0, 1))}
    for step in POSES[name]:
        pb = arm.pose.bones[step[1]]
        m = pb.matrix.copy()
        if step[0] == "move":
            pb.matrix = Matrix.Translation(Vector(step[2])) @ m
        else:
            head = m.translation.copy()
            r = Matrix.Rotation(math.radians(step[3]), 4, axes[step[2]])
            pb.matrix = Matrix.Translation(head) @ r @ Matrix.Translation(-head) @ m
        bpy.context.view_layer.update()


class Studio:
    """Eevee, a grey cyclorama, three lights that turn with the camera, orthographic for turnarounds."""

    def __init__(self, width=1000, height=1000, samples=32):
        sc = bpy.context.scene
        sc.render.engine = "BLENDER_EEVEE"
        sc.render.resolution_x, sc.render.resolution_y = width, height
        sc.render.film_transparent = False
        try:
            sc.eevee.taa_render_samples = samples
            sc.eevee.use_raytracing = True
        except AttributeError:
            pass
        sc.view_settings.view_transform = "AgX"
        world = bpy.data.worlds.new("Studio")
        tree = _node_tree(world)
        bg = tree.nodes.get("Background")
        bg.inputs[0].default_value = (0.32, 0.33, 0.35, 1.0)
        bg.inputs[1].default_value = 0.3
        sc.world = world
        floor = mesh_object("Floor", [(-4, -4, 0), (4, -4, 0), (4, 4, 0), (-4, 4, 0)], [(0, 1, 2, 3)])
        floor.data.materials.append(material("Floor", (0.35, 0.35, 0.36), 0.8))
        self.rig = link(bpy.data.objects.new("Rig", None))
        cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
        link(cam)
        cam.parent = self.rig
        sc.camera = cam
        self.cam = cam
        for name, kind, energy, loc, size in (("Key", "AREA", 700.0, (-2.2, -3.2, 3.2), 2.5),
                                              ("Fill", "AREA", 180.0, (3.0, -2.5, 1.6), 3.0),
                                              ("Rim", "AREA", 600.0, (1.0, 3.5, 3.0), 2.0)):
            light = bpy.data.lights.new(name, kind)
            light.energy = energy
            light.size = size
            obj = link(bpy.data.objects.new(name, light))
            obj.location = loc
            obj.parent = self.rig
            direction = Vector((0, 0, 1.0)) - Vector(loc)
            obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

    def shoot(self, path, angle_deg, ortho=True, target=(0.0, 0.0, 0.92), scale=2.1, lens=50.0, distance=6.0):
        self.rig.rotation_euler = (0.0, 0.0, math.radians(angle_deg))
        self.rig.location = (target[0], target[1], 0.0)
        cam = self.cam
        cam.data.type = "ORTHO" if ortho else "PERSP"
        cam.data.ortho_scale = scale
        cam.data.lens = lens
        cam.location = (0.0, -distance, target[2])
        cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)
        bpy.context.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        log("render", path)


def render_set(body, who, out_dir, views=""):
    """Turnaround at rest (front, 3/4, side, back) and the test poses, <who>_<view>.png in out_dir."""
    wanted = [v for v in views.split(",") if v] or list(TURNAROUND) + list(POSES)
    body.reference.hide_render = True
    studio = Studio()
    body.studio = studio   # for a build's own extra shots after the set
    arm = body.armature
    paths = []
    for view in wanted:
        path = os.path.join(out_dir, "{0}_{1}.png".format(who, view))
        if view in TURNAROUND:
            clear_pose(arm)
            arm.data.pose_position = "REST"
            studio.shoot(path, TURNAROUND[view])
        elif view in CLOSEUPS:
            clear_pose(arm)
            arm.data.pose_position = "REST"
            angle, target, scale = CLOSEUPS[view]
            studio.shoot(path, angle, target=target, scale=scale)
        elif view in POSES:
            apply_pose(arm, view)
            if view == "idle":
                studio.shoot(path, POSE_ANGLE[view], ortho=True, scale=2.0, target=(0, 0, 0.95))
            elif view in ("arms_up", "draw"):
                studio.shoot(path, POSE_ANGLE[view], ortho=True, scale=2.4, target=(0, 0, 1.1))
            else:
                studio.shoot(path, POSE_ANGLE[view], ortho=True, scale=2.0, target=(0, 0, 0.75))
        paths.append(path)
    clear_pose(arm)
    arm.data.pose_position = "REST"
    return paths
