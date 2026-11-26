"""The gunner's pistol: a generic full-size semi-automatic, original design, no markings (claude-docs/animation.md,
"Props in the hand").

    blender.exe --background --factory-startup --python Tools/Blender/build_pistol.py -- [--out <dir>] [--renders <dir>] [--no-render]

Writes <assets root>/built/props/SM_Pistol.glb (two materials: Slide, a dark gunmetal, and Frame, black polymer) and
pistol.json (the slots, the pistol's length, where the grip's middle and the muzzle are, for Tools/Editor/import_pistol.py),
then renders a side, a 3/4 and a top view to Saved/Screenshots/Blender/pistol_<view>.png.

The frame of the mesh is the game's pistol frame (AThugCharacter::PistolGripCenterCm): X is the muzzle, Y across, Z up, the
origin the middle of the slide's length and height; metres here, the importer makes centimetres. About 19 cm long.
It is a slide (rear serrations, an ejection port, a muzzle with the barrel showing, front and rear sights, a hammer spur), a
frame (dust cover, trigger guard, trigger, a beavertail, a grip leaned back 12 degrees with ridged side panels and a base
plate on the magazine) and nothing else: a few thousand triangles.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

SLIDE = {"name": "Slide", "color": (0.050, 0.052, 0.058), "roughness": 0.36, "metallic": 0.9}
FRAME = {"name": "Frame", "color": (0.012, 0.012, 0.013), "roughness": 0.64, "metallic": 0.0}

LEAN = math.radians(12.0)     # the grip leans back at the bottom
GRIP_TOP_Z = -0.014
GRIP_BOTTOM_Z = -0.130


def assets_root():
    for candidate in (os.environ.get("HAWKEYE_ASSETS_ROOT", ""), r"C:\Users\camer\code\hawkeye-assets"):
        if candidate and os.path.isdir(candidate):
            return candidate
    return r"C:\Users\camer\code\hawkeye-assets"


def material(spec):
    mat = bpy.data.materials.new("M_Pistol_" + spec["name"])
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*spec["color"], 1.0)
    bsdf.inputs["Roughness"].default_value = spec["roughness"]
    bsdf.inputs["Metallic"].default_value = spec["metallic"]
    return mat


def box(name, lo, hi, bevel=0.0015, mat=None, matrix=None):
    """A box from lo to hi (x, y, z), its edges bevelled by `bevel` (one segment: a chamfer-ish round that catches light)."""
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x = lo[0] if v.co.x < 0 else hi[0]
        v.co.y = lo[1] if v.co.y < 0 else hi[1]
        v.co.z = lo[2] if v.co.z < 0 else hi[2]
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=1, affect="EDGES")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    if matrix is not None:
        obj.matrix_world = matrix
    if mat is not None:
        obj.data.materials.append(mat)
    return obj


def cylinder(name, radius, x0, x1, y=0.0, z=0.0, segments=16, mat=None):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments, radius1=radius, radius2=radius, depth=x1 - x0)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    # create_cone is along Z: turn it onto X.
    obj.rotation_euler = (0.0, math.radians(90.0), 0.0)
    obj.location = ((x0 + x1) * 0.5, y, z)
    if mat is not None:
        obj.data.materials.append(mat)
    return obj


def apply_transforms(obj):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.select_set(False)


def join(objs, name):
    for o in objs:
        apply_transforms(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    out = bpy.context.view_layer.objects.active
    out.name = name
    out.data.name = name
    out.select_set(False)
    return out


def cut(target, cutters, name):
    """target minus the union of cutters (one exact boolean)."""
    cutter = join(cutters, name + "_cutter")
    apply_transforms(target)
    mod = target.modifiers.new(name, "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.solver = "EXACT"
    mod.object = cutter
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def grip_matrix():
    """Leans the handle back about its top: points go back (-X) the lower they are."""
    return Matrix.Translation((0.0, 0.0, GRIP_TOP_Z)) @ Matrix.Rotation(LEAN, 4, "Y") @ Matrix.Translation((0.0, 0.0, -GRIP_TOP_Z))


def build():
    slide_mat, frame_mat = material(SLIDE), material(FRAME)

    # ---- the slide: 18.4 cm, 2.8 wide, 3.2 high; the barrel shows at the muzzle ------------------------------------------
    slide = box("Slide", (-0.095, -0.0140, -0.0140), (0.088, 0.0140, 0.0180), bevel=0.0020, mat=slide_mat)
    # the top is flat with a gentle crown: two chamfered edges came from the bevel; a front taper on the slide's face
    cutters = []
    # rear serrations: seven vertical grooves a side, 1.2 mm deep, 1.6 mm wide, 3.3 mm apart, on the rear 3.4 cm
    for i in range(7):
        x = -0.088 + i * 0.0045
        for side in (-1, 1):
            y_in, y_out = side * 0.0128, side * 0.0152
            cutters.append(box("serr", (x, min(y_in, y_out), -0.0120), (x + 0.0017, max(y_in, y_out), 0.0155), bevel=0.0))
    # the ejection port: 3.4 cm long, 1.2 cm high, through the right side of the slide's top, over the chamber
    cutters.append(box("port", (-0.012, 0.0050, 0.0035), (0.0235, 0.0150, 0.0165), bevel=0.0))
    # the front of the slide's top is cut away over the muzzle's barrel a little: a notch under the front sight base
    cut(slide, cutters, "slide_cuts")

    barrel_mat = slide_mat
    barrel = cylinder("Barrel", 0.0058, 0.080, 0.0955, z=0.0030, segments=18, mat=barrel_mat)
    # the barrel's bore: a dark ring in the muzzle, a shallow recess (a cut into the barrel's end)
    bore = cylinder("Bore", 0.0034, 0.0935, 0.0975, z=0.0030, segments=14)
    cut(barrel, [bore], "bore")
    # the chamber visible in the port: a bar of barrel
    chamber = box("Chamber", (-0.008, 0.0060, 0.0030), (0.018, 0.0105, 0.0060), bevel=0.0, mat=frame_mat)
    # sights: a front post with a ramp, a rear block with a notch
    front = box("FrontSight", (0.073, -0.0016, 0.0180), (0.0835, 0.0016, 0.0232), bevel=0.0006, mat=slide_mat)
    rear_l = box("RearSightL", (-0.0875, -0.0094, 0.0180), (-0.0790, -0.0026, 0.0226), bevel=0.0006, mat=slide_mat)
    rear_r = box("RearSightR", (-0.0875, 0.0026, 0.0180), (-0.0790, 0.0094, 0.0226), bevel=0.0006, mat=slide_mat)
    # a slide stop pin and a safety lever stub on the left of the frame
    stop = box("SlideStop", (-0.006, -0.0160, -0.0215), (0.016, -0.0132, -0.0160), bevel=0.0006, mat=frame_mat)
    stop.data.materials.clear()
    stop.data.materials.append(slide_mat)

    # ---- the frame ------------------------------------------------------------------------------------------------------
    dust = box("DustCover", (-0.010, -0.0112, -0.0270), (0.0840, 0.0112, -0.0138), bevel=0.0016, mat=frame_mat)
    # a rail slot under the dust cover's front
    rail = box("RailCut", (0.034, -0.0030, -0.0290), (0.0800, 0.0030, -0.0238), bevel=0.0)
    cut(dust, [rail], "rail")
    # trigger guard: a rectangular loop in front of the grip, 2.2 cm deep inside
    gx0, gx1, gz_top, gz_bot = -0.004, 0.0505, -0.0138, -0.0455
    guard_parts = [
        box("GuardFront", (gx1 - 0.0062, -0.0058, gz_bot), (gx1, 0.0058, gz_top), bevel=0.0012, mat=frame_mat),
        box("GuardBottom", (gx0, -0.0058, gz_bot), (gx1, 0.0058, gz_bot + 0.0060), bevel=0.0012, mat=frame_mat),
    ]
    trigger = box("Trigger", (0.0170, -0.0030, -0.0385), (0.0235, 0.0030, -0.0138), bevel=0.0008, mat=slide_mat)
    # the trigger leans forward at the bottom
    trigger.matrix_world = Matrix.Translation((0.0, 0.0, -0.0138)) @ Matrix.Rotation(-math.radians(14.0), 4, "Y") @ Matrix.Translation((0.0, 0.0, 0.0138))
    # the hammer's spur and the beavertail
    hammer = box("Hammer", (-0.1005, -0.0035, 0.0030), (-0.0925, 0.0035, 0.0150), bevel=0.0008, mat=slide_mat)
    beaver = box("Beavertail", (-0.0990, -0.0140, -0.0200), (-0.0700, 0.0140, -0.0090), bevel=0.0030, mat=frame_mat)

    # ---- the grip: leaned back, 3.1 cm thick, ridged panels, a magazine base plate -----------------------------------------
    top_x0, top_x1 = -0.0740, -0.0100
    grip = box("Grip", (top_x0, -0.0150, GRIP_BOTTOM_Z), (top_x1, 0.0150, GRIP_TOP_Z + 0.0020), bevel=0.0035, mat=frame_mat)
    grip.matrix_world = grip_matrix()
    # finger-groove ridges on the front strap and the side panels: horizontal ridges in the handle's own frame
    ridges = []
    ridge_h = 0.0016
    z = GRIP_TOP_Z - 0.012
    while z > GRIP_BOTTOM_Z + 0.010:
        for side in (-1, 1):
            y0 = side * 0.0150 if side > 0 else -0.0150 - 0.0007
            y1 = 0.0150 + 0.0007 if side > 0 else -0.0150
            ridges.append(box("ridge", (top_x0 + 0.0060, y0, z), (top_x1 - 0.0100, y1, z + ridge_h), bevel=0.0003, mat=frame_mat))
        z -= 0.0064
    for r in ridges:
        r.matrix_world = grip_matrix()
    # the backstrap's texture and the front strap's: short ridges across the strap
    straps = []
    z = GRIP_TOP_Z - 0.010
    while z > GRIP_BOTTOM_Z + 0.010:
        straps.append(box("backridge", (top_x0 - 0.0007, -0.0100, z), (top_x0 + 0.0030, 0.0100, z + ridge_h), bevel=0.0003, mat=frame_mat))
        straps.append(box("frontridge", (top_x1 - 0.0030, -0.0100, z), (top_x1 + 0.0007, 0.0100, z + ridge_h), bevel=0.0003, mat=frame_mat))
        z -= 0.0064
    for s in straps:
        s.matrix_world = grip_matrix()
    plate = box("BasePlate", (top_x0 - 0.0010, -0.0160, GRIP_BOTTOM_Z - 0.0075), (top_x1 + 0.0010, 0.0160, GRIP_BOTTOM_Z + 0.0005), bevel=0.0018, mat=frame_mat)
    plate.matrix_world = grip_matrix()
    # the mag release button (left, behind the trigger guard)
    release = box("MagRelease", (-0.0090, -0.0172, -0.0390), (-0.0030, -0.0145, -0.0330), bevel=0.0008, mat=slide_mat)

    parts_slide = [slide, barrel, front, rear_l, rear_r, stop, trigger, hammer, release]
    parts_frame = [dust, chamber, beaver, grip, plate] + guard_parts + ridges + straps
    # one object per material: the importer makes a static mesh with two slots, in this order
    slide_obj = join(parts_slide, "Pistol_Slide")
    frame_obj = join(parts_frame, "Pistol_Frame")
    for obj, mat in ((slide_obj, slide_mat), (frame_obj, frame_mat)):
        obj.data.materials.clear()
        obj.data.materials.append(mat)
    for obj in (slide_obj, frame_obj):
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY")
        bpy.ops.mesh.normals_make_consistent(inside=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.object.shade_smooth(keep_sharp_edges=True)
        obj.select_set(False)
    # one object, two material slots (Slide, Frame): the importer makes one static mesh
    whole = join([slide_obj, frame_obj], "Pistol")
    return whole, whole


def grip_middle():
    """The middle of the handle, in the mesh's frame (x, y, z m): the point that sits in the fist."""
    mid_top = Vector(((-0.0740 + -0.0100) * 0.5, 0.0, GRIP_TOP_Z))
    mid_bottom = Vector(((-0.0740 + -0.0100) * 0.5, 0.0, GRIP_BOTTOM_Z))
    m = grip_matrix()
    a, b = m @ mid_top, m @ mid_bottom
    return ((a + b) * 0.5)


