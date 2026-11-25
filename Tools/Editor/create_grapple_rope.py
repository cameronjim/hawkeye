"""The grapple rope's look, and the shot switched on for the player's class.

    /Game/VFX/Materials/M_GrappleRope    braided nylon grey, lit by the scene (not emissive), a slight sheen and at most a very
                                         faint cool rim so the cord is findable at night. UGrappleRopeComponent draws it on the
                                         engine cylinder, one instance per piece of the rope.

Then, on BP_HawkeyeCharacter's grapple component (Kate and Clint inherit it): the grapple shot on
(`shot.enabled`: the raise, draw and release before the arrow leaves, the rope instead of the Niagara beam, the arrow taken
out after a let-go) and the bite sound. The numbers themselves (FHawkeyeGrappleShotSettings, FHawkeyeGrappleRopeSettings) are the C++
defaults; edit them on BP_HawkeyeCharacter to tune.

Idempotent: the material carries a HawkeyeRopeHash tag (its recipe); an unchanged recipe is left alone, a changed one is rebuilt in
place. The Blueprint is compiled and saved only when something changed.

Run headless as a step of create_all.py, or alone:

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script="Tools\\Editor\\create_grapple_rope.py" -unattended -nullrhi
"""

import hashlib
import inspect
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402

MATERIALS_PATH = "/Game/VFX/Materials"
ROPE_MATERIAL = "M_GrappleRope"
HASH_TAG = "HawkeyeRopeHash"
ROPE_BUILD = "rope-1"      # bump to rebuild

PLAYER_PATH = "/Game/Blueprints/Player"
PLAYER_BLUEPRINT = "BP_HawkeyeCharacter"

# Braided nylon, linear: a dark cool grey, not black (it has to read against snow and brick at night under the character rig).
ROPE_COLOR = (0.045, 0.047, 0.052)
ROPE_ROUGHNESS = 0.42
ROPE_SPECULAR = 0.5
# The rim: a cool tint at a fraction of an emissive unit. Very faint by design; 0 turns it off.
ROPE_RIM_COLOR = (0.45, 0.6, 1.0)
ROPE_RIM_INTENSITY = 0.05


def _build_rope(material):
    """Lit, opaque, two sided; Color, Roughness, a Fresnel rim."""
    c.set_props(material, [("two_sided", True)], material.get_name())
    m.connect_property(m.vector_param(material, "Color", ROPE_COLOR, -700, -250), unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(m.scalar_param(material, "Roughness", ROPE_ROUGHNESS, -700, -100), unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_scalar_property(material, ROPE_SPECULAR, unreal.MaterialProperty.MP_SPECULAR, -700, 0)
    m.set_scalar_property(material, 0.0, unreal.MaterialProperty.MP_METALLIC, -700, 100)

    fresnel = m.expr(material, "MaterialExpressionFresnel", -900, 350, [("exponent", 3.0), ("base_reflect_fraction", 0.0)], "Fresnel")
    rim_color = m.vector_param(material, "RimColor", ROPE_RIM_COLOR, -900, 500)
    rim_intensity = m.scalar_param(material, "RimIntensity", ROPE_RIM_INTENSITY, -900, 650)
    rim = m.multiply(material, m.multiply(material, fresnel, rim_color, -650, 400), rim_intensity, -450, 450)
    m.connect_property(m.multiply(material, rim, None, -250, 450, const_b=m.EMISSIVE_INTENSITY_FACTOR),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _hash():
    source = inspect.getsource(_build_rope) + repr((ROPE_COLOR, ROPE_ROUGHNESS, ROPE_SPECULAR, ROPE_RIM_COLOR, ROPE_RIM_INTENSITY,
                                                    m.EMISSIVE_INTENSITY_FACTOR))
    return hashlib.sha1((ROPE_BUILD + "\n" + source).encode("utf-8")).hexdigest()[:16]


def _tag_of(asset):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(asset, HASH_TAG) if asset is not None else None
    except Exception:  # noqa: BLE001
        return None


def ensure_rope_material():
    full = c.asset_path(MATERIALS_PATH, ROPE_MATERIAL)
    wanted = _hash()
    existing = c.load_or_none(full)
    if existing is not None and _tag_of(existing) == wanted:
        c.log("exists", full)
        return existing
    c.ensure_directory(MATERIALS_PATH)
    material = m.ensure_material(full, _build_rope, rebuild=existing is not None, instanced=True)
    if material is None:
        return None
    unreal.EditorAssetLibrary.set_metadata_tag(material, HASH_TAG, wanted)
    c.save(material)
    return material


def _struct_bool(struct, names, value):
    """Sets the first of names that is a property of struct to value. Returns True when it changed."""
    for name in names:
        try:
            current = struct.get_editor_property(name)
        except Exception:  # noqa: BLE001
            continue
        if bool(current) == value:
            return False
        struct.set_editor_property(name, value)
        return True
    c.log("skipped", "grapple shot settings", "none of {0} is a property".format(names))
    return False


def apply_grapple_defaults():
    """The shot on, on BP_HawkeyeCharacter's grapple component."""
    full = c.asset_path(PLAYER_PATH, PLAYER_BLUEPRINT)
    bp = c.load_or_none(full)
    cdo = c.blueprint_cdo(bp) if bp is not None else None
    grapple = None
    if cdo is not None:
        try:
            grapple = cdo.get_editor_property("grapple_component")
        except Exception:  # noqa: BLE001
            grapple = None
    if grapple is None:
        c.log("skipped", full + " grapple shot", "Blueprint or its grapple component not found")
        return
    changed = []
    try:
        shot = grapple.get_editor_property("shot")
        if _struct_bool(shot, ("enabled", "b_enabled"), True):
            grapple.set_editor_property("shot", shot)
            changed.append("shot.enabled")
    except Exception as exc:  # noqa: BLE001
        c.log_error("grapple shot " + full, exc)
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", full, "grapple: " + ", ".join(changed))
    else:
        c.log("exists", full, "grapple shot already on")


def run():
    ensure_rope_material()
    apply_grapple_defaults()


if __name__ == "__main__":
    run()
    c.print_summary("grapple rope")
