"""Blender side of the enemies' heads (claude-docs/blender-pipeline.md, "Enemy heads, second pass"): turns the cage of
`headcage` (subdivided in `headmesh`), the bakes of `headbake` and `headtex` and the hair and headwear shells of
`headshape` into skinned objects on the body's own armature: the head and neck (one surface), two ears, two eyeballs
(a cornea dome on each), two brow cards, a hair cap or a beanie or a cap with its brim, their materials and textures.

Runs inside Blender 5.3 (bpy), called by build_thug.py and build_heavy.py. The pack's own head, eyes and brows are
not used: `build_head` replaces them. Everything is deterministic; nothing is downloaded and no model is run.
"""

import os

import bpy
import numpy as np

import costume as cs
import headbake as hb
import headcage as hc
import headmesh as hm
import headshape as hs
import headtex as ht
import looks
import pieces


def who_of(look):
    return look.id.capitalize()


def linear(rgb):
    """sRGB 0..1 to the linear values a material constant takes."""
    return tuple(float(c) ** 2.2 for c in rgb)


def colour_image(name, arr, path):
    img = cs.save_image(name, arr, path, non_color=False)
    img.colorspace_settings.name = "sRGB"
    return img


def data_image(name, arr, path):
    return cs.save_image(name, arr, path, non_color=True)


def override_image(look, suffix, name, search_dirs, non_color):
    """A hand-made image `<look id>_<suffix>.png` in one of the folders, loaded; None when there is none (the bake is
    used). The hook for textures made outside the repo."""
    for folder in search_dirs:
        path = os.path.join(folder, "{0}_{1}.png".format(look.id, suffix)) if folder else ""
        if path and os.path.isfile(path):
            img = bpy.data.images.load(path, check_existing=True)
            img.colorspace_settings.name = "Non-Color" if non_color else "sRGB"
            cs.log("override", path)
            return img
    return None


def make_mesh(name, verts, faces, uvs=None, smooth_angle=100.0):
    """A mesh object from arrays, smooth-shaded (cs.mark_sharp: every face smooth, edges sharper than smooth_angle
    split; None leaves it flat), with a UVMap when uvs (one per face corner, in face order) is given."""
    obj = cs.mesh_object(name, verts, faces)
    me = obj.data
    if uvs is not None:
        layer = me.uv_layers.new(name="UVMap")
        layer.data.foreach_set("uv", np.asarray(uvs, np.float32).ravel())
    me.update()
    if smooth_angle is not None:
        cs.mark_sharp(obj, smooth_angle)
    return obj


def vertex_uvs(obj, uv_per_vertex):
    """UVMap from a per-vertex (n, 2) array."""
    me = obj.data
    lv = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", lv)
    layer = me.uv_layers.get("UVMap") or me.uv_layers.new(name="UVMap")
    layer.data.foreach_set("uv", np.asarray(uv_per_vertex, np.float32)[lv].ravel())


def head_weights(body, co):
    """Bone weights for a point on the head or neck: the pack body's near the neck's foot, all Head from the chin up."""
    W = body.transfer(co)
    blend = cs.smoothstep(hs.CHIN_Z - 0.07, hs.CHIN_Z, co[:, 2])
    head = np.zeros_like(W)
    head[:, body.bone_index["Head"]] = 1.0
    return W * (1.0 - blend)[:, None] + head * blend[:, None]


def rigid_head(body, obj):
    if body is not None:        # (the preview has no body)
        cs.skin(obj, body, cs.rigid(body, len(obj.data.vertices), {"Head": 1.0}))
    return obj


def hands_material(body, look, out):
    """The hands (the only pack skin left): the pack's texture recoloured to the face's skin, its normal map."""
    who = who_of(look)
    root = body.root
    src = cs.pack_file(root, cs.TEXTURES + ("T_Superhero_Male_Dark.png",))
    img = cs.tinted_image(src, "T_{0}_Hands".format(who), look.face.skin_rgb(), os.path.join(out, "hands.png"),
                          keep_detail=0.7)
    skin_n = bpy.data.images.load(cs.pack_file(root, cs.GL_NORMALS + ("T_Superhero_Male_Normal.png",)),
                                  check_existing=True)
    skin_n.colorspace_settings.name = "Non-Color"
    return cs.material("M_{0}_Hands".format(who), (1.0, 1.0, 1.0), 0.55, normal=skin_n, base_image=img, glow=True)


