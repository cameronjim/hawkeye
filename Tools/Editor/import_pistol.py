"""The gunner's pistol: /Game/Characters/Bodies/Built/Props/SM_Pistol and its two materials.

Source: <assets root>\\built\\props\\SM_Pistol.glb and pistol.json, written by Tools/Blender/build_pistol.py (our own model, no
brand markings; centimetres after the import, X the muzzle, the middle of the slide at the origin). Imported as one static mesh
with two slots, Slide and Frame; the materials are instances (colour, roughness, metallic from pistol.json) of M_PistolBuilt, a
plain static-mesh material.

Like the built bodies this is generated, git-ignored (Content/Characters/Bodies/) and idempotent: a build tag made of the file
sizes and times says whether the import is current. Without the glb the step skips and AThugCharacter keeps its slab of engine
cubes. Run by Tools/create-content.ps1 (step "pistol").
"""

import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402
import import_bodies as ib  # noqa: E402

PROPS = ib.BODIES + "/Built/Props"
MESH = PROPS + "/SM_Pistol"
M_PISTOL = PROPS + "/M_PistolBuilt"
MATERIAL_BUILD = "pistol-material-1"
MESH_BUILD = "pistol-mesh-1"


def source(name):
    return ib.source_file(("built", "props", name))


def _build_material(material):
    color = m.vector_param(material, "Color", (0.05, 0.05, 0.05), -600, -200)
    m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(m.scalar_param(material, "Roughness", 0.5, -600, 0), unreal.MaterialProperty.MP_ROUGHNESS)
    m.connect_property(m.scalar_param(material, "Metallic", 0.0, -600, 100), unreal.MaterialProperty.MP_METALLIC)


def _parent():
    existing = c.load_or_none(M_PISTOL)
    material = m.ensure_material(M_PISTOL, _build_material,
                                 rebuild=existing is not None and ib.tag_of(existing) != MATERIAL_BUILD)
    if material is not None and ib.tag_of(material) != MATERIAL_BUILD:
        ib.set_tag(material, MATERIAL_BUILD)
        c.save(material)
    return material


def _pipeline():
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    mesh_pipeline = pipeline.get_editor_property("mesh_pipeline")
    c.set_props(mesh_pipeline, [("import_static_meshes", True), ("import_skeletal_meshes", False),
                                ("generate_lightmap_u_vs", False)], "interchange mesh pipeline")
    c.set_props(pipeline.get_editor_property("animation_pipeline"), [("import_animations", False)], "interchange animation pipeline")
    material_pipeline = pipeline.get_editor_property("material_pipeline")
    c.set_props(material_pipeline, [("import_materials", False)], "interchange material pipeline")
    c.set_props(material_pipeline.get_editor_property("texture_pipeline"), [("import_textures", False)], "interchange texture pipeline")
    return pipeline


def import_mesh(glb, info_file):
    stamp = "{0}|{1}|{2}".format(MESH_BUILD, ib._file_stamp(glb), ib._file_stamp(info_file))
    existing = c.load_or_none(MESH)
    if existing is not None and ib.tag_of(existing) == stamp:
        c.log("exists", MESH)
        return existing
    staging = PROPS + "/Import"
    c.ensure_directory(staging)
    ok, objects = ib._interchange_import(glb, staging, _pipeline())
    meshes = [a for a in ib._assets_under(staging) if isinstance(a, unreal.StaticMesh)]
    if not meshes:
        c.log("FAILED", MESH, "the import made no static mesh (ok={0}, {1} objects)".format(ok, len(objects)))
        unreal.EditorAssetLibrary.delete_directory(staging)
        return None
    mesh = ib._rename(meshes[0], MESH)
    for leftover in ib._assets_under(staging):
        unreal.EditorAssetLibrary.delete_asset(leftover.get_path_name().split(".")[0])
    unreal.EditorAssetLibrary.delete_directory(staging)
    ib.set_tag(mesh, stamp)
    c.save(mesh)
    bounds = mesh.get_bounds()
    c.log("imported", MESH, "from {0}: {1:.1f} x {2:.1f} x {3:.1f} cm, {4} slots".format(
        os.path.basename(glb), 2.0 * bounds.box_extent.x, 2.0 * bounds.box_extent.y, 2.0 * bounds.box_extent.z,
        len(mesh.get_editor_property("static_materials"))))
    return mesh


def put_materials(mesh, slots):
    parent = _parent()
    if parent is None:
        return
    wanted = {}
    for slot in slots:
        label = "{0}/MI_Pistol_{1}".format(PROPS, slot["name"])
        wanted[slot["name"].lower()] = m.ensure_material_instance(
            label, parent, vectors=[("Color", tuple(slot["color"]))],
            scalars=[("Roughness", slot["roughness"]), ("Metallic", slot["metallic"])])
    materials = list(mesh.get_editor_property("static_materials"))
    changed = 0
    for index, entry in enumerate(materials):
        name = str(entry.get_editor_property("material_slot_name")).lower()
        instance = next((v for k, v in wanted.items() if k in name), None)
        if instance is None and index < len(slots):
            instance = wanted.get(slots[index]["name"].lower())
        if instance is not None and entry.get_editor_property("material_interface") != instance:
            entry.set_editor_property("material_interface", instance)
            changed += 1
    if changed:
        mesh.set_editor_property("static_materials", materials)
        c.save(mesh)
        c.log("updated", MESH, "materials on {0} slot(s)".format(changed))
    else:
        c.log("exists", MESH + " materials")


def run():
    glb = source("SM_Pistol.glb")
    info_file = source("pistol.json")
    if not (os.path.isfile(glb) and os.path.isfile(info_file)):
        c.log("skipped", MESH, "no {0}: the gunner keeps the slab of engine cubes (run Tools/Blender/build_pistol.py)".format(glb))
        return None
    with open(info_file, encoding="utf-8") as handle:
        info = json.load(handle)
    c.ensure_directory(PROPS)
    mesh = import_mesh(glb, info_file)
    if mesh is not None:
        put_materials(mesh, info["slots"])
    return mesh
