"""A fast look at the enemies' heads without the body: builds the head from its cage (headcage, headmesh), puts eyes in
it, and renders it from the front, three quarters, the side and the eyes under the build's studio light, a harsh top
light and a low side light (the engine's light showed ridges the studio's did not). About 10 s a head.

    blender --background --factory-startup --python Tools/Blender/head_preview.py -- --look thug --out Saved/Screenshots/Heads

Flags: --look (default thug), --out, --views (face_front, face_34, face_side, eyes, face_back), --lights (std, top,
side, all), --textured (use the bake: headbake), --wire (draw the cage's subdivided wire).
"""

import math
import os
import sys

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import headcage as hc  # noqa: E402
import headmesh as hm  # noqa: E402
import headshape as hs  # noqa: E402
import looks  # noqa: E402


def parse():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opts = {"look": "thug", "out": os.path.join(HERE, "..", "..", "Saved", "Screenshots", "Heads"),
            "views": "face_front,face_34,face_side", "lights": "std", "textured": False, "wire": False,
            "tag": ""}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--textured", "--wire"):
            opts[a[2:]] = True
        else:
            opts[a[2:]] = argv[i + 1]
            i += 1
        i += 1
    return opts


def material(name, rgb, rough):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    return mat


def mesh_obj(name, verts, faces, mat, corner_uv=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], [tuple(f) for f in faces])
    me.update()
    if corner_uv is not None:
        layer = me.uv_layers.new(name="UVMap")
        layer.data.foreach_set("uv", np.asarray(corner_uv, np.float32).ravel())
    for p in me.polygons:
        p.use_smooth = True
    me.materials.append(mat)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


LIGHTS = {
    "std": [("AREA", 260.0, (-0.9, -1.1, 1.9), 1.2), ("AREA", 50.0, (1.2, -1.0, 1.7), 1.5)],
    "top": [("SUN", 5.0, (0.0, -0.3, 3.0), 0.0)],
    "side": [("AREA", 220.0, (1.1, -0.35, 1.45), 0.6), ("AREA", 10.0, (-1.0, -1.0, 1.9), 1.5)],
}


def setup_scene(width=1000):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = sc.render.resolution_y = width
    try:
        sc.eevee.taa_render_samples = 32
        sc.eevee.use_raytracing = True
    except AttributeError:
        pass
    sc.view_settings.view_transform = "AgX"
    world = bpy.data.worlds.new("W")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.30, 0.31, 0.33, 1.0)
    bg.inputs[1].default_value = 0.25
    sc.world = world
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    bpy.context.scene.collection.objects.link(cam)
    sc.camera = cam
    return cam


def place_lights(kind):
    for o in [o for o in bpy.data.objects if o.name.startswith("Light")]:
        bpy.data.objects.remove(o)
    from mathutils import Vector
    for i, (ltype, energy, loc, size) in enumerate(LIGHTS[kind]):
        light = bpy.data.lights.new("Light%d" % i, ltype)
        light.energy = energy
        if ltype == "AREA":
            light.size = size
        obj = bpy.data.objects.new("Light%d" % i, light)
        bpy.context.scene.collection.objects.link(obj)
        obj.location = loc
        aim = Vector((0.0, -0.05, 1.66)) - Vector(loc)
        obj.rotation_euler = aim.to_track_quat("-Z", "Y").to_euler()


VIEWS = {"face_front": (0.0, 1.655, 0.78), "face_34": (38.0, 1.655, 0.78), "face_side": (90.0, 1.655, 0.78),
         "face_back": (180.0, 1.655, 0.78), "eyes": (0.0, 1.69, 0.42), "face_up": (0.0, 1.62, 0.78),
         "nose": (28.0, 1.63, 0.42), "mouth": (20.0, 1.60, 0.42), "nose_front": (0.0, 1.63, 0.42), "ear": (90.0, 1.65, 0.36)}


def shoot(cam, path, view):
    angle, zc, dist = VIEWS[view]
    a = math.radians(angle)
    target = (0.0, -0.01, zc)
    cam.data.lens = 85.0
    cam.data.type = "PERSP"
    cam.location = (dist * math.sin(a), -dist * math.cos(a) + target[1], zc + (0.06 if view == "face_up" else 0.0))
    from mathutils import Vector
    aim = Vector(target) - cam.location
    cam.rotation_euler = aim.to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def build(look, out_dir, textured=False):
    if textured:
        import headbuild
        parts = headbuild.build_head(None, look, out_dir, neck_y=0.03)
        return parts
    face = look.face
    data = hc.build_cage(face)
    surf = hm.subdivide("Head", data.verts, data.faces, data.uvs, data.creases, data.groups)
    skin = material("Skin", (0.30, 0.20, 0.15), 0.55)
    mesh_obj("Head", surf.verts, surf.faces, skin, surf.corner_uv)
    white = material("Sclera", (0.75, 0.72, 0.68), 0.3)
    iris = material("Iris", (0.08, 0.04, 0.02), 0.05)
    for centre, radius in data.eyes:
        verts, faces, _uv, mask = hs.eye_arrays(centre, radius)
        ob = mesh_obj("Eye", verts, faces, white)
        ob.data.materials.append(iris)
        for p, m in zip(ob.data.polygons, mask):
            p.material_index = 1 if m else 0
    return None


def main():
    o = parse()
    look = looks.get_look(o["look"])
    os.makedirs(o["out"], exist_ok=True)
    cam = setup_scene()
    build(look, o["out"], bool(o["textured"]))
    kinds = ["std", "top", "side"] if o["lights"] == "all" else [o["lights"]]
    for kind in kinds:
        place_lights(kind)
        for view in o["views"].split(","):
            tag = "" if kind == "std" else "_" + kind
            shoot(cam, os.path.join(o["out"], "{0}{1}_{2}{3}.png".format(look.id, o["tag"], view, tag)), view)


main()