class HeadParts:
    """What build_head made: the skinned objects, in order, and the materials."""

    def __init__(self):
        self.head = None
        self.ears = []
        self.eyes = None
        self.brows = None
        self.hair = None
        self.headwear = []
        self.grid = None
        self.mats = {}

    def all(self):
        return [o for o in [self.head] + self.ears + [self.eyes, self.brows, self.hair] + self.headwear
                if o is not None]


def head_materials(look, bake, out, search_dirs):
    """The materials; `bake` is a function returning (albedo, normal) arrays, called only when no override image."""
    face = look.face
    who = who_of(look)
    m = cs.material
    skin_img = override_image(look, "skin", "T_{0}_skin".format(who), search_dirs, non_color=False)
    skin_n = override_image(look, "skin_n", "T_{0}_skin_n".format(who), search_dirs, non_color=True)
    if skin_img is None or skin_n is None:
        albedo, normal = bake()
        skin_img = skin_img or colour_image("T_{0}_skin".format(who), albedo, os.path.join(out, "skin.png"))
        skin_n = skin_n or data_image("T_{0}_skin_n".format(who), normal, os.path.join(out, "skin_n.png"))
    eye_img = colour_image("T_{0}_eye".format(who), ht.eye_image(face), os.path.join(out, "eye.png"))
    brow_img = colour_image("T_{0}_brow".format(who), ht.brow_image(face), os.path.join(out, "brow.png"))
    hair_rgb = linear(face.hair_rgb())
    mats = {
        "skin": m("M_{0}_Skin".format(who), (1.0, 1.0, 1.0), 0.52, normal=skin_n, normal_strength=0.8,
                  base_image=skin_img, glow=True),
        "sclera": m("M_{0}_Sclera".format(who), (1.0, 1.0, 1.0), 0.32, base_image=eye_img, glow=True),
        # The cornea: wet. The engine's light and the dome's curvature make the highlight.
        "iris": m("M_{0}_Iris".format(who), (1.0, 1.0, 1.0), 0.05, base_image=eye_img, glow=True),
        "brow": m("M_{0}_Brows".format(who), hair_rgb, 0.7, base_image=brow_img, alpha_clip=True, glow=True),
    }
    if face.hair_style != "bald":
        albedo, normal = ht.hair_maps(face)
        hair_img = colour_image("T_{0}_hair".format(who), albedo, os.path.join(out, "hair.png"))
        hair_n = data_image("T_{0}_hair_n".format(who), normal, os.path.join(out, "hair_n.png"))
        mats["hair"] = m("M_{0}_Hair".format(who), hair_rgb, 0.62, base_image=hair_img, normal=hair_n,
                         normal_strength=0.6, alpha_clip=True, glow=True)
    if face.headwear != "none":
        weave = "rib" if face.headwear == "beanie" else "twill"
        normal = cs.weave_normal(weave, out, strength=2.0)
        mats["headwear"] = m("M_{0}_Headwear".format(who), linear(looks.CLOTH_COLOURS[face.headwear_colour]), 0.9,
                             normal=normal, normal_strength=0.7, glow=True)
    return mats