def triangles(*objs):
    total = 0
    for obj in objs:
        total += sum(len(p.vertices) - 2 for p in obj.data.polygons)
    return total


def render(objs, out_dir):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 1400, 800
    sc.render.film_transparent = False
    world = bpy.data.worlds.new("W")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.32, 0.34, 0.37, 1.0)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.2
    sc.world = world
    for name, loc, energy in (("key", (0.25, -0.35, 0.30), 120.0), ("fill", (-0.30, 0.25, 0.15), 40.0), ("rim", (-0.1, 0.3, 0.3), 60.0)):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = energy
        data.size = 0.25
        light = bpy.data.objects.new(name, data)
        light.location = loc
        bpy.context.collection.objects.link(light)
        direction = Vector((0.0, 0.0, -0.04)) - light.location
        light.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 0.26
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    sc.camera = cam
    target = Vector((0.0, 0.0, -0.045))
    views = {"side_right": Vector((0.0, 1.0, 0.0)), "side": Vector((0.0, -1.0, 0.0)), "three_quarter": Vector((0.35, -0.9, 0.45)), "top": Vector((0.0, -0.05, 1.0)),
             "front": Vector((1.0, -0.2, 0.15))}
    os.makedirs(out_dir, exist_ok=True)
    for name, direction in views.items():
        cam.location = target + direction.normalized() * 0.6
        cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(out_dir, "pistol_{0}.png".format(name))
        bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    def option(flag, default):
        return argv[argv.index(flag) + 1] if flag in argv else default

    out_dir = option("--out", os.path.join(assets_root(), "built", "props"))
    renders = option("--renders", os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                               "Saved", "Screenshots", "Blender"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    slide_obj, frame_obj = build()
    os.makedirs(out_dir, exist_ok=True)
    bounds = [Vector(c) for c in slide_obj.bound_box]
    lo = Vector((min(b.x for b in bounds), min(b.y for b in bounds), min(b.z for b in bounds)))
    hi = Vector((max(b.x for b in bounds), max(b.y for b in bounds), max(b.z for b in bounds)))
    mid = grip_middle()
    info = {
        "slots": [dict(SLIDE, color=list(SLIDE["color"])), dict(FRAME, color=list(FRAME["color"]))],
        "triangles": triangles(slide_obj),
        "length_cm": round((hi.x - lo.x) * 100.0, 2),
        "bounds_cm": [[round(v * 100.0, 2) for v in lo], [round(v * 100.0, 2) for v in hi]],
        "grip_center_cm": [round(v * 100.0, 2) for v in mid],
        "muzzle_cm": [round(hi.x * 100.0, 2), 0.0, 0.3],
    }
    with open(os.path.join(out_dir, "pistol.json"), "w", encoding="utf-8") as handle:
        json.dump(info, handle, indent=2)
    bpy.ops.object.select_all(action="DESELECT")
    slide_obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(out_dir, "SM_Pistol.glb"), export_format="GLB", use_selection=True,
                              export_yup=False, export_apply=True)
    print("PISTOL", json.dumps(info))
    if "--no-render" not in argv:
        render((slide_obj, frame_obj), renders)


if __name__ == "__main__":
    main()