def ear_surface(face, data, grid, side):
    """The ear, subdivided: rooted on the skull's side at the ears' height, its UVs in the head's own texture."""
    z_c = hc.CHIN_Z + 0.5 * (hc.EAR_TOP_Z + hc.EAR_BOTTOM_Z) * 0.01
    y_a = grid.axis_y(z_c) + 0.004
    x_h = grid.ray_x(z_c, side, y_a)
    u_side, per_cm = data.ear_uv
    v0 = (0.5 * (hc.EAR_TOP_Z + hc.EAR_BOTTOM_Z) - hc.V_MIN) / (hc.APEX_Z - hc.V_MIN)
    anchor = (x_h, y_a, z_c)
    zs = z_c + (np.linspace(0.0, 1.0, hc.EAR_NU) - 0.52) * hc.EAR_LENGTH * face.ear_size * 0.01
    rows = [grid.ray_x(z, side, y_a) for z in zs]
    verts, faces, uv = hc.ear_cage(face, anchor, side, (u_side if side > 0 else 1.0 - u_side, v0), per_cm, rows)
    return hm.subdivide("Ear", verts, faces, uv, fix_normals=True)


def build_ear(body, surf, side, mats):
    ear = make_mesh("Ear" + ("L" if side > 0 else "R"), surf.verts, surf.faces, surf.corner_uv)
    cs.set_material(ear, mats["skin"])
    return rigid_head(body, ear)


def build_eyes(body, grid, mats):
    objs = []
    for centre, radius in grid.eyes():
        verts, faces, uv, iris = hs.eye_arrays(centre, radius)
        eye = make_mesh("Eye", verts, faces)
        cs.recalc_normals(eye)
        vertex_uvs(eye, uv)
        cs.set_material(eye, mats["sclera"])
        cs.set_material(eye, mats["iris"], iris)
        objs.append(eye)
    eyes = cs.join(objs, "Eyes")
    return rigid_head(body, eyes)


def build_brows(body, look, grid, mats):
    objs = []
    for side in (1, -1):
        P, u, v = hm.brow_arrays(look.face, grid, side, hc.CHIN_Z)
        rows, cols = P.shape[0], P.shape[1]
        faces = []
        for r in range(rows - 1):
            for c in range(cols - 1):
                quad = (r * cols + c, (r + 1) * cols + c, (r + 1) * cols + c + 1, r * cols + c + 1)
                faces.append(quad if side > 0 else tuple(reversed(quad)))
        brow = make_mesh("Brow", P.reshape(-1, 3), faces)
        vertex_uvs(brow, np.stack([u.ravel(), v.ravel()], axis=1))
        cs.set_material(brow, mats["brow"])
        objs.append(brow)
    return rigid_head(body, cs.join(objs, "Brows"))


def shell_object(name, P, apex, s, uv_rows=None):
    """A cap shell from cap_grid arrays: quads up from the edge row and a fan at the crown, UV u round the head and
    v = distance up from the edge."""
    rows, segs = P.shape[0], P.shape[1]
    faces = hs.cap_faces(rows, segs)
    verts = np.concatenate([P.reshape(-1, 3), apex[None, :]], axis=0)
    uvs = hs.grid_loop_uvs(faces, rows, segs, s if uv_rows is None else uv_rows)
    return make_mesh(name, verts, faces, uvs)


def build_hair(body, look, grid, mats):
    face = look.face
    style = face.hair_style
    if style == "bald":
        return None
    thick = hs.hair_thickness(style)
    if face.headwear != "none":
        thick = min(thick, 0.004)      # under a beanie or a cap: only the fringe at the nape and sideburns shows
    if style == "short":
        profile = lambda s: thick * (0.75 + 0.45 * s)   # noqa: E731
    else:
        profile = thick
    edge = hs.hairline_t(face, grid.theta)
    P, _N, apex, s = hs.cap_grid(grid, edge, profile, rows=16, feather=0.16)
    hair = shell_object("Hair", P, apex, s)
    cs.set_material(hair, mats["hair"])
    return rigid_head(body, hair)


def build_headwear(body, look, grid, mats):
    face = look.face
    if face.headwear == "none":
        return []
    out = []
    if face.headwear == "beanie":
        edge = hs.beanie_edge_t(grid.theta)
        # Knit with a rolled cuff round the edge; fits close to the skull.
        profile = lambda s: 0.0085 + 0.0040 * float(cs.smoothstep(0.22, 0.10, s))   # noqa: E731
        P, _N, apex, s = hs.cap_grid(grid, edge, profile, rows=14, feather=0.03)
        beanie = shell_object("Beanie", P, apex, s)
        cs.set_material(beanie, mats["headwear"])
        cs.box_uv(beanie, 0.03)
        out.append(rigid_head(body, beanie))
    else:
        edge = hs.cap_edge_t(grid.theta)
        P, N, apex, s = hs.cap_grid(grid, edge, lambda s: 0.011, rows=14, feather=0.03)
        crown = shell_object("CapCrown", P, apex, s)
        cs.set_material(crown, mats["headwear"])
        cs.box_uv(crown, 0.03)
        out.append(rigid_head(body, crown))
        bp, bn = hs.brim_arrays(grid, P, N, grid.theta)
        brim = cs.slab("CapBrim", bp, bn, 0.003, gap=0.0)
        cs.set_material(brim, mats["headwear"])
        cs.box_uv(brim, 0.03)
        cs.mark_sharp(brim, 70)
        out.append(rigid_head(body, brim))
    return out


def bake_skin(look, full):
    """(albedo, normal) of the skin from the head-and-ears surface `full`."""
    tris, _owner, corners = full.tri_arrays()
    ao = hm.ambient_occlusion(full)
    w = full.weights
    zero = np.zeros(len(full.verts))
    parts = {"verts": full.verts, "tris": tris, "tri_corners": corners, "corner_uv": full.corner_uv,
             "normals": hm.vertex_normals(full), "ao": ao, "lips": w.get("lips", zero), "inner": w.get("inner", zero),
             "lash": w.get("lash", zero), "ridge": w.get("ridge", zero)}
    return hb.skin_maps(look.face, parts, hc.CHIN_Z, hairline_t=hs.hairline_t)


def build_head(body, look, out, override_dir="", neck_y=None):
    """The whole head for a look: HeadParts. The caller drops the pack's eyes and brows (`drop_pack_face`) and keeps
    only the hands of the pack's skin."""
    problems = look.problems()
    if problems:
        raise SystemExit("looks: " + "; ".join(problems))
    face = look.face
    if neck_y is None:
        cy, _rx, _ry = pieces.neck_shape(body)
        neck_y = float(np.clip(cy + pieces.NECK_FORWARD, 0.0, 0.06))
    data = hc.build_cage(face, neck_y)
    surf = hm.subdivide("Head", data.verts, data.faces, data.uvs, data.creases, data.groups)
    grid = hm.SurfaceGrid(surf, face, data.eyes, neck_y)
    ear_surfs = [] if face.headwear == "beanie" else [ear_surface(face, data, grid, side) for side in (1, -1)]
    full = surf
    for e in ear_surfs:
        full = hm.concat(full, e)
    parts = HeadParts()
    parts.grid = grid
    parts.mats = head_materials(look, lambda: bake_skin(look, full), out, [override_dir, out])
    head = make_mesh("Head", surf.verts, surf.faces, surf.corner_uv)
    cs.set_material(head, parts.mats["skin"])
    if body is not None:
        cs.skin(head, body, head_weights(body, surf.verts))
    parts.head = head
    parts.ears = [build_ear(body, e, side, parts.mats) for e, side in zip(ear_surfs, (1, -1))]
    parts.eyes = build_eyes(body, grid, parts.mats)
    parts.brows = build_brows(body, look, grid, parts.mats)
    parts.hair = build_hair(body, look, grid, parts.mats)
    parts.headwear = build_headwear(body, look, grid, parts.mats)
    cs.log("head", look.id, len(surf.verts), "verts,", sum(cs.poly_count(o) for o in parts.all()), "tris")
    return parts


def drop_pack_face(body):
    """Remove the pack's own eyes and eyebrows (its face is painted on its skin texture; ours replaces it)."""
    for name in ("eyes", "brows"):
        obj = getattr(body, name)
        if obj is not None:
            cs.remove(obj)
            setattr(body, name, None)
